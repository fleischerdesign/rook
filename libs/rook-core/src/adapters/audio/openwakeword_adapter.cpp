#include "rook/adapters/audio/openwakeword_adapter.hpp"
#include "rook/adapters/audio/model_downloader.hpp"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <cmath>
#include <dlfcn.h>

#include <rook_config.h>

#ifdef ROOK_HAS_ONNXRUNTIME
#include <onnxruntime_c_api.h>
#endif

namespace rook::adapters::audio {

struct OpenWakeWordAdapter::Impl {
    float sensitivity = 0.5f;
    bool ready = false;

#ifdef ROOK_HAS_ONNXRUNTIME
    const OrtApi* ort = nullptr;
    OrtEnv* env = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memory_info = nullptr;
    std::vector<const char*> input_names;
    std::vector<const char*> output_names;

    ~Impl() {
        if (session) ort->ReleaseSession(session);
        if (env) ort->ReleaseEnv(env);
        if (memory_info) ort->ReleaseMemoryInfo(memory_info);
    }
#else
    void* onnx_lib = nullptr;

    ~Impl() {
        if (onnx_lib) ::dlclose(onnx_lib);
    }
#endif
};

OpenWakeWordAdapter::OpenWakeWordAdapter()
    : m_impl(std::make_unique<Impl>())
{
    auto* user_data = ::getenv("XDG_DATA_HOME");
    std::string data_dir = user_data ? std::string(user_data) + "/rook/models/wakewords"
                                     : std::string(::getenv("HOME")) + "/.local/share/rook/models/wakewords";

    auto candidate = data_dir + "/openwakeword.onnx";
    if (!std::filesystem::exists(candidate)) {
        SPDLOG_DEBUG("OpenWakeWord: model not found at {}", candidate);
        return;
    }

#ifdef ROOK_HAS_ONNXRUNTIME
    m_impl->ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!m_impl->ort) {
        SPDLOG_ERROR("OpenWakeWord: GetApi failed");
        return;
    }

    auto status = m_impl->ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING,
                                          "rook_wakeword", &m_impl->env);
    if (status) {
        SPDLOG_ERROR("OpenWakeWord: CreateEnv failed");
        return;
    }

    OrtSessionOptions* session_opts = nullptr;
    m_impl->ort->CreateSessionOptions(&session_opts);
    m_impl->ort->SetSessionGraphOptimizationLevel(session_opts, ORT_ENABLE_ALL);

    status = m_impl->ort->CreateSession(m_impl->env, candidate.c_str(),
                                        session_opts, &m_impl->session);
    m_impl->ort->ReleaseSessionOptions(session_opts);

    if (status) {
        SPDLOG_ERROR("OpenWakeWord: CreateSession failed");
        return;
    }

    m_impl->ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault,
                                      &m_impl->memory_info);

    OrtAllocator* allocator = nullptr;
    m_impl->ort->GetAllocatorWithDefaultOptions(&allocator);

    std::size_t num_inputs = 0;
    m_impl->ort->SessionGetInputCount(m_impl->session, &num_inputs);
    for (std::size_t i = 0; i < num_inputs; ++i) {
        char* name_c = nullptr;
        m_impl->ort->SessionGetInputName(m_impl->session, i, allocator, &name_c);
        m_impl->input_names.push_back(name_c);
        allocator->Free(allocator, name_c);
    }

    std::size_t num_outputs = 0;
    m_impl->ort->SessionGetOutputCount(m_impl->session, &num_outputs);
    for (std::size_t i = 0; i < num_outputs; ++i) {
        char* name_c = nullptr;
        m_impl->ort->SessionGetOutputName(m_impl->session, i, allocator, &name_c);
        m_impl->output_names.push_back(name_c);
        allocator->Free(allocator, name_c);
    }

    SPDLOG_INFO("OpenWakeWord: engine ready (model={}, inputs={}, outputs={})",
                candidate, num_inputs, num_outputs);
    m_impl->ready = true;

#else
    m_impl->onnx_lib = ::dlopen("libonnxruntime.so", RTLD_NOW | RTLD_LOCAL);
    if (!m_impl->onnx_lib) {
        SPDLOG_DEBUG("OpenWakeWord: libonnxruntime.so not found, engine unavailable");
        return;
    }
    SPDLOG_INFO("OpenWakeWord: engine ready (dlopen, no infer — rebuild with onnxruntime-dev)");
    m_impl->ready = true;
