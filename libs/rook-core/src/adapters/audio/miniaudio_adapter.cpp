#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "rook/adapters/audio/miniaudio_adapter.hpp"

#include <spdlog/spdlog.h>
#include <cstring>
#include <thread>

namespace rook::adapters::audio {

struct MiniaudioAdapter::Impl {
    ma_context context{};
    ma_device capture_device{};
    ma_device playback_device{};
    bool context_ok = false;
    bool capture_open = false;
    bool playback_open = false;
    ports::AudioCaptureCallback capture_cb;
    rook::core::SpScRingBuffer<float> playback_buffer{262144};
    int playback_sample_rate = 0;

    static void onCapture(ma_device* device, void*, const void* input, ma_uint32 frame_count) {
        auto* self = static_cast<Impl*>(device->pUserData);
        if (self->capture_cb)
            self->capture_cb(static_cast<const int16_t*>(input), frame_count);
    }

    static void onPlayback(ma_device* device, void* output, const void*, ma_uint32 frame_count) {
        auto* self = static_cast<Impl*>(device->pUserData);
        auto* out = static_cast<float*>(output);
        auto read = self->playback_buffer.read(out, frame_count);
        if (read < frame_count)
            std::memset(out + read, 0, (frame_count - read) * sizeof(float));
    }
};

MiniaudioAdapter::MiniaudioAdapter()
    : m_impl(std::make_unique<Impl>())
{
    if (ma_context_init(nullptr, 0, nullptr, &m_impl->context) == MA_SUCCESS) {
        m_impl->context_ok = true;
        SPDLOG_DEBUG("miniaudio context initialized");
    } else {
        SPDLOG_ERROR("miniaudio context init failed");
    }
}

MiniaudioAdapter::~MiniaudioAdapter() {
    stopCapture();
    stopPlayback();
    if (m_impl->context_ok)
        ma_context_uninit(&m_impl->context);
}

std::vector<ports::DeviceInfo> MiniaudioAdapter::enumerateInputs() const {
    std::vector<ports::DeviceInfo> result;
    if (!m_impl->context_ok) return result;

    ma_device_info* devices = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&m_impl->context, nullptr, nullptr, &devices, &count) != MA_SUCCESS)
        return result;

    for (ma_uint32 i = 0; i < count; ++i) {
        result.push_back({
            std::string(devices[i].id.alsa[0] ? devices[i].id.alsa : ""),
            std::string(devices[i].name),
            devices[i].isDefault != 0
        });
    }
    return result;
}

std::vector<ports::DeviceInfo> MiniaudioAdapter::enumerateOutputs() const {
    std::vector<ports::DeviceInfo> result;
    if (!m_impl->context_ok) return result;

    ma_device_info* devices = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&m_impl->context, &devices, &count, nullptr, nullptr) != MA_SUCCESS)
        return result;

    for (ma_uint32 i = 0; i < count; ++i) {
        result.push_back({
            std::string(devices[i].id.alsa[0] ? devices[i].id.alsa : ""),
            std::string(devices[i].name),
            devices[i].isDefault != 0
        });
    }
    return result;
}

