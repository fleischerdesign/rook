#include "rook/adapters/audio/openwakeword_adapter.hpp"
#include "rook/adapters/audio/model_downloader.hpp"

#include <spdlog/spdlog.h>
#include <dlfcn.h>
#include <filesystem>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <vector>

#ifdef ROOK_HAS_ONNXRUNTIME
#include <onnxruntime/onnxruntime_c_api.h>
#endif

namespace rook::adapters::audio {

namespace {

constexpr int k_sample_rate = 16000;
constexpr int k_hop_samples = 160;
constexpr int k_mel_win = 76;
constexpr int k_mel_step = 8;
constexpr int k_chunk_size = 1280;
constexpr int k_feature_dim = 96;
constexpr int k_n_mels = 32;
constexpr int k_classifier_frames = 16;
constexpr float k_ema_alpha = 0.3f;
constexpr int k_cooldown_frames = 20;

constexpr std::size_t k_max_raw_buffer = k_sample_rate * 3;
constexpr std::size_t k_max_mel_frames = 290;
constexpr std::size_t k_max_feature_frames = 120;

std::string models_dir() {
    auto* d = ::getenv("XDG_DATA_HOME");
    std::string base = d ? std::string(d) : std::string(::getenv("HOME")) + "/.local/share";
    return base + "/rook/models/wakewords";
}

#ifdef ROOK_HAS_ONNXRUNTIME

void log_ort_error(const OrtApi* ort, OrtStatus* status, const char* context) {
    if (status) {
        SPDLOG_ERROR("OpenWakeWord: {} failed: {}", context, ort->GetErrorMessage(status));
        ort->ReleaseStatus(status);
    }
}

#define ORT_CHECK(call, ctx) do { \
    OrtStatus* _s = (call); \
    if (_s) { log_ort_error(ort, _s, ctx); } \
} while(0)

void release_alloc(OrtAllocator* a, char* p) {
    if (a && p) a->Free(a, p);
}

#endif // ROOK_HAS_ONNXRUNTIME

} // anonymous namespace

struct OpenWakeWordAdapter::Impl {
#ifdef ROOK_HAS_ONNXRUNTIME
    void* onnx_lib = nullptr;
    const OrtApi* ort = nullptr;
    OrtEnv* env = nullptr;
    OrtAllocator* alloc = nullptr;

    OrtSession* mel_session = nullptr;
    char* mel_input_name = nullptr;
    char* mel_output_name = nullptr;

    OrtSession* emb_session = nullptr;
    char* emb_input_name = nullptr;
    char* emb_output_name = nullptr;

    OrtSession* cls_session = nullptr;
    char* cls_input_name = nullptr;
    char* cls_output_name = nullptr;

    OrtMemoryInfo* mem_info = nullptr;

    std::vector<int64_t> mel_shape;

    void releaseSessions() {
        if (!ort) return;
        release_alloc(alloc, mel_input_name);
        release_alloc(alloc, mel_output_name);
        release_alloc(alloc, emb_input_name);
        release_alloc(alloc, emb_output_name);
        release_alloc(alloc, cls_input_name);
        release_alloc(alloc, cls_output_name);
        mel_input_name = mel_output_name = nullptr;
        emb_input_name = emb_output_name = nullptr;
        cls_input_name = cls_output_name = nullptr;
        if (cls_session) { ort->ReleaseSession(cls_session); cls_session = nullptr; }
        if (emb_session) { ort->ReleaseSession(emb_session); emb_session = nullptr; }
        if (mel_session) { ort->ReleaseSession(mel_session); mel_session = nullptr; }
    }

    void releaseAll() {
        releaseSessions();
        if (ort) {
            if (mem_info) { ort->ReleaseMemoryInfo(mem_info); mem_info = nullptr; }
            if (env) { ort->ReleaseEnv(env); env = nullptr; }
        }
        alloc = nullptr;
        ort = nullptr;
        if (onnx_lib) { ::dlclose(onnx_lib); onnx_lib = nullptr; }
    }

