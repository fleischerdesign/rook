#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "rook/adapters/audio/miniaudio_adapter.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <thread>

namespace rook::adapters::audio {

namespace {

constexpr std::size_t k_ma_device_id_size = sizeof(ma_device_id);

std::string serialize_device_id(const ma_device_id& id) {
    std::string hex(k_ma_device_id_size * 2, '0');
    auto* raw = reinterpret_cast<const unsigned char*>(&id);
    for (std::size_t i = 0; i < k_ma_device_id_size; ++i) {
        static const char nib[] = "0123456789abcdef";
        hex[i * 2]     = nib[(raw[i] >> 4) & 0xf];
        hex[i * 2 + 1] = nib[raw[i] & 0xf];
    }
    return hex;
}

bool deserialize_device_id(std::string_view hex, ma_device_id& out) {
    if (hex.size() < k_ma_device_id_size * 2) return false;
    std::memset(&out, 0, sizeof(out));
    auto* raw = reinterpret_cast<unsigned char*>(&out);
    for (std::size_t i = 0; i < k_ma_device_id_size; ++i) {
        auto hi = hex[i * 2];
        auto lo = hex[i * 2 + 1];
        auto from_nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int h = from_nib(hi), l = from_nib(lo);
        if (h < 0 || l < 0) return false;
        raw[i] = static_cast<unsigned char>((h << 4) | l);
    }
    return true;
}

std::string backend_name(ma_backend backend) {
    switch (backend) {
        case ma_backend_pulseaudio: return "PulseAudio";
        case ma_backend_alsa:       return "ALSA";
        case ma_backend_jack:       return "JACK";
        case ma_backend_oss:        return "OSS";
        case ma_backend_wasapi:     return "WASAPI";
        case ma_backend_dsound:     return "DirectSound";
        case ma_backend_winmm:      return "WinMM";
        case ma_backend_coreaudio:  return "CoreAudio";
        default:                    return "Unknown";
    }
}

const ma_backend k_backend_priority[] = {
    ma_backend_pulseaudio,
    ma_backend_alsa,
};

} // anonymous namespace

struct MiniaudioAdapter::Impl {
    ma_context context{};
    ma_device capture_device{};
    ma_device playback_device{};
    ma_backend active_backend = ma_backend_null;
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
    auto config = ma_context_config_init();
    config.alsa.useVerboseDeviceEnumeration = MA_TRUE;

    ma_result result = ma_context_init(k_backend_priority,
                                        sizeof(k_backend_priority) / sizeof(k_backend_priority[0]),
                                        &config, &m_impl->context);

