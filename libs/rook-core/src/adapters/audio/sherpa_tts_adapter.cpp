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

static bool is_emoji(uint32_t cp) {
    return (cp >= 0x1F600 && cp <= 0x1F64F)
        || (cp >= 0x1F300 && cp <= 0x1F5FF)
        || (cp >= 0x1F680 && cp <= 0x1F6FF)
        || (cp >= 0x1F1E0 && cp <= 0x1F1FF)
        || (cp >= 0x2600 && cp <= 0x27BF)
        || (cp >= 0x1F900 && cp <= 0x1F9FF)
        || (cp >= 0x1FA00 && cp <= 0x1FA6F)
        || (cp >= 0x1FA70 && cp <= 0x1FAFF)
        || cp == 0xFE0F || cp == 0x200D || cp == 0x20E3;
}

static std::string strip_emojis(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size();) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        uint32_t cp;
        int len;
        if ((c & 0x80u) == 0) {
            cp = c; len = 1;
        } else if ((c & 0xE0u) == 0xC0) {
            cp = c & 0x1F; len = 2;
        } else if ((c & 0xF0u) == 0xE0) {
            cp = c & 0x0F; len = 3;
        } else {
            cp = c & 0x07; len = 4;
        }
        if (i + static_cast<std::size_t>(len) > input.size()) break;
        for (int j = 1; j < len; ++j)
            cp = (cp << 6) | (static_cast<unsigned char>(input[i + j]) & 0x3F);
        if (!is_emoji(cp))
            out.append(input.data() + i, static_cast<std::size_t>(len));
        i += static_cast<std::size_t>(len);
    }
    return out;
}

std::string find_espeak_data() {
    auto try_dir = [](const std::filesystem::path& p) -> std::string {
        auto canonical = std::filesystem::weakly_canonical(p);
        if (!std::filesystem::exists(canonical)) return {};
        if (std::filesystem::exists(canonical / "phontab"))
            return canonical;
        if (std::filesystem::exists(canonical / "espeak-ng-data" / "phontab"))
            return canonical / "espeak-ng-data";
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

    using ChunkFn = std::function<void(const float*, std::size_t, int, bool)>;
    ChunkFn user_chunk_cb;

    static int32_t onTtsChunk(const float* samples, int32_t n, float,
                              void* arg)
    {
        auto* self = static_cast<Impl*>(arg);
        if (self->cancel_requested.load(std::memory_order_acquire))
            return 0;
        if (self->user_chunk_cb)
            self->user_chunk_cb(samples, static_cast<std::size_t>(n),
                                22050, false);
        return 1;
    }
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
    m_impl->user_chunk_cb = std::move(on_chunk);

    std::string clean_text = strip_emojis(text);
    const auto* tts = m_impl->tts;

    auto* gs = g_settings_new("io.github.fleischerdesign.Rook");
    float speed = static_cast<float>(g_settings_get_double(gs, "tts-speed"));
    g_object_unref(gs);

    m_impl->speech_thread = std::thread([this,
                                          text = std::move(clean_text),
                                          tts, speed]() {
        SherpaOnnxGenerationConfig gen_cfg;
        std::memset(&gen_cfg, 0, sizeof(gen_cfg));
        gen_cfg.sid = 0;
        gen_cfg.speed = speed;
        gen_cfg.silence_scale = 0.2f;

        const SherpaOnnxGeneratedAudio* audio =
            SherpaOnnxOfflineTtsGenerateWithConfig(tts, text.c_str(),
                                                    &gen_cfg,
                                                    Impl::onTtsChunk,
                                                    m_impl.get());

        if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

        if (m_impl->cancel_requested.load(std::memory_order_acquire))
            return;

        if (m_impl->user_chunk_cb) {
            float dummy = 0.0f;
            m_impl->user_chunk_cb(&dummy, 0, 22050, true);
        }
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