    bool tryInitSessions();
#endif

    float sensitivity = 0.5f;
    bool ready = false;

    std::deque<int16_t> raw_buffer;
    int accumulated_samples = 0;

    std::vector<float> mel_buffer;
    std::vector<float> feature_buffer;

    float ema_prob = 0.0f;
    int cooldown = 0;
};

#ifdef ROOK_HAS_ONNXRUNTIME

bool OpenWakeWordAdapter::Impl::tryInitSessions() {
    ready = false;
    releaseAll();

    onnx_lib = ::dlopen("libonnxruntime.so", RTLD_NOW | RTLD_LOCAL);
    if (!onnx_lib) {
        SPDLOG_DEBUG("OpenWakeWord: libonnxruntime.so not found");
        return false;
    }

    auto* get_api_base = reinterpret_cast<const OrtApiBase* (*)()>(
        ::dlsym(onnx_lib, "OrtGetApiBase"));
    if (!get_api_base) {
        SPDLOG_DEBUG("OpenWakeWord: OrtGetApiBase not found");
        return false;
    }

    ort = get_api_base()->GetApi(ORT_API_VERSION);
    if (!ort) {
        SPDLOG_DEBUG("OpenWakeWord: OrtApi not available");
        return false;
    }

    auto status = ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "oww", &env);
    if (status) {
        log_ort_error(ort, status, "CreateEnv");
        return false;
    }

    status = ort->GetAllocatorWithDefaultOptions(&alloc);
    if (status) {
        log_ort_error(ort, status, "GetAllocator");
        return false;
    }

    status = ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem_info);
    if (status) {
        log_ort_error(ort, status, "CreateCpuMemoryInfo");
        return false;
    }

    releaseSessions();

    auto dir = models_dir();
    std::string mel_path = dir + "/melspectrogram.onnx";
    std::string emb_path = dir + "/embedding_model.onnx";
    std::string cls_path = dir + "/openwakeword.onnx";

    if (!std::filesystem::exists(mel_path) ||
        !std::filesystem::exists(emb_path) ||
        !std::filesystem::exists(cls_path)) {
        SPDLOG_DEBUG("OpenWakeWord: model files missing");
        return false;
    }

    OrtSessionOptions* sess_opts = nullptr;
    ORT_CHECK(ort->CreateSessionOptions(&sess_opts), "CreateSessionOptions");
    if (!sess_opts) return false;
    ORT_CHECK(ort->SetInterOpNumThreads(sess_opts, 1), "SetInterOpNumThreads");
    ORT_CHECK(ort->SetIntraOpNumThreads(sess_opts, 1), "SetIntraOpNumThreads");

    status = ort->CreateSession(env, mel_path.c_str(), sess_opts, &mel_session);
    if (status) {
        log_ort_error(ort, status, "CreateSession(mel)");
        ort->ReleaseSessionOptions(sess_opts);
        return false;
    }

    status = ort->CreateSession(env, emb_path.c_str(), sess_opts, &emb_session);
    if (status) {
        log_ort_error(ort, status, "CreateSession(emb)");
        ort->ReleaseSessionOptions(sess_opts);
        return false;
    }

    status = ort->CreateSession(env, cls_path.c_str(), sess_opts, &cls_session);
    if (status) {
        log_ort_error(ort, status, "CreateSession(cls)");
        ort->ReleaseSessionOptions(sess_opts);
        return false;
    }

    ort->ReleaseSessionOptions(sess_opts);

    ORT_CHECK(ort->SessionGetInputName(mel_session, 0, alloc, &mel_input_name),
              "SessionGetInputName(mel)");
    ORT_CHECK(ort->SessionGetOutputName(mel_session, 0, alloc, &mel_output_name),
              "SessionGetOutputName(mel)");

    {
        OrtTypeInfo* ti = nullptr;
        ORT_CHECK(ort->SessionGetInputTypeInfo(mel_session, 0, &ti),
                  "SessionGetInputTypeInfo(mel)");
        const OrtTensorTypeAndShapeInfo* tsi_c = nullptr;
        ORT_CHECK(ort->CastTypeInfoToTensorInfo(ti, &tsi_c),
                  "CastTypeInfoToTensorInfo(mel)");
        std::size_t ndim = 0;
        ORT_CHECK(ort->GetDimensionsCount(tsi_c, &ndim), "GetDimensionsCount(mel)");
        mel_shape.resize(ndim);
        ORT_CHECK(ort->GetDimensions(tsi_c, mel_shape.data(), ndim), "GetDimensions(mel)");
        ort->ReleaseTypeInfo(ti);
    }

    ORT_CHECK(ort->SessionGetInputName(emb_session, 0, alloc, &emb_input_name),
              "SessionGetInputName(emb)");
    ORT_CHECK(ort->SessionGetOutputName(emb_session, 0, alloc, &emb_output_name),
              "SessionGetOutputName(emb)");

    ORT_CHECK(ort->SessionGetInputName(cls_session, 0, alloc, &cls_input_name),
              "SessionGetInputName(cls)");
    ORT_CHECK(ort->SessionGetOutputName(cls_session, 0, alloc, &cls_output_name),
              "SessionGetOutputName(cls)");

    ready = true;
    SPDLOG_INFO("OpenWakeWord: engine ready (mel={}, emb={}, cls={})",
                mel_path, emb_path, cls_path);
    return true;
}

