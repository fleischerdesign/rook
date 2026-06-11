#include "rook/adapters/audio/sherpa_adapter.hpp"
#include "rook/adapters/audio/model_downloader.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

namespace rook::adapters::audio {

namespace {

std::optional<std::string> find_sherpa_binary() {
    const char* path = ::getenv("PATH");
    if (!path) return std::nullopt;

    std::string paths(path);
    std::size_t start = 0;
    while (start <= paths.size()) {
        auto end = paths.find(':', start);
        if (end == std::string::npos) end = paths.size();
        auto dir = paths.substr(start, end - start);
        auto candidate = dir + "/sherpa-onnx-offline-tts";
        if (std::filesystem::exists(candidate) &&
            std::filesystem::is_regular_file(candidate))
            return candidate;
        start = end + 1;
    }
    return std::nullopt;
}

std::string find_espeak_data(std::string_view binary_path) {
    auto try_dir = [](const std::filesystem::path& p) -> std::string {
        auto canonical = std::filesystem::weakly_canonical(p);
        if (std::filesystem::exists(canonical)) return canonical;
        return {};
    };

    auto bin_dir = std::filesystem::path(binary_path).parent_path();
    auto share = try_dir(bin_dir / ".." / "share" / "sherpa-onnx" / "espeak-ng-data");
    if (!share.empty()) return share;

    auto* data_dir = ::getenv("XDG_DATA_HOME");
    if (data_dir) {
        auto base = std::filesystem::path(data_dir) / "rook" / "models" / "sherpa";
        for (auto& sub : {"espeak-ng-data", "espeak-ng-data/espeak-ng-data",
                          "vits-piper-de_DE-thorsten-medium/espeak-ng-data",
                          "vits-piper-de_DE-thorsten-medium"}) {
            auto s = try_dir(base / sub);
            if (!s.empty()) return s;
        }
    }
    auto* home = ::getenv("HOME");
    if (home) {
        auto base = std::filesystem::path(home) / ".local" / "share" / "rook" / "models" / "sherpa";
        for (auto& sub : {"espeak-ng-data", "espeak-ng-data/espeak-ng-data",
                          "vits-piper-de_DE-thorsten-medium/espeak-ng-data",
                          "vits-piper-de_DE-thorsten-medium"}) {
            auto s = try_dir(base / sub);
            if (!s.empty()) return s;
        }
    }
    return {};
}

constexpr int k_sample_rate = 22050;

} // anonymous namespace

struct SherpaAdapter::Impl {
    std::string binary_path;
    std::string model_path;
    std::string tokens_path;
    std::string data_dir;
    std::string voice_id;
    std::atomic<bool> stop_requested{false};
    std::thread speech_thread;
    pid_t child_pid = 0;
};

SherpaAdapter::SherpaAdapter(std::string model_path, std::string voice_id)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->voice_id = std::move(voice_id);

    auto bin = find_sherpa_binary();
    if (bin) {
        m_impl->binary_path = *bin;
        SPDLOG_DEBUG("SherpaAdapter: found binary at {}", m_impl->binary_path);
        m_impl->data_dir = find_espeak_data(m_impl->binary_path);
        if (!m_impl->data_dir.empty())
            SPDLOG_DEBUG("SherpaAdapter: espeak-ng-data at {}", m_impl->data_dir);
        else
            SPDLOG_WARN("SherpaAdapter: espeak-ng-data not found");
    } else {
        SPDLOG_WARN("SherpaAdapter: sherpa-onnx-offline-tts not found in PATH");
    }

    m_impl->model_path = model_path.empty() ? defaultModelPath() : std::move(model_path);

    auto model_dir = std::filesystem::path(m_impl->model_path).parent_path();
    m_impl->tokens_path = (model_dir / "tokens.txt").string();
}

SherpaAdapter::~SherpaAdapter() { stop(); }

bool SherpaAdapter::isReady() const {
    return !m_impl->binary_path.empty() && !m_impl->model_path.empty() &&
           std::filesystem::exists(m_impl->model_path) &&
           std::filesystem::exists(m_impl->tokens_path) &&
           !m_impl->data_dir.empty();
}

std::vector<ports::VoiceInfo> SherpaAdapter::listVoices() const {
    return {};
}

void SherpaAdapter::setVoice(std::string_view voice_id) {
    m_impl->voice_id = voice_id;
}

