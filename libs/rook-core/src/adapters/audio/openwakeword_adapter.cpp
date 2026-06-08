#include "rook/adapters/audio/openwakeword_adapter.hpp"

#include <spdlog/spdlog.h>

#include <dlfcn.h>
#include <filesystem>

namespace rook::adapters::audio {

struct OpenWakeWordAdapter::Impl {
    void* onnx_lib = nullptr;
    std::string model_path;
    float sensitivity = 0.5f;
    bool ready = false;
};

OpenWakeWordAdapter::OpenWakeWordAdapter()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->onnx_lib = ::dlopen("libonnxruntime.so", RTLD_NOW | RTLD_LOCAL);
    if (m_impl->onnx_lib) {
        SPDLOG_DEBUG("OpenWakeWord: libonnxruntime.so loaded");
    } else {
        SPDLOG_DEBUG("OpenWakeWord: libonnxruntime.so not found ({}), engine unavailable", ::dlerror());
        return;
    }

    auto* user_data = ::getenv("XDG_DATA_HOME");
    std::string data_dir = user_data ? std::string(user_data) + "/rook/models/wakewords"
                                     : std::string(::getenv("HOME")) + "/.local/share/rook/models/wakewords";

    auto candidate = data_dir + "/openwakeword.onnx";
    if (std::filesystem::exists(candidate)) {
        m_impl->model_path = candidate;
    } else {
        SPDLOG_DEBUG("OpenWakeWord: model not found at {}", candidate);
        return;
    }

    SPDLOG_INFO("OpenWakeWord: engine ready (model={})", m_impl->model_path);
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
    if (!m_impl->ready) return false;
    return false;
}

void OpenWakeWordAdapter::reset() {
}

void OpenWakeWordAdapter::setSensitivity(float value) {
    m_impl->sensitivity = value;
}

} // namespace rook::adapters::audio