#endif // ROOK_HAS_ONNXRUNTIME

OpenWakeWordAdapter::OpenWakeWordAdapter()
    : m_impl(std::make_unique<Impl>())
{
#ifdef ROOK_HAS_ONNXRUNTIME
    m_impl->tryInitSessions();
#else
    SPDLOG_DEBUG("OpenWakeWord: compiled without ONNX Runtime support");
#endif
}

OpenWakeWordAdapter::~OpenWakeWordAdapter() {
#ifdef ROOK_HAS_ONNXRUNTIME
    m_impl->releaseAll();
#endif
}

bool OpenWakeWordAdapter::isReady() const {
    return m_impl->ready;
}

std::size_t OpenWakeWordAdapter::frameSize() const {
    return 512;
}

std::string OpenWakeWordAdapter::modelPath() const {
    return models_dir() + "/openwakeword.onnx";
}

std::string OpenWakeWordAdapter::defaultModelPath() const {
    return modelPath();
}

std::string OpenWakeWordAdapter::defaultModelUrl() const {
    return "https://github.com/dscripka/openWakeWord/releases/download/"
           "v0.5.1/alexa_v0.1.onnx";
}

void OpenWakeWordAdapter::downloadModel(ProgressFn on_progress, DoneFn on_done) {
    auto dir = models_dir();
    SPDLOG_INFO("OpenWakeWord: downloadModel called, dir={}", dir);

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        SPDLOG_ERROR("OpenWakeWord: create_directories failed: {}", ec.message());
        if (on_done) on_done(false);
        return;
    }

    auto mel_url = std::string(
        "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1/melspectrogram.onnx");
    auto emb_url = std::string(
        "https://github.com/dscripka/openWakeWord/releases/download/v0.5.1/embedding_model.onnx");
    auto cls_url = defaultModelUrl();

    auto mel_path = dir + "/melspectrogram.onnx";
    auto emb_path = dir + "/embedding_model.onnx";
    auto cls_path = dir + "/openwakeword.onnx";

    downloadFile(mel_url, mel_path,
        [on_progress](float p) { if (on_progress) on_progress(p * 0.30f); },
        [this, on_done, emb_url, emb_path, cls_url, cls_path, on_progress](bool ok) {
            if (!ok) { if (on_done) on_done(false); return; }

            downloadFile(emb_url, emb_path,
                [on_progress](float p) { if (on_progress) on_progress(0.30f + p * 0.35f); },
                [this, on_done, cls_url, cls_path, on_progress](bool ok2) {
                    if (!ok2) { if (on_done) on_done(false); return; }

                    downloadFile(cls_url, cls_path,
                        [on_progress](float p) { if (on_progress) on_progress(0.65f + p * 0.35f); },
                        [this, on_done](bool ok3) {
                            if (ok3) {
                                SPDLOG_INFO("OpenWakeWord: all models downloaded, reinitializing");
#ifdef ROOK_HAS_ONNXRUNTIME
                                m_impl->tryInitSessions();
#endif
                            }
                            if (on_done) on_done(ok3);
                        });
                });
        });
}

