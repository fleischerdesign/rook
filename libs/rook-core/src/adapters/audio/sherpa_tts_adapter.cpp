#include "rook/adapters/audio/sherpa_tts_adapter.hpp"
#include "rook/adapters/audio/model_downloader.hpp"

#include <spdlog/spdlog.h>
#include <gio/gio.h>
#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace rook::adapters::audio {

namespace {

std::string find_espeak_data() {
    auto try_dir = [](const std::filesystem::path& p) -> std::string {
        auto canonical = std::filesystem::weakly_canonical(p);
        if (std::filesystem::exists(canonical)) return canonical;
        return {};
    };

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

    for (auto& entry : std::filesystem::directory_iterator("/nix/store")) {
        auto name = entry.path().filename().string();
        if (name.find("sherpa-onnx") != std::string::npos) {
            auto p = entry.path() / "share" / "sherpa-onnx" / "espeak-ng-data";
            auto s = try_dir(p);
            if (!s.empty()) return s;
        }
    }

    return {};
}

} // anonymous namespace

struct SherpaTtsAdapter::Impl {
    std::string model_path;
    std::string tokens_path;
    std::string data_dir;
    std::string voice_id;

    const SherpaOnnxOfflineTts* tts = nullptr;

    std::atomic<bool> cancel_requested{false};
    std::thread speech_thread;
};

SherpaTtsAdapter::SherpaTtsAdapter(std::string model_path)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->model_path = model_path.empty() ? defaultModelPath() : std::move(model_path);

    auto model_dir = std::filesystem::path(m_impl->model_path).parent_path();
    m_impl->tokens_path = (model_dir / "tokens.txt").string();
    m_impl->data_dir = find_espeak_data();

    if (isReady()) {
        SherpaOnnxOfflineTtsConfig config;
        std::memset(&config, 0, sizeof(config));
        config.model.vits.model = m_impl->model_path.c_str();
        config.model.vits.tokens = m_impl->tokens_path.c_str();
        config.model.vits.data_dir = m_impl->data_dir.c_str();
        config.model.num_threads = 1;
        config.model.debug = 0;

        m_impl->tts = SherpaOnnxCreateOfflineTts(&config);
        if (!m_impl->tts) {
            SPDLOG_ERROR("SherpaTtsAdapter: failed to create OfflineTts");
        }
    }
}

SherpaTtsAdapter::~SherpaTtsAdapter() {
    stop();
    if (m_impl->tts) SherpaOnnxDestroyOfflineTts(m_impl->tts);
}

bool SherpaTtsAdapter::isReady() const {
    return !m_impl->model_path.empty() &&
           std::filesystem::exists(m_impl->model_path) &&
           std::filesystem::exists(m_impl->tokens_path) &&
           !m_impl->data_dir.empty();
}

std::vector<ports::VoiceInfo> SherpaTtsAdapter::listVoices() const {
    return {};
}

void SherpaTtsAdapter::setVoice(std::string_view voice_id) {
    m_impl->voice_id = voice_id;
}

void SherpaTtsAdapter::speak(std::string_view text,
                              std::function<void(const float* pcm, std::size_t sample_count,
                                                 int sample_rate, bool is_last)> on_chunk) {
    stop();

    if (!isReady() || !m_impl->tts) {
        SPDLOG_WARN("SherpaTtsAdapter: not ready");
        float dummy = 0.0f;
        on_chunk(&dummy, 0, 22050, true);
        return;
    }

    m_impl->cancel_requested.store(false, std::memory_order_release);

    std::string text_copy(text);
    const auto* tts = m_impl->tts;

    auto* gs = g_settings_new("io.github.fleischerdesign.Rook");
    float speed = static_cast<float>(g_settings_get_double(gs, "tts-speed"));
    float noise_scale = static_cast<float>(
        g_settings_get_double(gs, "tts-noise-scale"));
    g_object_unref(gs);

    m_impl->speech_thread = std::thread([this, text_copy, tts, speed,
                                          noise_scale,
                                          cb = std::move(on_chunk)]() {
        SherpaOnnxGenerationConfig gen_cfg;
        std::memset(&gen_cfg, 0, sizeof(gen_cfg));
        gen_cfg.sid = 0;
        gen_cfg.speed = speed;
        gen_cfg.silence_scale = 0.2f;

        const SherpaOnnxGeneratedAudio* audio =
            SherpaOnnxOfflineTtsGenerateWithConfig(tts, text_copy.c_str(),
                                                    &gen_cfg, nullptr, nullptr);

        if (m_impl->cancel_requested.load(std::memory_order_acquire)) {
            if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
            return;
        }

        if (!audio || audio->n == 0) {
            SPDLOG_ERROR("SherpaTtsAdapter: generation produced no audio");
            float dummy = 0.0f;
            cb(&dummy, 0, 22050, true);
            if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
            return;
        }

        cb(audio->samples, audio->n, audio->sample_rate, false);

        if (m_impl->cancel_requested.load(std::memory_order_acquire)) {
            SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
            return;
        }

        float dummy = 0.0f;
        cb(&dummy, 0, 22050, true);

        SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    });
}

void SherpaTtsAdapter::stop() {
    m_impl->cancel_requested.store(true, std::memory_order_release);
    if (m_impl->speech_thread.joinable()) {
        m_impl->speech_thread.join();
    }
}

std::string SherpaTtsAdapter::defaultModelPath() {
    auto* d = ::getenv("XDG_DATA_HOME");
    std::string base = d ? std::string(d) : std::string(::getenv("HOME")) + "/.local/share";
    return base + "/rook/models/sherpa/vits-piper-de_DE-thorsten-medium/de_DE-thorsten-medium.onnx";
}

std::string SherpaTtsAdapter::defaultModelUrl() {
    return "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-piper-de_DE-thorsten-medium.tar.bz2";
}

void SherpaTtsAdapter::downloadModel(ProgressFn on_progress, DoneFn on_done) {
    auto path = defaultModelPath();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    auto model_url = defaultModelUrl();
    auto data_url = "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/espeak-ng-data.tar.bz2";
    auto data_path = std::filesystem::path(path).parent_path().string() + "/espeak-ng-data";

    downloadFile(model_url, path + ".tar.bz2",
        [on_progress](float p) { if (on_progress) on_progress(p * 0.6f); },
        [path, data_url, data_path, on_progress, on_done](bool model_ok) {
            if (!model_ok) { if (on_done) on_done(false); return; }

            std::string extract_cmd = "tar xf " + path + ".tar.bz2 -C " +
                std::filesystem::path(path).parent_path().string();
            int ret = std::system(extract_cmd.c_str());
            if (ret != 0) {
                SPDLOG_ERROR("SherpaTtsAdapter: failed to extract model archive");
                if (on_done) on_done(false);
                return;
            }
            std::filesystem::remove(path + ".tar.bz2");

            downloadFile(data_url, data_path + ".tar.bz2",
                [on_progress](float p) { if (on_progress) on_progress(0.6f + p * 0.4f); },
                [data_path, on_done](bool data_ok) {
                    if (!data_ok) { if (on_done) on_done(false); return; }

                    std::string extract_cmd = "mkdir -p " + data_path + " && tar xf " +
                        data_path + ".tar.bz2 -C " + data_path;
                    int ret = std::system(extract_cmd.c_str());
                    std::filesystem::remove(data_path + ".tar.bz2");
                    if (on_done) on_done(ret == 0);
                });
        });
}

} // namespace rook::adapters::audio
