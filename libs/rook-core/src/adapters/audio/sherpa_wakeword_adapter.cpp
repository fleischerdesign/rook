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

constexpr const char* k_kws_keyword_tokens = "▁HE Y ▁RO O K";

} // anonymous namespace

struct SherpaWakewordAdapter::Impl {
    float sensitivity = 0.5f;

    const SherpaOnnxVoiceActivityDetector* vad = nullptr;
    bool vad_loaded = false;

    const SherpaOnnxKeywordSpotter* kws = nullptr;
    const SherpaOnnxOnlineStream* kws_stream = nullptr;
    bool kws_loaded = false;

    std::vector<float> conversion_buffer;

    int speech_counter = 0;
    int silence_counter = 0;
    int cooldown = 0;

    static float vadThreshold(float sensitivity) {
        return 0.3f + sensitivity * 0.4f;
    }

    static float kwsThreshold(float sensitivity) {
        return 0.02f + sensitivity * 0.4f;
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

    void reloadKws() {
        if (kws_stream) {
            SherpaOnnxDestroyOnlineStream(kws_stream);
            kws_stream = nullptr;
        }
        if (kws) {
            SherpaOnnxDestroyKeywordSpotter(kws);
            kws = nullptr;
        }
        kws_loaded = false;

        auto dir = SherpaWakewordAdapter::defaultKwsModelDir();
        if (!std::filesystem::exists(dir)) return;

        auto find = [&](std::string_view suffix) -> std::string {
            for (auto& e : std::filesystem::directory_iterator(dir)) {
                auto n = e.path().filename().string();
                if (n.ends_with(suffix))
                    return e.path().string();
            }
            return {};
        };

        auto encoder = find("encoder-epoch-12-avg-2-chunk-16-left-64.onnx");
        auto decoder = find("decoder-epoch-12-avg-2-chunk-16-left-64.onnx");
        auto joiner = find("joiner-epoch-12-avg-2-chunk-16-left-64.onnx");
        auto tokens = find("tokens.txt");

        if (encoder.empty() || decoder.empty() ||
            joiner.empty() || tokens.empty()) {
            SPDLOG_WARN("SherpaWakewordAdapter: KWS model files incomplete");
            return;
        }

        SherpaOnnxKeywordSpotterConfig cfg;
        std::memset(&cfg, 0, sizeof(cfg));
        cfg.feat_config.sample_rate = 16000;
        cfg.feat_config.feature_dim = 80;
        cfg.model_config.transducer.encoder = encoder.c_str();
        cfg.model_config.transducer.decoder = decoder.c_str();
        cfg.model_config.transducer.joiner = joiner.c_str();
        cfg.model_config.tokens = tokens.c_str();
        cfg.model_config.num_threads = 1;
        cfg.model_config.provider = "cpu";
        cfg.max_active_paths = 4;
        cfg.num_trailing_blanks = 1;
        cfg.keywords_score = 1.5f;
        cfg.keywords_threshold = kwsThreshold(sensitivity);
        cfg.keywords_buf = k_kws_keyword_tokens;
        cfg.keywords_buf_size =
            static_cast<int32_t>(std::strlen(k_kws_keyword_tokens));

        kws = SherpaOnnxCreateKeywordSpotter(&cfg);
        if (!kws) {
            SPDLOG_WARN("SherpaWakewordAdapter: failed to create KWS");
            return;
        }

        kws_stream =
            SherpaOnnxCreateKeywordStreamWithKeywords(
                kws, k_kws_keyword_tokens);
        if (!kws_stream) {
            SPDLOG_WARN("SherpaWakewordAdapter: failed to create KWS stream");
            SherpaOnnxDestroyKeywordSpotter(kws);
            kws = nullptr;
            return;
        }

        kws_loaded = true;
        SPDLOG_INFO("SherpaWakewordAdapter: KWS ready — 'HEY ROOK'");
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

    m_impl->reloadKws();
    m_impl->reloadVad();
    if (!m_impl->kws_loaded && !m_impl->vad_loaded)
        SPDLOG_INFO("SherpaWakewordAdapter: no KWS/VAD model, "
                    "using energy fallback");
}

SherpaWakewordAdapter::~SherpaWakewordAdapter() {
    if (m_impl->kws_stream)
        SherpaOnnxDestroyOnlineStream(m_impl->kws_stream);
    if (m_impl->kws)
        SherpaOnnxDestroyKeywordSpotter(m_impl->kws);
    if (m_impl->vad)
        SherpaOnnxDestroyVoiceActivityDetector(m_impl->vad);
}

bool SherpaWakewordAdapter::processFrame(const int16_t* pcm) {
    if (m_impl->cooldown > 0) {
        m_impl->cooldown--;
        return false;
    }

    for (int i = 0; i < k_frame_size; ++i)
        m_impl->conversion_buffer[i] =
            static_cast<float>(pcm[i]) / 32768.0f;

    if (m_impl->kws_loaded) {
        SherpaOnnxOnlineStreamAcceptWaveform(
            m_impl->kws_stream, k_vad_sample_rate,
            m_impl->conversion_buffer.data(), k_frame_size);

        while (SherpaOnnxIsKeywordStreamReady(m_impl->kws,
                                               m_impl->kws_stream)) {
            SherpaOnnxDecodeKeywordStream(m_impl->kws,
                                          m_impl->kws_stream);
        }

        const auto* r = SherpaOnnxGetKeywordResult(
            m_impl->kws, m_impl->kws_stream);
        if (r && r->keyword && r->keyword[0] != '\0') {
            SPDLOG_DEBUG("SherpaWakewordAdapter: KWS detected '{}'",
                         r->keyword);
            SherpaOnnxDestroyKeywordResult(r);
            reset();
            return true;
        }
        if (r) SherpaOnnxDestroyKeywordResult(r);
        return false;
    }

    if (m_impl->vad_loaded) {
        SherpaOnnxVoiceActivityDetectorAcceptWaveform(
            m_impl->vad, m_impl->conversion_buffer.data(), k_frame_size);
        auto speech = SherpaOnnxVoiceActivityDetectorDetected(
            m_impl->vad) != 0;

        if (speech) {
            m_impl->speech_counter++;
            m_impl->silence_counter = 0;
        } else if (m_impl->speech_counter > 0) {
            m_impl->silence_counter++;
        }

        if (m_impl->speech_counter > k_min_speech_frames &&
            m_impl->silence_counter > k_min_silence_frames) {
            SPDLOG_DEBUG("SherpaWakewordAdapter: VAD wakeword triggered");
            reset();
            return true;
        }

        if (m_impl->silence_counter > k_min_speech_frames * 3)
            reset();
        return false;
    }

    double sum = 0.0;
    for (int i = 0; i < k_frame_size; ++i)
        sum += static_cast<double>(pcm[i]) * static_cast<double>(pcm[i]);
    float rms = static_cast<float>(
        std::sqrt(sum / static_cast<double>(k_frame_size)));
    float threshold = 800.0f + m_impl->sensitivity * 3000.0f;
    bool speech = rms > threshold;

    if (speech) {
        m_impl->speech_counter++;
        m_impl->silence_counter = 0;
    } else if (m_impl->speech_counter > 0) {
        m_impl->silence_counter++;
    }

    if (m_impl->speech_counter > k_min_speech_frames &&
        m_impl->silence_counter > k_min_silence_frames) {
        SPDLOG_DEBUG("SherpaWakewordAdapter: RMS wakeword triggered");
        reset();
        return true;
    }

    if (m_impl->silence_counter > k_min_speech_frames * 3)
        reset();

    return false;
}

void SherpaWakewordAdapter::reset() {
    m_impl->speech_counter = 0;
    m_impl->silence_counter = 0;
    m_impl->cooldown = k_cooldown_frames;
    if (m_impl->kws_loaded && m_impl->kws_stream)
        SherpaOnnxResetKeywordStream(m_impl->kws, m_impl->kws_stream);
    if (m_impl->vad_loaded)
        SherpaOnnxVoiceActivityDetectorReset(m_impl->vad);
}

bool SherpaWakewordAdapter::hasKws() const {
    return m_impl->kws_loaded;
}

void SherpaWakewordAdapter::setSensitivity(float value) {
    m_impl->sensitivity = std::clamp(value, 0.0f, 1.0f);

    m_impl->reloadVad();
    m_impl->reloadKws();
}

std::string SherpaWakewordAdapter::defaultModelPath() {
    auto* d = ::getenv("XDG_DATA_HOME");
    std::string base = d ? std::string(d)
                          : std::string(::getenv("HOME")) + "/.local/share";
    return base + "/rook/models/sherpa-vad/silero_vad.onnx";
}

std::string SherpaWakewordAdapter::defaultModelUrl() {
    return "https://github.com/k2-fsa/sherpa-onnx/releases/download/"
           "asr-models/silero_vad.onnx";
}

void SherpaWakewordAdapter::downloadModel(ProgressFn on_progress,
                                          DoneFn on_done)
{
    auto url = defaultModelUrl();
    auto path = defaultModelPath();
    auto dir = std::filesystem::path(path).parent_path();
    std::filesystem::create_directories(dir);

    downloadFile(url, path, on_progress,
        [this, path, on_done](bool ok) {
            if (!ok) { if (on_done) on_done(false); return; }
            m_impl->reloadVad();
            if (m_impl->vad_loaded)
                SPDLOG_INFO("SherpaWakewordAdapter: VAD loaded "
                            "after download");
            if (on_done) on_done(m_impl->vad_loaded);
        });
}

std::string SherpaWakewordAdapter::defaultKwsModelDir() {
    auto* d = ::getenv("XDG_DATA_HOME");
    std::string base = d ? std::string(d)
                          : std::string(::getenv("HOME")) + "/.local/share";
    return base + "/rook/models/sherpa-kws/gigaspeech-3.3M";
}

std::string SherpaWakewordAdapter::defaultKwsModelUrl() {
    return "https://github.com/k2-fsa/sherpa-onnx/releases/download/"
           "kws-models/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01"
           ".tar.bz2";
}

void SherpaWakewordAdapter::downloadKwsModel(ProgressFn on_progress,
                                              DoneFn on_done)
{
    auto url = defaultKwsModelUrl();
    auto dir = defaultKwsModelDir();
    std::filesystem::create_directories(dir);
    std::string archive = dir + ".tar.bz2";

    downloadFile(url, archive,
        [on_progress](float p) {
            if (on_progress) on_progress(p * 0.8f);
        },
        [this, dir, archive, on_done](bool ok) {
            if (!ok) { if (on_done) on_done(false); return; }
            std::string cmd = "tar xf " + archive
                              + " --strip-components=1 -C " + dir;
            int ret = std::system(cmd.c_str());
            std::filesystem::remove(archive);
            if (ret == 0) m_impl->reloadKws();
            if (m_impl->kws_loaded)
                SPDLOG_INFO("SherpaWakewordAdapter: KWS loaded "
                            "after download");
            if (on_done) on_done(m_impl->kws_loaded);
        });
}

} // namespace rook::adapters::audio