#ifdef ROOK_HAS_ONNXRUNTIME

static std::vector<float> run_melspectrogram(
    const OrtApi* ort, OrtSession* session,
    OrtMemoryInfo* mem_info,
    const char* input_name, const char* output_name,
    const int16_t* pcm, std::size_t sample_count)
{
    std::vector<float> result;

    std::vector<float> f32(sample_count);
    for (std::size_t i = 0; i < sample_count; ++i)
        f32[i] = static_cast<float>(pcm[i]);

    std::vector<int64_t> shape = {1, static_cast<int64_t>(sample_count)};

    OrtValue* input = nullptr;
    auto status = ort->CreateTensorWithDataAsOrtValue(
        mem_info, f32.data(), sample_count * sizeof(float),
        shape.data(), shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input);
    if (status) {
        log_ort_error(ort, status, "CreateTensor(mel)");
        return result;
    }

    const char* in_names[] = {input_name};
    const char* out_names[] = {output_name};
    const OrtValue* in_vals[] = {input};

    OrtValue* output = nullptr;
    status = ort->Run(session, nullptr,
                      in_names, in_vals, 1,
                      out_names, 1, &output);
    ort->ReleaseValue(input);
    if (status) {
        log_ort_error(ort, status, "Run(mel)");
        return result;
    }

    OrtTensorTypeAndShapeInfo* tsi = nullptr;
    status = ort->GetTensorTypeAndShape(output, &tsi);
    if (status == nullptr && tsi) {
        std::size_t n_elems = 0;
        ORT_CHECK(ort->GetTensorShapeElementCount(tsi, &n_elems),
                  "GetTensorShapeElementCount(mel)");

        float* data = nullptr;
        ORT_CHECK(ort->GetTensorMutableData(output, reinterpret_cast<void**>(&data)),
                  "GetTensorMutableData(mel)");

        result.assign(data, data + n_elems);
        ort->ReleaseTensorTypeAndShapeInfo(tsi);
    }
    ort->ReleaseValue(output);

    return result;
}

