#include "rook/adapters/audio/whisper_adapter.hpp"
#include "rook/adapters/audio/model_downloader.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <unistd.h>
#include <sys/wait.h>

namespace rook::adapters::audio {

namespace {

std::optional<std::string> find_whisper_binary() {
    const char* path = ::getenv("PATH");
    if (!path) return std::nullopt;

    std::string paths(path);
    std::size_t start = 0;
    while (start <= paths.size()) {
        auto end = paths.find(':', start);
        if (end == std::string::npos) end = paths.size();
        auto dir = paths.substr(start, end - start);
        auto candidate = dir + "/whisper-cli";
        if (std::filesystem::exists(candidate) &&
            std::filesystem::is_regular_file(candidate))
            return candidate;
        start = end + 1;
    }
    return std::nullopt;
}

void write_wav_header(std::FILE* f, std::size_t data_size, int sample_rate) {
    std::uint32_t byte_rate = static_cast<std::uint32_t>(sample_rate) * 1 * 2;
    std::uint16_t block_align = 1 * 2;
    std::uint32_t chunk_size = 36 + static_cast<std::uint32_t>(data_size);

    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&chunk_size, 4, 1, f);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);

    std::uint32_t subchunk1_size = 16;
    std::uint16_t audio_format = 1;
    std::uint16_t num_channels = 1;
    std::uint32_t sr = static_cast<std::uint32_t>(sample_rate);

    std::fwrite(&subchunk1_size, 4, 1, f);
    std::fwrite(&audio_format, 2, 1, f);
    std::fwrite(&num_channels, 2, 1, f);
    std::fwrite(&sr, 4, 1, f);
    std::fwrite(&byte_rate, 4, 1, f);
    std::fwrite(&block_align, 2, 1, f);

    std::uint16_t bits_per_sample = 16;
    std::fwrite(&bits_per_sample, 2, 1, f);

    std::fwrite("data", 1, 4, f);

    std::uint32_t subchunk2_size = static_cast<std::uint32_t>(data_size);
    std::fwrite(&subchunk2_size, 4, 1, f);
}

} // anonymous namespace

struct WhisperAdapter::Impl {
    std::string model_path;
    std::string binary_path;
    std::atomic<bool> cancel_requested{false};
};

WhisperAdapter::WhisperAdapter(std::string model_path)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->model_path = std::move(model_path);

    auto bin = find_whisper_binary();
    if (bin) {
        m_impl->binary_path = *bin;
        SPDLOG_DEBUG("WhisperAdapter: found binary at {}", m_impl->binary_path);
    } else {
        SPDLOG_WARN("WhisperAdapter: whisper-cli not found in PATH");
    }

    if (m_impl->model_path.empty())
        m_impl->model_path = defaultModelPath();
}

WhisperAdapter::~WhisperAdapter() = default;

bool WhisperAdapter::isReady() const {
    return !m_impl->binary_path.empty() && !m_impl->model_path.empty() &&
           std::filesystem::exists(m_impl->model_path);
}

void WhisperAdapter::transcribe(const int16_t* audio, std::size_t sample_count,
                                 int sample_rate,
                                 std::function<void(ports::SttResult)> on_result) {
    if (!isReady()) {
        SPDLOG_WARN("WhisperAdapter: not ready (binary={} model={})",
                    m_impl->binary_path, m_impl->model_path);
        on_result({"", false});
        return;
    }

    m_impl->cancel_requested.store(false);

    char tmpname[] = "/tmp/rook-whisper-XXXXXX.wav";
    int tmpfd = ::mkstemps(tmpname, 4);
    if (tmpfd < 0) {
        SPDLOG_ERROR("WhisperAdapter: cannot create temp file");
        on_result({"", false});
        return;
    }

    auto* wav = ::fdopen(tmpfd, "wb");
    if (!wav) {
        ::close(tmpfd);
        on_result({"", false});
        return;
    }

    std::size_t data_size = sample_count * sizeof(int16_t);
    write_wav_header(wav, data_size, sample_rate);
    std::fwrite(audio, 1, data_size, wav);
    std::fclose(wav);

    int pipefd[2];
    if (::pipe(pipefd) != 0) {
        SPDLOG_ERROR("WhisperAdapter: pipe creation failed");
        std::filesystem::remove(tmpname);
        on_result({"", false});
        return;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        SPDLOG_ERROR("WhisperAdapter: fork failed");
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        std::filesystem::remove(tmpname);
        on_result({"", false});
        return;
    }

    if (pid == 0) {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::close(pipefd[1]);

        const char* lang = ::getenv("LANG");
        const char* slang = (lang && std::string_view(lang).starts_with("de")) ? "de" : "en";

        ::execlp(m_impl->binary_path.c_str(),
                 "whisper-cli",
                 "-m", m_impl->model_path.c_str(),
                 "-f", tmpname,
                 "-l", slang,
                 "--output-txt",
                 "--no-timestamps",
                 nullptr);
        _exit(127);
    }

    ::close(pipefd[1]);

    std::string transcript;
    std::array<char, 4096> buf;
    ssize_t n;
    while ((n = ::read(pipefd[0], buf.data(), buf.size())) > 0) {
        if (m_impl->cancel_requested.load(std::memory_order_acquire)) {
            ::kill(pid, SIGTERM);
            break;
        }
        transcript.append(buf.data(), static_cast<std::size_t>(n));
    }
    ::close(pipefd[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    std::filesystem::remove(tmpname);

    if (m_impl->cancel_requested.load()) {
        SPDLOG_DEBUG("WhisperAdapter: transcription cancelled");
        on_result({"", false});
        return;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        SPDLOG_ERROR("WhisperAdapter: whisper-cli exited with {}", WEXITSTATUS(status));
        on_result({"", false});
        return;
    }

    if (WIFSIGNALED(status)) {
        SPDLOG_ERROR("WhisperAdapter: whisper-cli killed by signal {}", WTERMSIG(status));
        on_result({"", false});
        return;
    }

    while (!transcript.empty() &&
           (transcript.back() == '\n' || transcript.back() == ' ' || transcript.back() == '\r'))
        transcript.pop_back();

    SPDLOG_DEBUG("WhisperAdapter: transcription done ({} chars)", transcript.size());
    on_result({std::move(transcript), true});
}

void WhisperAdapter::cancel() {
    m_impl->cancel_requested.store(true, std::memory_order_release);
}

void WhisperAdapter::setModel(std::string_view path) {
    m_impl->model_path = path;
}

std::vector<std::string> WhisperAdapter::availableModels() const {
    return {"tiny", "base", "small", "medium", "large-v3"};
}

std::string WhisperAdapter::defaultModelPath() const {
    auto* d = ::getenv("XDG_DATA_HOME");
    std::string base = d ? std::string(d) : std::string(::getenv("HOME")) + "/.local/share";
    return base + "/rook/models/whisper/ggml-small.bin";
}

std::string WhisperAdapter::defaultModelUrl() const {
    return "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin";
}

void WhisperAdapter::downloadModel(ProgressFn on_progress, DoneFn on_done) {
    auto path = defaultModelPath();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    downloadFile(defaultModelUrl(), path,
        [on_progress](float p) { if (on_progress) on_progress(p); },
        [this, path, on_done](bool success) {
            if (success) m_impl->model_path = path;
            if (on_done) on_done(success);
        });
}

} // namespace rook::adapters::audio