#endif
}

OpenWakeWordAdapter::~OpenWakeWordAdapter() = default;

bool OpenWakeWordAdapter::isReady() const {
    return m_impl->ready;
}

std::size_t OpenWakeWordAdapter::frameSize() const {
    return 512;
}

bool OpenWakeWordAdapter::processFrame(const int16_t* pcm) {
    if (!m_impl->ready) return false;

#ifdef ROOK_HAS_ONNXRUNTIME
    std::vector<float> mel_input(512);
    for (std::size_t i = 0; i < 512; ++i)
        mel_input[i] = static_cast<float>(pcm[i]) / 32768.0f;

    const int64_t shape[] = {1, 512};
    OrtValue* input_tensor = nullptr;
    auto status = m_impl->ort->CreateTensorWithDataAsOrtValue(
        m_impl->memory_info,
        mel_input.data(),
        mel_input.size() * sizeof(float),
        shape, 2,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &input_tensor);

    if (status) return false;

    const OrtValue* inputs[] = {input_tensor};
    OrtValue* outputs[4] = {nullptr};

    status = m_impl->ort->Run(m_impl->session, nullptr,
                              m_impl->input_names.data(), inputs,
                              m_impl->input_names.size(),
                              m_impl->output_names.data(), outputs,
                              m_impl->output_names.size());

    m_impl->ort->ReleaseValue(input_tensor);

    if (status) {
        for (std::size_t i = 0; i < m_impl->output_names.size(); ++i)
            if (outputs[i]) m_impl->ort->ReleaseValue(outputs[i]);
        return false;
    }

    float max_score = 0.0f;
    for (std::size_t i = 0; i < m_impl->output_names.size(); ++i) {
        if (!outputs[i]) continue;
        float* data = nullptr;
        m_impl->ort->GetTensorMutableData(outputs[i], reinterpret_cast<void**>(&data));
        std::size_t count = 0;
        {
            OrtTensorTypeAndShapeInfo* info = nullptr;
            m_impl->ort->GetTensorTypeAndShape(outputs[i], &info);
            std::size_t dims = 0;
            m_impl->ort->GetDimensionsCount(info, &dims);
            int64_t dim_vals[4] = {};
            if (dims > 0) m_impl->ort->GetDimensions(info, dim_vals, dims);
            count = (dims > 0 && dim_vals[0] > 0) ? static_cast<std::size_t>(dim_vals[0]) : 1;
            m_impl->ort->ReleaseTensorTypeAndShapeInfo(info);
        }
        if (data) {
            for (std::size_t j = 0; j < count; ++j)
                if (data[j] > max_score) max_score = data[j];
        }
        m_impl->ort->ReleaseValue(outputs[i]);
    }

    return max_score > m_impl->sensitivity;
#else
    (void)pcm;
    return false;
#endif
}

void OpenWakeWordAdapter::reset() {
}

void OpenWakeWordAdapter::setSensitivity(float value) {
    m_impl->sensitivity = value;
}

std::string OpenWakeWordAdapter::defaultModelPath() const {
    auto* d = ::getenv("XDG_DATA_HOME");
    std::string base = d ? std::string(d) : std::string(::getenv("HOME")) + "/.local/share";
    return base + "/rook/models/wakewords/openwakeword.onnx";
}

std::string OpenWakeWordAdapter::defaultModelUrl() const {
    return "https://huggingface.co/datasets/roskiroskiroskiroskiroskiroski/open-wake-word-models/resolve/main/openwakeword.onnx";
}

void OpenWakeWordAdapter::downloadModel(ProgressFn on_progress, DoneFn on_done) {
    auto path = defaultModelPath();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    downloadFile(defaultModelUrl(), path,
        [on_progress](float p) { if (on_progress) on_progress(p); },
        [this, path, on_done](bool success) {
            if (success) {
                ::dlclose(m_impl->onnx_lib);
                m_impl->onnx_lib = nullptr;
                m_impl->ready = false;
            }
            if (on_done) on_done(success);
        });
}

} // namespace rook::adapters::audio
