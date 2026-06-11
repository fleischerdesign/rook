#include "rook/adapters/audio/sherpa_asr_adapter.hpp"
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
#include <thread>
#include <unordered_map>
#include <vector>

namespace rook::adapters::audio {

namespace {

enum class AsrBackend { Whisper, Transducer };

AsrBackend parseBackend(std::string_view s) {
    if (s == "transducer") return AsrBackend::Transducer;
    return AsrBackend::Whisper;
}

const char* backendString(AsrBackend b) {
    return b == AsrBackend::Transducer ? "transducer" : "whisper";
}

struct RegistryEntry {
    AsrBackend backend;
    std::string model_id;
    std::string display_name;
    std::string archive_url;
    bool supports_language;
    int64_t size_mb;
};

const std::vector<RegistryEntry> k_registry = {
    {AsrBackend::Whisper, "tiny", "Whisper Tiny (116 MB, schnell)",
     "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-tiny.tar.bz2",
     true, 116},
    {AsrBackend::Whisper, "base", "Whisper Base (208 MB)",
     "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-base.tar.bz2",
     true, 208},
    {AsrBackend::Whisper, "small", "Whisper Small (639 MB)",
     "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-small.tar.bz2",
     true, 639},
    {AsrBackend::Whisper, "medium", "Whisper Medium (1.9 GB)",
     "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-medium.tar.bz2",
     true, 1930},
    {AsrBackend::Whisper, "large-v3", "Whisper Large v3 (1.1 GB)",
     "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-large-v3.tar.bz2",
     true, 1070},
    {AsrBackend::Transducer, "parakeet-tdt-0.6b", "Parakeet TDT 0.6B (25 Sprachen, hohe Genauigkeit)",
     "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8.tar.bz2",
     false, 600},
};

const RegistryEntry* lookupRegistry(AsrBackend backend, std::string_view model_id) {
    for (auto& e : k_registry)
        if (e.backend == backend && e.model_id == model_id)
            return &e;
    return nullptr;
}

struct ResolvedPaths {
    std::string encoder, decoder, joiner, tokens, model;
};

ResolvedPaths discoverFiles(const std::string& dir) {
    ResolvedPaths p;
    if (!std::filesystem::exists(dir)) return p;
    for (auto& e : std::filesystem::directory_iterator(dir)) {
        auto n = e.path().filename().string();
        if (n.find("encoder") != std::string::npos && n.ends_with(".onnx"))
            p.encoder = e.path().string();
        if (n.find("decoder") != std::string::npos && n.ends_with(".onnx"))
            p.decoder = e.path().string();
        if (n.find("joiner") != std::string::npos && n.ends_with(".onnx"))
            p.joiner = e.path().string();
        if (n.find("tokens") != std::string::npos && n.ends_with(".txt"))
            p.tokens = e.path().string();
        if (n.ends_with(".onnx") && p.model.empty())
            p.model = e.path().string();
    }
    return p;
}

SherpaOnnxOfflineRecognizerConfig buildConfig(
    AsrBackend backend, const ResolvedPaths& p, int threads,
    std::string_view language)
{
    SherpaOnnxOfflineRecognizerConfig c;
    std::memset(&c, 0, sizeof(c));
    c.feat_config.sample_rate = 16000;
    c.feat_config.feature_dim = 80;
    c.decoding_method = "greedy_search";
    c.model_config.tokens = p.tokens.c_str();
    c.model_config.num_threads = threads;
    c.model_config.provider = "cpu";

    switch (backend) {
    case AsrBackend::Whisper:
        c.model_config.whisper.encoder = p.encoder.c_str();
        c.model_config.whisper.decoder = p.decoder.c_str();
        c.model_config.whisper.language = std::string(language).c_str();
        c.model_config.whisper.task = "transcribe";
        break;
    case AsrBackend::Transducer:
        c.model_config.transducer.encoder = p.encoder.c_str();
        c.model_config.transducer.decoder = p.decoder.c_str();
        c.model_config.transducer.joiner = p.joiner.c_str();
        c.model_config.model_type = "nemo_transducer";
        break;
    }
    return c;
}

} // anonymous namespace

struct SherpaAsrAdapter::Impl {
    AsrBackend asr_backend = AsrBackend::Whisper;
    std::string model_id = "tiny";
    std::string model_dir;
    std::string language = "de";

    const SherpaOnnxOfflineRecognizer* recognizer = nullptr;

    std::atomic<bool> cancel_requested{false};
    std::thread transcribe_thread;
    bool ready = false;