static std::vector<std::vector<float>> run_embedding(
    const OrtApi* ort, OrtSession* session,
    OrtMemoryInfo* mem_info,
    const char* input_name, const char* output_name,
    const std::vector<float>& mel_data, int mel_frames)
{
    std::vector<std::vector<float>> embeddings;

    if (mel_frames < k_mel_win) return embeddings;

    std::vector<std::array<float, k_mel_win * k_n_mels>> windows;
    for (int i = 0; i <= mel_frames - k_mel_win; i += k_mel_step) {
        windows.emplace_back();
        for (int j = 0; j < k_mel_win; ++j) {
            int src_idx = (i + j) * k_n_mels;
            for (int m = 0; m < k_n_mels; ++m)
                windows.back()[j * k_n_mels + m] = mel_data[src_idx + m];
        }
    }

    if (windows.empty()) return embeddings;

    int batch_size = static_cast<int>(windows.size());
    std::vector<float> batch_data(batch_size * k_mel_win * k_n_mels);
    for (int b = 0; b < batch_size; ++b)
        std::memcpy(batch_data.data() + b * k_mel_win * k_n_mels,
                    windows[b].data(),
                    k_mel_win * k_n_mels * sizeof(float));

    std::vector<int64_t> shape = {static_cast<int64_t>(batch_size),
                                  static_cast<int64_t>(k_mel_win),
                                  static_cast<int64_t>(k_n_mels), 1};

    OrtValue* input = nullptr;
    auto status = ort->CreateTensorWithDataAsOrtValue(
        mem_info, batch_data.data(), batch_data.size() * sizeof(float),
        shape.data(), shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input);
    if (status) {
        log_ort_error(ort, status, "CreateTensor(emb)");
        return embeddings;
    }

    const char* in_names[] = {input_name};
    const char* out_names[] = {output_name};
    const OrtValue* in_vals[] = {input};

    OrtValue* output = nullptr;
    status = ort->Run(session, nullptr,
                      in_names, in_vals, 1,
                      out_names, 1, &output);
    ort->ReleaseValue(input);
    if (status) {
        log_ort_error(ort, status, "Run(emb)");
        return embeddings;
    }

    OrtTensorTypeAndShapeInfo* tsi = nullptr;
    status = ort->GetTensorTypeAndShape(output, &tsi);
    if (status == nullptr && tsi) {
        std::size_t n_elems = 0;
        ORT_CHECK(ort->GetTensorShapeElementCount(tsi, &n_elems),
                  "GetTensorShapeElementCount(emb)");

        float* data = nullptr;
        ORT_CHECK(ort->GetTensorMutableData(output, reinterpret_cast<void**>(&data)),
                  "GetTensorMutableData(emb)");

        int total_embeddings = static_cast<int>(n_elems) / k_feature_dim;
        embeddings.resize(total_embeddings);
        for (int b = 0; b < total_embeddings; ++b) {
            embeddings[b].assign(data + b * k_feature_dim,
                                 data + (b + 1) * k_feature_dim);
        }
        ort->ReleaseTensorTypeAndShapeInfo(tsi);
    }
    ort->ReleaseValue(output);

    return embeddings;
}

static float run_classifier(
    const OrtApi* ort, OrtSession* session,
    OrtMemoryInfo* mem_info,
    const char* input_name, const char* output_name,
    const std::vector<float>& feature_data, int num_features)
{
    std::vector<float> flat(num_features * k_feature_dim);
    std::memcpy(flat.data(), feature_data.data() + (feature_data.size() - num_features * k_feature_dim),
                num_features * k_feature_dim * sizeof(float));

    std::vector<int64_t> shape = {1, static_cast<int64_t>(num_features),
                                  static_cast<int64_t>(k_feature_dim)};

    OrtValue* input = nullptr;
    auto status = ort->CreateTensorWithDataAsOrtValue(
        mem_info, flat.data(), flat.size() * sizeof(float),
        shape.data(), shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input);
    if (status) {
        log_ort_error(ort, status, "CreateTensor(cls)");
        return 0.0f;
    }

    const char* in_names[] = {input_name};
    const char* out_names[] = {output_name};
    const OrtValue* in_vals[] = {input};

    OrtValue* output = nullptr;
    status = ort->Run(session, nullptr,
                      in_names, in_vals, 1,
                      out_names, 1, &output);
    ort->ReleaseValue(input);
    if (status) {
        log_ort_error(ort, status, "Run(cls)");
        return 0.0f;
    }

    float prob = 0.0f;
    float* data = nullptr;
    status = ort->GetTensorMutableData(output, reinterpret_cast<void**>(&data));
    if (status == nullptr && data) {
        prob = data[0];
    } else if (status) {
        log_ort_error(ort, status, "GetTensorMutableData(cls)");
    }
    ort->ReleaseValue(output);

    return prob;
}

#endif // ROOK_HAS_ONNXRUNTIME

