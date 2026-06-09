#include "rook/adapters/audio/openwakeword_adapter.hpp"
#include "rook/adapters/audio/model_downloader.hpp"

#include <spdlog/spdlog.h>
#include <dlfcn.h>
#include <filesystem>

namespace rook::adapters::audio {

struct OpenWakeWordAdapter::Impl {
    void* onnx_lib = nullptr;
    float sensitivity = 0.5f;
    bool ready = false;
};

OpenWakeWordAdapter::OpenWakeWordAdapter()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->onnx_lib = ::dlopen("libonnxruntime.so", RTLD_NOW | RTLD_LOCAL);
    if (!m_impl->onnx_lib) {
        SPDLOG_DEBUG("OpenWakeWord: libonnxruntime.so not found, engine unavailable");
        return;
    }
    SPDLOG_DEBUG("OpenWakeWord: libonnxruntime.so loaded");

    auto* user_data = ::getenv("XDG_DATA_HOME");
    std::string data_dir = user_data ? std::string(user_data) + "/rook/models/wakewords"
                                     : std::string(::getenv("HOME")) + "/.local/share/rook/models/wakewords";

    auto candidate = data_dir + "/openwakeword.onnx";
    if (!std::filesystem::exists(candidate)) {
        SPDLOG_DEBUG("OpenWakeWord: model not found at {}", candidate);
        return;
    }

    SPDLOG_INFO("OpenWakeWord: engine ready (model={})", candidate);
    m_impl->ready = true;
}

OpenWakeWordAdapter::~OpenWakeWordAdapter() {
    if (m_impl->onnx_lib)
        ::dlclose(m_impl->onnx_lib);
}

bool OpenWakeWordAdapter::isReady() const {
    return m_impl->ready;
}

std::size_t OpenWakeWordAdapter::frameSize() const {
    return 512;
}

bool OpenWakeWordAdapter::processFrame(const int16_t*) {
    return false;
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
    return "";
}

void OpenWakeWordAdapter::downloadModel(ProgressFn on_progress, DoneFn on_done) {
    if (on_done) on_done(false);
    (void)on_progress;
}

} // namespace rook::adapters::audio
