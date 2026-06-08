#include "rook/adapters/audio/piper_adapter.hpp"

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

} // anonymous namespace

struct PiperAdapter::Impl {
    std::string model_path;
    std::string voice_id;
    std::string binary_path;
    std::atomic<bool> stop_requested{false};
    std::thread speech_thread;
    pid_t child_pid = 0;
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
}

bool PiperAdapter::isReady() const {
    return !m_impl->binary_path.empty() && !m_impl->model_path.empty() &&
           std::filesystem::exists(m_impl->model_path);
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
        on_chunk(&dummy, 0, 22050, true);
        return;
    }

    m_impl->stop_requested.store(false, std::memory_order_release);
    m_impl->child_pid = 0;

    std::string text_copy(text);
    std::string model = m_impl->model_path;

    m_impl->speech_thread = std::thread([this, text_copy, model, cb = std::move(on_chunk)]() mutable {
        int pipe_in[2];
        int pipe_out[2];

        if (::pipe(pipe_in) != 0 || ::pipe(pipe_out) != 0) {
            SPDLOG_ERROR("PiperAdapter: pipe creation failed");
            float dummy = 0.0f;
            cb(&dummy, 0, 22050, true);
            return;
        }

        pid_t pid = ::fork();
        if (pid < 0) {
            SPDLOG_ERROR("PiperAdapter: fork failed");
            ::close(pipe_in[0]); ::close(pipe_in[1]);
            ::close(pipe_out[0]); ::close(pipe_out[1]);
            float dummy = 0.0f;
            cb(&dummy, 0, 22050, true);
            return;
        }

        if (pid == 0) {
            ::close(pipe_in[1]);
            ::close(pipe_out[0]);
            ::dup2(pipe_in[0], STDIN_FILENO);
            ::dup2(pipe_out[1], STDOUT_FILENO);
            ::close(pipe_in[0]);
            ::close(pipe_out[1]);

            ::execlp(m_impl->binary_path.c_str(),
                     "piper",
                     "--model", model.c_str(),
                     "--output-raw",
                     nullptr);
            _exit(127);
        }

        ::close(pipe_in[0]);
        ::close(pipe_out[1]);

        m_impl->child_pid = pid;

        auto written = ::write(pipe_in[1], text_copy.data(), text_copy.size());
        (void)written;
        ::close(pipe_in[1]);

        int sample_rate = 22050;
        std::array<int16_t, 2048> raw_buf;
        std::array<float, 2048> float_buf;
        ssize_t n;

        while ((n = ::read(pipe_out[0], raw_buf.data(), raw_buf.size() * sizeof(int16_t))) > 0) {
            if (m_impl->stop_requested.load(std::memory_order_acquire)) {
                ::kill(pid, SIGTERM);
                break;
            }
            std::size_t samples = static_cast<std::size_t>(n) / sizeof(int16_t);
            for (std::size_t i = 0; i < samples; ++i)
                float_buf[i] = static_cast<float>(raw_buf[i]) / 32768.0f;
            cb(float_buf.data(), samples, sample_rate, false);
        }

        ::close(pipe_out[0]);

        int status = 0;
        ::waitpid(pid, &status, 0);
        m_impl->child_pid = 0;

        if (!m_impl->stop_requested.load(std::memory_order_acquire)) {
            float dummy = 0.0f;
            cb(&dummy, 0, sample_rate, true);
        }
    });
}

void PiperAdapter::stop() {
    m_impl->stop_requested.store(true, std::memory_order_release);
    if (m_impl->child_pid > 0) {
        ::kill(m_impl->child_pid, SIGTERM);
        m_impl->child_pid = 0;
    }
    if (m_impl->speech_thread.joinable()) {
        m_impl->speech_thread.join();
    }
}

} // namespace rook::adapters::audio