bool OpenWakeWordAdapter::processFrame(const int16_t* pcm) {
    if (!m_impl->ready) return false;

#ifdef ROOK_HAS_ONNXRUNTIME
    const auto* ort = m_impl->ort;

    for (std::size_t i = 0; i < 512; ++i)
        m_impl->raw_buffer.push_back(pcm[i]);

    while (m_impl->raw_buffer.size() > k_max_raw_buffer)
        m_impl->raw_buffer.pop_front();

    m_impl->accumulated_samples += 512;

    if (m_impl->accumulated_samples >= k_chunk_size) {
        int n_samples = m_impl->accumulated_samples;
        std::size_t n_raw = m_impl->raw_buffer.size();

        std::size_t mel_input_samples = n_samples + k_hop_samples * 3;
        if (mel_input_samples > n_raw) mel_input_samples = n_raw;

        std::vector<int16_t> mel_pcm(mel_input_samples);
        for (std::size_t i = 0; i < mel_input_samples; ++i)
            mel_pcm[i] = m_impl->raw_buffer[n_raw - mel_input_samples + i];

        auto mel_out = run_melspectrogram(
            ort, m_impl->mel_session, m_impl->mem_info,
            m_impl->mel_input_name, m_impl->mel_output_name,
            mel_pcm.data(), mel_pcm.size());

        if (!mel_out.empty()) {
            int mel_frames = static_cast<int>(mel_out.size()) / k_n_mels;
            int new_frames_start = mel_frames - (n_samples / k_hop_samples) - 3;
            if (new_frames_start < 0) new_frames_start = 0;

            for (int f = new_frames_start; f < mel_frames; ++f) {
                for (int m = 0; m < k_n_mels; ++m) {
                    float val = mel_out[f * k_n_mels + m];
                    m_impl->mel_buffer.push_back(val / 10.0f + 2.0f);
                }
            }

            while (m_impl->mel_buffer.size() > k_max_mel_frames * k_n_mels) {
                m_impl->mel_buffer.erase(m_impl->mel_buffer.begin(),
                                         m_impl->mel_buffer.begin() + k_n_mels);
            }

            int total_mel_frames = static_cast<int>(m_impl->mel_buffer.size()) / k_n_mels;
            auto embeddings = run_embedding(
                ort, m_impl->emb_session, m_impl->mem_info,
                m_impl->emb_input_name, m_impl->emb_output_name,
                m_impl->mel_buffer, total_mel_frames);

            for (auto& emb : embeddings) {
                m_impl->feature_buffer.insert(m_impl->feature_buffer.end(),
                                              emb.begin(), emb.end());
            }

            while (m_impl->feature_buffer.size() > k_max_feature_frames * k_feature_dim) {
                m_impl->feature_buffer.erase(m_impl->feature_buffer.begin(),
                                             m_impl->feature_buffer.begin() + k_feature_dim);
            }
        }

        m_impl->accumulated_samples = 0;
    }

    int total_features = static_cast<int>(m_impl->feature_buffer.size()) / k_feature_dim;
    if (total_features >= k_classifier_frames) {
        float prob = run_classifier(
            ort, m_impl->cls_session, m_impl->mem_info,
            m_impl->cls_input_name, m_impl->cls_output_name,
            m_impl->feature_buffer, k_classifier_frames);

        m_impl->ema_prob = (1.0f - k_ema_alpha) * m_impl->ema_prob
                           + k_ema_alpha * prob;

        if (m_impl->cooldown > 0) {
            m_impl->cooldown--;
        } else if (m_impl->ema_prob > m_impl->sensitivity) {
            m_impl->cooldown = k_cooldown_frames;
            SPDLOG_DEBUG("OpenWakeWord: wake word detected (p={:.3f})", m_impl->ema_prob);
            reset();
            return true;
        }
    }
#endif

    return false;
}

void OpenWakeWordAdapter::reset() {
    m_impl->raw_buffer.clear();
    m_impl->accumulated_samples = 0;
    m_impl->mel_buffer.clear();
    m_impl->feature_buffer.clear();
    m_impl->ema_prob = 0.0f;
}

void OpenWakeWordAdapter::setSensitivity(float value) {
    m_impl->sensitivity = std::clamp(value, 0.0f, 1.0f);
}

} // namespace rook::adapters::audio
