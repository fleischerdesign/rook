#include "rook/adapters/audio/piper_adapter.hpp"
#include "rook/adapters/audio/model_downloader.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

namespace rook::adapters::audio {

namespace {

std::optional<std::string> find_piper_binary() {
    const char* path = ::getenv("PATH");
    if (!path) return std::nullopt;

    std::string paths(path);
    std::size_t start = 0;
    while (start <= paths.size()) {
        auto end = paths.find(':', start);
        if (end == std::string::npos) end = paths.size();
        auto dir = paths.substr(start, end - start);
        auto candidate = dir + "/piper";
        if (std::filesystem::exists(candidate) &&
            std::filesystem::is_regular_file(candidate))
            return candidate;
        start = end + 1;
    }
    return std::nullopt;
}

void close_non_pipe_fds(int keep_0, int keep_1) {
    DIR* fds = ::opendir("/proc/self/fd");
    if (!fds) return;
    struct dirent* de;
    while ((de = ::readdir(fds))) {
        int fd = std::atoi(de->d_name);
        if (fd > STDERR_FILENO && fd != keep_0 && fd != keep_1)
            ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    ::closedir(fds);
}

constexpr int k_piper_sample_rate = 22050;

class PiperProcess {
public:
    PiperProcess() = default;

    ~PiperProcess() { stop(); }

    void configure(std::string_view binary, std::string_view model) {
        m_binary = binary;
        m_model = model;
    }

    void synthesize(std::string_view text,
                    const std::function<void(const float*, size_t, bool)>& cb,
                    std::atomic<bool>& stop_flag) {
        std::lock_guard<std::mutex> lock(m_mutex);

        int pipe_in[2];
        int pipe_out[2];
        if (::pipe(pipe_in) != 0 || ::pipe(pipe_out) != 0) {
            SPDLOG_ERROR("PiperProcess: pipe creation failed");
            float dummy = 0.0f;
            cb(&dummy, 0, true);
            return;
        }

        close_non_pipe_fds(pipe_in[0], pipe_out[1]);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipe_in[0], STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipe_in[0]);
        posix_spawn_file_actions_adddup2(&actions, pipe_out[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipe_out[1]);
        posix_spawn_file_actions_addclose(&actions, pipe_in[1]);
        posix_spawn_file_actions_addclose(&actions, pipe_out[0]);

        char* argv[] = {
            const_cast<char*>("piper"),
            const_cast<char*>("--model"),
            const_cast<char*>(m_model.c_str()),
            const_cast<char*>("--output-raw"),
            nullptr
        };

        pid_t pid;
        int ret = ::posix_spawnp(&pid, m_binary.c_str(), &actions, nullptr,
                                  argv, environ);
        posix_spawn_file_actions_destroy(&actions);

        ::close(pipe_in[0]);
        ::close(pipe_out[1]);

        if (ret != 0) {
            SPDLOG_ERROR("PiperProcess: posix_spawnp failed (errno={})", ret);
            ::close(pipe_in[1]);
            ::close(pipe_out[0]);
            float dummy = 0.0f;
            cb(&dummy, 0, true);
            return;
        }

        m_pid = pid;

        auto written = ::write(pipe_in[1], text.data(), text.size());
        if (written < 0) {
            SPDLOG_ERROR("PiperProcess: write to piper stdin failed");
        }
        ::close(pipe_in[1]);

        std::array<int16_t, 2048> raw_buf;
        std::array<float, 2048> float_buf;
        ssize_t n;
        bool cancelled = false;

        while ((n = ::read(pipe_out[0], raw_buf.data(),
                           raw_buf.size() * sizeof(int16_t))) > 0) {
            if (stop_flag.load(std::memory_order_acquire)) {
                ::kill(pid, SIGTERM);
                cancelled = true;
                break;
            }
            size_t samples = static_cast<size_t>(n) / sizeof(int16_t);
            for (size_t i = 0; i < samples; ++i)
                float_buf[i] = static_cast<float>(raw_buf[i]) / 32768.0f;
            cb(float_buf.data(), samples, false);
        }

        ::close(pipe_out[0]);

        if (!cancelled) {
            int status = 0;
            ::waitpid(pid, &status, 0);
        }
        m_pid = -1;

        if (!stop_flag.load(std::memory_order_acquire)) {
            float dummy = 0.0f;
            cb(&dummy, 0, true);
        }
    }

    void stop() {
        if (m_pid > 0) {
            ::kill(m_pid, SIGTERM);
            int status;
            ::waitpid(m_pid, &status, 0);
            m_pid = -1;
        }
    }

private:
    std::string m_binary;
    std::string m_model;
    pid_t m_pid = -1;
    std::mutex m_mutex;
};

} // anonymous namespace

struct PiperAdapter::Impl {
    std::string model_path;
    std::string voice_id;
    std::string binary_path;
    PiperProcess process;
    std::atomic<bool> stop_requested{false};
    std::thread speech_thread;
};

PiperAdapter::PiperAdapter(std::string model_path, std::string voice_id)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->model_path = std::move(model_path);
    m_impl->voice_id = std::move(voice_id);