bool MiniaudioAdapter::startCapture(std::string_view device_id,
                                     ports::AudioCaptureCallback callback) {
    if (!m_impl->context_ok || m_impl->capture_open) return false;

    auto config = ma_device_config_init(ma_device_type_capture);
    config.capture.format   = ma_format_s16;
    config.capture.channels = 1;
    config.sampleRate       = 16000;
    config.periodSizeInFrames = 480;
    config.dataCallback     = Impl::onCapture;
    config.pUserData        = m_impl.get();

    ma_device_info* devices = nullptr;
    ma_uint32 count = 0;
    if (device_id.empty()) {
        ma_context_get_devices(&m_impl->context, nullptr, nullptr, &devices, &count);
    }

    ma_device_id did;

    if (!device_id.empty()) {
        std::memset(&did, 0, sizeof(did));
        std::strncpy(did.alsa, device_id.data(), std::min(sizeof(did.alsa) - 1, device_id.size()));
        config.capture.pDeviceID = &did;
    }

    if (ma_device_init(&m_impl->context, &config, &m_impl->capture_device) == MA_SUCCESS) {
        if (ma_device_start(&m_impl->capture_device) == MA_SUCCESS) {
            m_impl->capture_cb = std::move(callback);
            m_impl->capture_open = true;
        SPDLOG_INFO("miniaudio capture started (16kHz mono s16)");
            return true;
        }
        SPDLOG_ERROR("miniaudio capture device start failed");
        ma_device_uninit(&m_impl->capture_device);
        std::memset(&m_impl->capture_device, 0, sizeof(m_impl->capture_device));
    }

    if (!device_id.empty() || count == 0) {
        SPDLOG_ERROR("miniaudio capture device init failed");
        return false;
    }

    for (ma_uint32 i = 0; i < count; ++i) {
        if (devices[i].id.alsa[0] == '\0') continue;
        SPDLOG_INFO("miniaudio: falling back to capture device '{}'", devices[i].name);

        auto fb_config = ma_device_config_init(ma_device_type_capture);
        fb_config.capture.format   = ma_format_s16;
        fb_config.capture.channels = 1;
        fb_config.sampleRate       = 16000;
        fb_config.periodSizeInFrames = 480;
        fb_config.dataCallback     = Impl::onCapture;
        fb_config.pUserData        = m_impl.get();
        fb_config.capture.pDeviceID = &devices[i].id;

        if (ma_device_init(&m_impl->context, &fb_config, &m_impl->capture_device) != MA_SUCCESS)
            continue;
        if (ma_device_start(&m_impl->capture_device) != MA_SUCCESS) {
            ma_device_uninit(&m_impl->capture_device);
            std::memset(&m_impl->capture_device, 0, sizeof(m_impl->capture_device));
            continue;
        }

        m_impl->capture_cb = std::move(callback);
        m_impl->capture_open = true;
        SPDLOG_INFO("miniaudio capture started on fallback device '{}' (16kHz mono s16)", devices[i].name);
        return true;
    }

    SPDLOG_ERROR("miniaudio: no capture device available");
    return false;
}

void MiniaudioAdapter::stopCapture() {
    if (!m_impl->capture_open) return;

    ma_device_stop(&m_impl->capture_device);
    ma_device_uninit(&m_impl->capture_device);
    std::memset(&m_impl->capture_device, 0, sizeof(m_impl->capture_device));
    m_impl->capture_cb = nullptr;
    m_impl->capture_open = false;
    SPDLOG_DEBUG("miniaudio capture stopped");
}

bool MiniaudioAdapter::isCaptureActive() const {
    return m_impl->capture_open;
}

bool MiniaudioAdapter::startPlayback(std::string_view device_id, int sample_rate) {
    if (!m_impl->context_ok || m_impl->playback_open) return false;

    m_impl->playback_buffer.clear();
    m_impl->playback_sample_rate = sample_rate;

    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 1;
    config.sampleRate        = static_cast<ma_uint32>(sample_rate);
    config.periodSizeInFrames = 512;
    config.dataCallback      = Impl::onPlayback;
    config.pUserData         = m_impl.get();

    ma_device_id did;
    if (!device_id.empty()) {
        std::memset(&did, 0, sizeof(did));
        std::strncpy(did.alsa, device_id.data(), std::min(sizeof(did.alsa) - 1, device_id.size()));
        config.playback.pDeviceID = &did;
    }

    if (ma_device_init(&m_impl->context, &config, &m_impl->playback_device) != MA_SUCCESS) {
        SPDLOG_ERROR("miniaudio playback device init failed");
        return false;
    }

    if (ma_device_start(&m_impl->playback_device) != MA_SUCCESS) {
        SPDLOG_ERROR("miniaudio playback device start failed");
        ma_device_uninit(&m_impl->playback_device);
        return false;
    }

    m_impl->playback_open = true;
    SPDLOG_INFO("miniaudio playback started ({}Hz mono f32)", sample_rate);
    return true;
}

bool MiniaudioAdapter::writePlayback(const float* pcm, std::size_t sample_count) {
    if (!m_impl->playback_open) return false;
    while (sample_count > 0) {
        auto written = m_impl->playback_buffer.write(pcm, sample_count);
        if (written == 0) {
            std::this_thread::yield();
            continue;
        }
        pcm += written;
        sample_count -= written;
    }
    return true;
}

void MiniaudioAdapter::stopPlayback() {
    if (!m_impl->playback_open) return;

    m_impl->playback_buffer.clear();
    ma_device_stop(&m_impl->playback_device);
    ma_device_uninit(&m_impl->playback_device);
    m_impl->playback_open = false;
    SPDLOG_DEBUG("miniaudio playback stopped");
}

bool MiniaudioAdapter::isPlaybackActive() const {
    return m_impl->playback_open;
}

} // namespace rook::adapters::audio