    void initRecognizer();
    void readSettings();
};

void SherpaAsrAdapter::Impl::readSettings() {
    auto* gs = g_settings_new("io.github.fleischerdesign.Rook");
    char* backend_str = g_settings_get_string(gs, "asr-backend");
    char* model_str   = g_settings_get_string(gs, "asr-model");
    char* lang_str    = g_settings_get_string(gs, "asr-language");
    g_object_unref(gs);

    asr_backend = parseBackend(backend_str);
    model_id = model_str && model_str[0] ? model_str : "tiny";
    language = lang_str && lang_str[0] ? lang_str : "de";

    g_free(backend_str); g_free(model_str); g_free(lang_str);
    model_dir = SherpaAsrAdapter::defaultModelDir(
        backendString(asr_backend), model_id);
}

void SherpaAsrAdapter::Impl::initRecognizer() {
    if (recognizer) {
        SherpaOnnxDestroyOfflineRecognizer(recognizer);
        recognizer = nullptr;
    }
    ready = false;

    readSettings();

    auto paths = discoverFiles(model_dir);

    if (paths.tokens.empty()) {
        SPDLOG_ERROR("SherpaAsrAdapter: tokens.txt not found in {}",
                     model_dir);
        return;
    }

    if (asr_backend == AsrBackend::Whisper) {
        if (paths.encoder.empty() || paths.decoder.empty()) {
            SPDLOG_ERROR("SherpaAsrAdapter: encoder/decoder not found in {}",
                         model_dir);
            return;
        }
    } else if (asr_backend == AsrBackend::Transducer) {
        if (paths.encoder.empty() || paths.decoder.empty() ||
            paths.joiner.empty()) {
            SPDLOG_ERROR("SherpaAsrAdapter: encoder/decoder/joiner not found "
                         "in {}", model_dir);
            return;
        }
    }

    auto* gs = g_settings_new("io.github.fleischerdesign.Rook");
    int threads = g_settings_get_int(gs, "asr-threads");
    g_object_unref(gs);

    auto config = buildConfig(asr_backend, paths, threads, language);
    recognizer = SherpaOnnxCreateOfflineRecognizer(&config);
    if (!recognizer) {
        SPDLOG_ERROR("SherpaAsrAdapter: failed to create OfflineRecognizer");
        return;
    }

    ready = true;
    SPDLOG_INFO("SherpaAsrAdapter: engine ready (backend={}, model={})",
                backendString(asr_backend), model_id);
}

SherpaAsrAdapter::SherpaAsrAdapter(std::string model_path)
    : m_impl(std::make_unique<Impl>())
{
    if (!model_path.empty()) {
        m_impl->model_dir = model_path;
    } else {
        m_impl->readSettings();
    }
    m_impl->initRecognizer();
}

SherpaAsrAdapter::~SherpaAsrAdapter() {
    cancel();
    if (m_impl->recognizer)
        SherpaOnnxDestroyOfflineRecognizer(m_impl->recognizer);
}

bool SherpaAsrAdapter::isReady() const {
    return m_impl->ready;
}

void SherpaAsrAdapter::transcribe(
    const int16_t* audio, std::size_t sample_count,
    int sample_rate,
    std::function<void(ports::SttResult)> on_result)
{
    if (!m_impl->ready || !m_impl->recognizer) {
        on_result({"", false});
        return;
    }

    m_impl->cancel_requested.store(false, std::memory_order_release);

    std::vector<float> f32(sample_count);
    for (std::size_t i = 0; i < sample_count; ++i)
        f32[i] = static_cast<float>(audio[i]) / 32768.0f;

    const auto* recognizer = m_impl->recognizer;

    m_impl->transcribe_thread = std::thread(
        [this, f32 = std::move(f32), sample_rate, recognizer,
         cb = std::move(on_result)]()
    {
        const SherpaOnnxOfflineStream* stream =
            SherpaOnnxCreateOfflineStream(recognizer);
        SherpaOnnxAcceptWaveformOffline(
            stream, sample_rate, f32.data(),
            static_cast<int32_t>(f32.size()));

        if (m_impl->cancel_requested.load(std::memory_order_acquire)) {
            SherpaOnnxDestroyOfflineStream(stream);
            cb({"", false});
            return;
        }

        SherpaOnnxDecodeOfflineStream(recognizer, stream);

        if (m_impl->cancel_requested.load(std::memory_order_acquire)) {
            SherpaOnnxDestroyOfflineStream(stream);
            cb({"", false});
            return;
        }

        const SherpaOnnxOfflineRecognizerResult* result =
            SherpaOnnxGetOfflineStreamResult(stream);

        std::string clean = result->text ? std::string(result->text) : "";
        while (!clean.empty() &&
               (clean.back() == '\n' || clean.back() == ' '))
            clean.pop_back();
        while (!clean.empty() && clean.front() == ' ')
            clean.erase(0, 1);

        SherpaOnnxDestroyOfflineRecognizerResult(result);
        SherpaOnnxDestroyOfflineStream(stream);

        if (clean.empty()) {
            cb({"", false});
            return;
        }

        cb({std::move(clean), true});
    });
}

void SherpaAsrAdapter::cancel() {
    m_impl->cancel_requested.store(true, std::memory_order_release);
    if (m_impl->transcribe_thread.joinable())
        m_impl->transcribe_thread.join();
}

void SherpaAsrAdapter::setModel(std::string_view path) {
    m_impl->model_dir = path;
    m_impl->initRecognizer();
}

std::vector<std::string> SherpaAsrAdapter::availableModels() const {
    std::vector<std::string> models;
    for (auto& e : k_registry)
        if (e.backend == m_impl->asr_backend)
            models.push_back(e.model_id);
    return models;
}

void SherpaAsrAdapter::setBackend(std::string_view backend) {
    auto* gs = g_settings_new("io.github.fleischerdesign.Rook");
    g_settings_set_string(gs, "asr-backend", std::string(backend).c_str());
    g_object_unref(gs);

    auto b = parseBackend(backend);
    const RegistryEntry* best = nullptr;
    for (auto& e : k_registry) {
        if (e.backend == b && (!best || e.size_mb < best->size_mb))
            best = &e;
    }
    if (best) {
        gs = g_settings_new("io.github.fleischerdesign.Rook");
        g_settings_set_string(gs, "asr-model", best->model_id.c_str());
        g_object_unref(gs);
    }
    m_impl->initRecognizer();
}

void SherpaAsrAdapter::setLanguage(std::string_view language) {
    auto* gs = g_settings_new("io.github.fleischerdesign.Rook");
    g_settings_set_string(gs, "asr-language", std::string(language).c_str());
    g_object_unref(gs);
    m_impl->language = language;
    m_impl->initRecognizer();
}

std::string SherpaAsrAdapter::backend() const {
    return backendString(m_impl->asr_backend);
}

std::vector<SherpaAsrAdapter::ModelInfo> SherpaAsrAdapter::modelsForBackend(
    std::string_view backend) const
{
    std::vector<ModelInfo> models;
    auto b = parseBackend(backend);
    for (auto& e : k_registry) {
        if (e.backend == b)
            models.push_back({e.model_id, e.display_name, e.size_mb});
    }
    return models;
}

std::vector<std::string> SherpaAsrAdapter::availableBackends() const {
    return {"whisper", "transducer"};
}

std::string SherpaAsrAdapter::defaultModelDir(
    std::string_view backend, std::string_view model_id)
{
    auto* d = ::getenv("XDG_DATA_HOME");
    std::string base = d ? std::string(d)
                         : std::string(::getenv("HOME")) + "/.local/share";
    return base + "/rook/models/sherpa-asr/"
           + std::string(backend) + "/" + std::string(model_id);
}

std::string SherpaAsrAdapter::defaultModelPath() {
    auto* gs = g_settings_new("io.github.fleischerdesign.Rook");
    char* backend_str = g_settings_get_string(gs, "asr-backend");
    char* model_str   = g_settings_get_string(gs, "asr-model");
    g_object_unref(gs);
    std::string b = backend_str && backend_str[0] ? backend_str : "whisper";
    std::string m = model_str && model_str[0] ? model_str : "tiny";
    g_free(backend_str); g_free(model_str);
    return defaultModelDir(b, m);
}

void SherpaAsrAdapter::downloadModel(ProgressFn on_progress,
                                      DoneFn on_done)
{
    auto b = m_impl->asr_backend;
    auto* entry = lookupRegistry(b, m_impl->model_id);
    if (!entry) {
        SPDLOG_ERROR("SherpaAsrAdapter: no registry entry for {}/{}",
                     backendString(b), m_impl->model_id);
        if (on_done) on_done(false);
        return;
    }

    auto dir = m_impl->model_dir;
    std::filesystem::create_directories(std::filesystem::path(dir).parent_path());
    std::string archive = dir + ".tar.bz2";

    downloadFile(entry->archive_url, archive,
        [on_progress](float p) {
            if (on_progress) on_progress(p * 0.8f);
        },
        [this, dir, archive, on_done](bool ok) {
            if (!ok) { if (on_done) on_done(false); return; }
            std::string cmd = "tar xf " + archive + " --strip-components=1 -C " +
                std::filesystem::path(dir).parent_path().string();
            int ret = std::system(cmd.c_str());
            std::filesystem::remove(archive);
            if (ret == 0) m_impl->initRecognizer();
            if (on_done) on_done(ret == 0);
        });
}

} // namespace rook::adapters::audio
