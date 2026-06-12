#include "rook/adapters/audio/sherpa_wakeword_adapter.hpp"
#include "rook/adapters/audio/model_downloader.hpp"

#include <spdlog/spdlog.h>
#include <gio/gio.h>
#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

namespace rook::adapters::audio {

namespace {

constexpr int k_frame_size = 512;
constexpr int k_min_speech_frames = 12;
constexpr int k_min_silence_frames = 8;
constexpr int k_cooldown_frames = 25;
constexpr int k_vad_sample_rate = 16000;

} // anonymous namespace

struct SherpaWakewordAdapter::Impl {
    float sensitivity = 0.5f;

    const SherpaOnnxVoiceActivityDetector* vad = nullptr;

    std::vector<float> conversion_buffer;

    int speech_counter = 0;
    int silence_counter = 0;
    int cooldown = 0;

    bool vad_loaded = false;

    static float vadThreshold(float sensitivity) {
        return 0.7f - sensitivity * 0.4f;
    }

    void reloadVad() {
        if (vad) {
            SherpaOnnxDestroyVoiceActivityDetector(vad);
            vad = nullptr;
        }
        vad_loaded = false;

        auto path = SherpaWakewordAdapter::defaultModelPath();
        if (!std::filesystem::exists(path)) return;

        SherpaOnnxVadModelConfig cfg;
        std::memset(&cfg, 0, sizeof(cfg));
        cfg.silero_vad.model = path.c_str();
        cfg.silero_vad.threshold = vadThreshold(sensitivity);
        cfg.silero_vad.min_silence_duration = 0.25f;
        cfg.silero_vad.min_speech_duration = 0.25f;
        cfg.silero_vad.max_speech_duration = 5.0f;
        cfg.silero_vad.window_size = k_frame_size;
        cfg.sample_rate = k_vad_sample_rate;
        cfg.num_threads = 1;
        cfg.provider = "cpu";

        const auto* v = SherpaOnnxCreateVoiceActivityDetector(&cfg, 30.0f);
        if (v) {
            vad = v;
            vad_loaded = true;
        }
    }
};

SherpaWakewordAdapter::SherpaWakewordAdapter()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->conversion_buffer.resize(k_frame_size);

    auto* gs = g_settings_new("io.github.fleischerdesign.Rook");
    m_impl->sensitivity = static_cast<float>(
        g_settings_get_double(gs, "wakeword-sensitivity"));
    g_object_unref(gs);

    m_impl->reloadVad();
    if (!m_impl->vad_loaded)
        SPDLOG_INFO("SherpaWakewordAdapter: no VAD model, using energy fallback");
}

SherpaWakewordAdapter::~SherpaWakewordAdapter() {
    if (m_impl->vad)
        SherpaOnnxDestroyVoiceActivityDetector(m_impl->vad);
}

bool SherpaWakewordAdapter::processFrame(const int16_t* pcm) {
    if (m_impl->cooldown > 0) {
        m_impl->cooldown--;
        return false;
    }

    bool speech = false;

    if (m_impl->vad_loaded) {
        for (int i = 0; i < k_frame_size; ++i)
            m_impl->conversion_buffer[i] =
                static_cast<float>(pcm[i]) / 32768.0f;
        SherpaOnnxVoiceActivityDetectorAcceptWaveform(
            m_impl->vad, m_impl->conversion_buffer.data(), k_frame_size);
        speech = SherpaOnnxVoiceActivityDetectorDetected(m_impl->vad) != 0;
    } else {
        double sum = 0.0;
        for (int i = 0; i < k_frame_size; ++i)
            sum += static_cast<double>(pcm[i]) * static_cast<double>(pcm[i]);
        float rms = static_cast<float>(
            std::sqrt(sum / static_cast<double>(k_frame_size)));
        float threshold = 800.0f + (1.0f - m_impl->sensitivity) * 3000.0f;
        speech = rms > threshold;
    }

    if (speech) {
        m_impl->speech_counter++;
        m_impl->silence_counter = 0;
    } else if (m_impl->speech_counter > 0) {
        m_impl->silence_counter++;
    }

    if (m_impl->speech_counter > k_min_speech_frames &&
        m_impl->silence_counter > k_min_silence_frames) {
        SPDLOG_DEBUG("SherpaWakewordAdapter: wakeword triggered");
        reset();
        return true;
    }

    if (m_impl->silence_counter > k_min_speech_frames * 3) {
        reset();
    }

    return false;
}

void SherpaWakewordAdapter::reset() {
    m_impl->speech_counter = 0;
    m_impl->silence_counter = 0;
    m_impl->cooldown = k_cooldown_frames;
    if (m_impl->vad_loaded)
        SherpaOnnxVoiceActivityDetectorReset(m_impl->vad);
}

void SherpaWakewordAdapter::setSensitivity(float value) {
    m_impl->sensitivity = std::clamp(value, 0.0f, 1.0f);
    auto* gs = g_settings_new("io.github.fleischerdesign.Rook");
    g_settings_set_double(gs, "wakeword-sensitivity",
                          static_cast<double>(m_impl->sensitivity));
    g_object_unref(gs);

    m_impl->reloadVad();
}

std::string SherpaWakewordAdapter::defaultModelPath() {
    auto* d = ::getenv("XDG_DATA_HOME");
    std::string base = d ? std::string(d)
                          : std::string(::getenv("HOME")) + "/.local/share";
    return base + "/rook/models/sherpa-vad/silero_vad.onnx";
}

std::string SherpaWakewordAdapter::defaultModelUrl() {
    return "https://github.com/k2-fsa/sherpa-onnx/releases/download/"
           "asr-models/sherpa-onnx-silero-vad-v5.0.0.tar.bz2";
}

void SherpaWakewordAdapter::downloadModel(ProgressFn on_progress,
                                          DoneFn on_done)
{
    auto url = defaultModelUrl();
    auto path = defaultModelPath();
    auto dir = std::filesystem::path(path).parent_path();
    std::filesystem::create_directories(dir);
    std::string archive = dir.string() + "/silero_vad.tar.bz2";

    downloadFile(url, archive,
        [on_progress](float p) {
            if (on_progress) on_progress(p * 0.8f);
        },
        [this, dir, archive, path, on_done](bool ok) {
            if (!ok) { if (on_done) on_done(false); return; }
            std::string cmd = "tar xf " + archive
                              + " --strip-components=1 -C "
                              + dir.string();
            int ret = std::system(cmd.c_str());
            std::filesystem::remove(archive);
            if (ret == 0 && std::filesystem::exists(path)) {
                m_impl->reloadVad();
                if (m_impl->vad_loaded)
                    SPDLOG_INFO("SherpaWakewordAdapter: VAD loaded "
                                "after download");
            }
            if (on_done) on_done(ret == 0);
        });
}

} // namespace rook::adapters::audio