    if (result == MA_SUCCESS) {
        m_impl->context_ok = true;
        m_impl->active_backend = m_impl->context.backend;
        SPDLOG_INFO("miniaudio: context initialized (backend={}, verbose enumeration)",
                    backend_name(m_impl->active_backend));
    } else {
        SPDLOG_ERROR("miniaudio: context init failed (code={})", static_cast<int>(result));
    }
}

MiniaudioAdapter::~MiniaudioAdapter() {
    stopCapture();
    stopPlayback();
    if (m_impl->context_ok)
        ma_context_uninit(&m_impl->context);
}

static std::vector<ports::DeviceInfo> do_enumerate(
    ma_context& ctx, ma_device_type type, ma_backend active_backend)
{
    std::vector<ports::DeviceInfo> result;

    ma_device_info* devices = nullptr;
    ma_uint32 pb_count = 0;
    ma_uint32 cap_count = 0;
    ma_result r = ma_context_get_devices(&ctx,
        (type == ma_device_type_playback) ? &devices : nullptr,
        &pb_count,
        (type == ma_device_type_capture)  ? &devices : nullptr,
        &cap_count);
    if (r != MA_SUCCESS) return result;

    ma_uint32 count = (type == ma_device_type_playback) ? pb_count : cap_count;
    if (count == 0) return result;

    auto backend_label = backend_name(active_backend);

    for (ma_uint32 i = 0; i < count; ++i) {
        auto& d = devices[i];
        auto id_hex = serialize_device_id(d.id);
        auto name = std::string(d.name);

        result.push_back(ports::DeviceInfo{
            .id = std::move(id_hex),
            .name = name.empty() ? std::string("Default Device") : std::move(name),
            .backend = backend_label,
            .is_default = d.isDefault != 0,
        });
    }

    return result;
}

std::vector<ports::DeviceInfo> MiniaudioAdapter::enumerateInputs() const {
    if (!m_impl->context_ok) return {};
    return do_enumerate(m_impl->context, ma_device_type_capture, m_impl->active_backend);
}

std::vector<ports::DeviceInfo> MiniaudioAdapter::enumerateOutputs() const {
    if (!m_impl->context_ok) return {};
    return do_enumerate(m_impl->context, ma_device_type_playback, m_impl->active_backend);
}

static bool init_device(ma_context& ctx, ma_device& dev, ma_device_type type,
                         std::string_view device_id_hex, bool capture,
                         ma_uint32 sample_rate, ma_format format,
                         ma_uint32 period_frames, ma_device_data_proc callback,
                         void* pUserData)
{
    ma_device_config config = ma_device_config_init(type);
    if (capture) {
        config.capture.format   = format;
        config.capture.channels = 1;
    } else {
        config.playback.format   = format;
        config.playback.channels = 1;
    }
    config.sampleRate          = sample_rate;
    config.periodSizeInFrames  = period_frames;
    config.dataCallback        = callback;
    config.pUserData           = pUserData;

    ma_device_id did;
    bool has_explicit_device = false;

    if (!device_id_hex.empty()) {
        if (deserialize_device_id(device_id_hex, did)) {
            if (capture)
                config.capture.pDeviceID = &did;
            else
                config.playback.pDeviceID = &did;
            has_explicit_device = true;
        }
    }

    if (ma_device_init(&ctx, &config, &dev) == MA_SUCCESS) {
        if (ma_device_start(&dev) == MA_SUCCESS) {
            return true;
        }
        SPDLOG_ERROR("miniaudio: device start failed for '{}'",
                     has_explicit_device ? device_id_hex : "default");
        ma_device_uninit(&dev);
        std::memset(&dev, 0, sizeof(dev));
    } else {
        SPDLOG_ERROR("miniaudio: device init failed for '{}'",
                     has_explicit_device ? device_id_hex : "default");
    }

    if (!has_explicit_device) return false;

    SPDLOG_INFO("miniaudio: falling back to default device");

    ma_device_config fb_config = ma_device_config_init(type);
    if (capture) {
        fb_config.capture.format   = format;
        fb_config.capture.channels = 1;
    } else {
        fb_config.playback.format   = format;
        fb_config.playback.channels = 1;
    }
    fb_config.sampleRate         = sample_rate;
    fb_config.periodSizeInFrames = period_frames;
    fb_config.dataCallback       = callback;
    fb_config.pUserData          = pUserData;

    if (ma_device_init(&ctx, &fb_config, &dev) != MA_SUCCESS) {
        SPDLOG_ERROR("miniaudio: fallback device init failed");
        return false;
    }
    if (ma_device_start(&dev) != MA_SUCCESS) {
        SPDLOG_ERROR("miniaudio: fallback device start failed");
        ma_device_uninit(&dev);
        std::memset(&dev, 0, sizeof(dev));
        return false;
    }

    return true;
}

bool MiniaudioAdapter::startCapture(std::string_view device_id,
                                     ports::AudioCaptureCallback callback) {
    if (!m_impl->context_ok || m_impl->capture_open) return false;

    bool ok = init_device(m_impl->context, m_impl->capture_device,
                          ma_device_type_capture, device_id, true,
                          16000, ma_format_s16, 512,
                          Impl::onCapture, m_impl.get());

    if (ok) {
        m_impl->capture_cb = std::move(callback);
        m_impl->capture_open = true;
        SPDLOG_INFO("miniaudio: capture started ({}Hz mono s16, device={})",
                    16000, device_id.empty() ? "default" : device_id.substr(0, 16));
        return true;
    }

    SPDLOG_ERROR("miniaudio: capture start failed");
    return false;
}

void MiniaudioAdapter::stopCapture() {
    if (!m_impl->capture_open) return;

    ma_device_stop(&m_impl->capture_device);
    ma_device_uninit(&m_impl->capture_device);
    std::memset(&m_impl->capture_device, 0, sizeof(m_impl->capture_device));
    m_impl->capture_cb = nullptr;
    m_impl->capture_open = false;
    SPDLOG_DEBUG("miniaudio: capture stopped");
}

bool MiniaudioAdapter::isCaptureActive() const {
    return m_impl->capture_open;
}

bool MiniaudioAdapter::startPlayback(std::string_view device_id, int sample_rate) {
    if (!m_impl->context_ok || m_impl->playback_open) return false;

    m_impl->playback_buffer.clear();
    m_impl->playback_sample_rate = sample_rate;

    bool ok = init_device(m_impl->context, m_impl->playback_device,
                          ma_device_type_playback, device_id, false,
                          static_cast<ma_uint32>(sample_rate), ma_format_f32, 512,
                          Impl::onPlayback, m_impl.get());

    if (ok) {
        m_impl->playback_open = true;
        SPDLOG_INFO("miniaudio: playback started ({}Hz mono f32, device={})",
                    sample_rate, device_id.empty() ? "default" : device_id.substr(0, 16));
        return true;
    }

    SPDLOG_ERROR("miniaudio: playback start failed");
    return false;
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
    SPDLOG_DEBUG("miniaudio: playback stopped");
}

bool MiniaudioAdapter::isPlaybackActive() const {
    return m_impl->playback_open;
}

} // namespace rook::adapters::audio