void SherpaAdapter::speak(std::string_view text,
                          std::function<void(const float* pcm, std::size_t sample_count,
                                             int sample_rate, bool is_last)> on_chunk) {
    stop();

    if (!isReady()) {
        SPDLOG_WARN("SherpaAdapter: not ready");
        float dummy = 0.0f;
        on_chunk(&dummy, 0, k_sample_rate, true);
        return;
    }

    m_impl->stop_requested.store(false, std::memory_order_release);
    m_impl->child_pid = 0;

    std::string text_copy(text);
    std::string model = m_impl->model_path;
    std::string tokens = m_impl->tokens_path;
    std::string data_dir = m_impl->data_dir;
    std::string binary = m_impl->binary_path;

    m_impl->speech_thread = std::thread([this, text_copy, model, tokens, data_dir,
                                         binary, cb = std::move(on_chunk)]() mutable {
        char wav_path[] = "/tmp/rook-sherpa-XXXXXX.wav";
        int wav_fd = ::mkstemps(wav_path, 4);
        if (wav_fd < 0) {
            SPDLOG_ERROR("SherpaAdapter: cannot create temp file");
            float dummy = 0.0f;
            cb(&dummy, 0, k_sample_rate, true);
            return;
        }
        ::close(wav_fd);

        pid_t pid = ::fork();
        if (pid < 0) {
            SPDLOG_ERROR("SherpaAdapter: fork failed");
            ::unlink(wav_path);
            float dummy = 0.0f;
            cb(&dummy, 0, k_sample_rate, true);
            return;
        }

        if (pid == 0) {
            auto vits_model = "--vits-model=" + model;
            auto vits_tokens = "--vits-tokens=" + tokens;
            auto vits_data = "--vits-data-dir=" + data_dir;
            auto output = "--output-filename=" + std::string(wav_path);

            ::execlp(binary.c_str(),
                     "sherpa-onnx-offline-tts",
                     vits_model.c_str(),
                     vits_tokens.c_str(),
                     vits_data.c_str(),
                     output.c_str(),
                     "--", text_copy.c_str(),
                     nullptr);
            _exit(127);
        }

        m_impl->child_pid = pid;

        int status = 0;
        ::waitpid(pid, &status, 0);
        m_impl->child_pid = 0;

        if (m_impl->stop_requested.load(std::memory_order_acquire)) {
            ::unlink(wav_path);
            return;
        }

        auto* f = std::fopen(wav_path, "rb");
        if (!f) {
            SPDLOG_ERROR("SherpaAdapter: cannot open {}", wav_path);
            ::unlink(wav_path);
            float dummy = 0.0f;
            cb(&dummy, 0, k_sample_rate, true);
            return;
        }

        std::fseek(f, 44, SEEK_SET);

        std::array<int16_t, 2048> raw_buf;
        std::array<float, 2048> float_buf;
        std::size_t nread;

        while ((nread = std::fread(raw_buf.data(), sizeof(int16_t),
                                   raw_buf.size(), f)) > 0) {
            for (std::size_t i = 0; i < nread; ++i)
                float_buf[i] = static_cast<float>(raw_buf[i]) / 32768.0f;
            cb(float_buf.data(), nread, k_sample_rate, false);
        }

        std::fclose(f);
        ::unlink(wav_path);

        if (!m_impl->stop_requested.load(std::memory_order_acquire)) {
            float dummy = 0.0f;
            cb(&dummy, 0, k_sample_rate, true);
        }
    });
}

void SherpaAdapter::stop() {
    m_impl->stop_requested.store(true, std::memory_order_release);
    if (m_impl->child_pid > 0) {
        ::kill(m_impl->child_pid, SIGTERM);
        m_impl->child_pid = 0;
    }
    if (m_impl->speech_thread.joinable()) {
        m_impl->speech_thread.join();
    }
}

std::string SherpaAdapter::defaultModelPath() const {
    auto* d = ::getenv("XDG_DATA_HOME");
    std::string base = d ? std::string(d) : std::string(::getenv("HOME")) + "/.local/share";
    return base + "/rook/models/sherpa/vits-piper-de_DE-thorsten-medium/de_DE-thorsten-medium.onnx";
}

std::string SherpaAdapter::defaultModelUrl() const {
    return "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-piper-de_DE-thorsten-medium.tar.bz2";
}

void SherpaAdapter::downloadModel(ProgressFn on_progress, DoneFn on_done) {
    auto path = defaultModelPath();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    auto model_url = defaultModelUrl();
    auto tokens_url = model_url; // same archive, not used — tokens are bundled
    auto data_url = "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/espeak-ng-data.tar.bz2";
    auto data_path = std::filesystem::path(path).parent_path().string() + "/espeak-ng-data";

    downloadFile(model_url, path + ".tar.bz2",
        [on_progress](float p) { if (on_progress) on_progress(p * 0.6f); },
        [this, path, data_url, data_path, on_progress, on_done](bool model_ok) {
            if (!model_ok) { if (on_done) on_done(false); return; }

            // Extract model archive
            std::string extract_cmd = "tar xf " + path + ".tar.bz2 -C " +
                std::filesystem::path(path).parent_path().string();
            int ret = std::system(extract_cmd.c_str());
            if (ret != 0) {
                SPDLOG_ERROR("SherpaAdapter: failed to extract model archive");
                if (on_done) on_done(false);
                return;
            }
            std::filesystem::remove(path + ".tar.bz2");

            downloadFile(data_url, data_path + ".tar.bz2",
                [on_progress](float p) { if (on_progress) on_progress(0.6f + p * 0.4f); },
                [this, path, data_path, on_done](bool data_ok) {
                    if (!data_ok) { if (on_done) on_done(false); return; }

                    std::string extract_cmd = "mkdir -p " + data_path + " && tar xf " +
                        data_path + ".tar.bz2 -C " + data_path;
                    int ret = std::system(extract_cmd.c_str());
                    std::filesystem::remove(data_path + ".tar.bz2");

                    if (ret == 0) {
                        m_impl->model_path = path;
                        m_impl->tokens_path = std::filesystem::path(path)
                            .replace_extension("").string() + ".tokens.txt";
                        m_impl->data_dir = data_path;
                    }
                    if (on_done) on_done(ret == 0);
                });
        });
}

} // namespace rook::adapters::audio