    auto bin = find_piper_binary();
    if (bin) {
        m_impl->binary_path = *bin;
        SPDLOG_DEBUG("PiperAdapter: found binary at {}", m_impl->binary_path);
    } else {
        SPDLOG_WARN("PiperAdapter: piper not found in PATH");
    }

    if (m_impl->model_path.empty())
        m_impl->model_path = defaultModelPath();

    m_impl->process.configure(m_impl->binary_path, m_impl->model_path);
}

PiperAdapter::~PiperAdapter() { stop(); }

bool PiperAdapter::isReady() const {
    return !m_impl->binary_path.empty() && !m_impl->model_path.empty() &&
           std::filesystem::exists(m_impl->model_path) &&
           std::filesystem::exists(m_impl->model_path + ".json");
}

std::vector<ports::VoiceInfo> PiperAdapter::listVoices() const {
    return {};
}

void PiperAdapter::setVoice(std::string_view voice_id) {
    m_impl->voice_id = voice_id;
}

void PiperAdapter::speak(std::string_view text,
                          std::function<void(const float* pcm, std::size_t sample_count,
                                             int sample_rate, bool is_last)> on_chunk) {
    stop();

    if (!isReady()) {
        SPDLOG_WARN("PiperAdapter: not ready");
        float dummy = 0.0f;
        on_chunk(&dummy, 0, k_piper_sample_rate, true);
        return;
    }

    m_impl->stop_requested.store(false, std::memory_order_release);

    std::string text_copy(text);
    m_impl->speech_thread = std::thread([this, text_copy,
                                         cb = std::move(on_chunk)]() mutable {
        m_impl->process.synthesize(text_copy,
            [&cb](const float* pcm, size_t count, bool is_last) {
                cb(pcm, count, k_piper_sample_rate, is_last);
            },
            m_impl->stop_requested);
    });
}

void PiperAdapter::stop() {
    m_impl->stop_requested.store(true, std::memory_order_release);
    if (m_impl->speech_thread.joinable()) {
        m_impl->speech_thread.join();
    }
}

std::string PiperAdapter::defaultModelPath() const {
    auto* d = ::getenv("XDG_DATA_HOME");
    std::string base = d ? std::string(d) : std::string(::getenv("HOME")) + "/.local/share";
    return base + "/rook/models/piper/de_DE-thorsten-medium.onnx";
}

std::string PiperAdapter::defaultModelUrl() const {
    return "https://huggingface.co/rhasspy/piper-voices/resolve/main/de/de_DE/thorsten/medium/de_DE-thorsten-medium.onnx";
}

void PiperAdapter::downloadModel(ProgressFn on_progress, DoneFn on_done) {
    auto path = defaultModelPath();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    auto json_path = path + ".json";
    auto json_url = defaultModelUrl() + ".json";

    downloadFile(defaultModelUrl(), path,
        [on_progress](float p) { if (on_progress) on_progress(p * 0.8f); },
        [this, path, json_path, json_url, on_progress, on_done](bool success) {
            if (!success) { if (on_done) on_done(false); return; }

            downloadFile(json_url, json_path,
                [on_progress](float p) { if (on_progress) on_progress(0.8f + p * 0.2f); },
                [this, path, on_done](bool json_success) {
                    if (json_success) m_impl->model_path = path;
                    if (on_done) on_done(json_success);
                });
        });
}

} // namespace rook::adapters::audio
