#include "rook/adapters/audio/sherpa_wakeword_adapter.hpp"

#include <spdlog/spdlog.h>
#include <gio/gio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rook::adapters::audio {

struct SherpaWakewordAdapter::Impl {
    float sensitivity = 0.5f;

    int energy_counter = 0;
    int silence_counter = 0;
    int cooldown = 0;

    static constexpr int k_trigger_threshold = 15;
    static constexpr int k_release_threshold = 8;
    static constexpr int k_cooldown_frames = 25;

    float compute_rms(const int16_t* pcm, std::size_t count) {
        double sum = 0.0;
        for (std::size_t i = 0; i < count; ++i)
            sum += static_cast<double>(pcm[i]) * static_cast<double>(pcm[i]);
        return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
    }
};

SherpaWakewordAdapter::SherpaWakewordAdapter()
    : m_impl(std::make_unique<Impl>())
{
    auto* gs = g_settings_new("io.github.fleischerdesign.Rook");
    m_impl->sensitivity = static_cast<float>(
        g_settings_get_double(gs, "wakeword-sensitivity"));
    g_object_unref(gs);
}

SherpaWakewordAdapter::~SherpaWakewordAdapter() = default;

bool SherpaWakewordAdapter::processFrame(const int16_t* pcm) {
    if (m_impl->cooldown > 0) {
        m_impl->cooldown--;
        return false;
    }

    float rms = m_impl->compute_rms(pcm, 512);
    float threshold = 800.0f + (1.0f - m_impl->sensitivity) * 3000.0f;

    if (rms > threshold) {
        m_impl->energy_counter++;
        m_impl->silence_counter = 0;
    } else {
        if (m_impl->energy_counter > 0)
            m_impl->silence_counter++;
    }

    if (m_impl->energy_counter > Impl::k_trigger_threshold &&
        m_impl->silence_counter > Impl::k_release_threshold) {
        SPDLOG_DEBUG("SherpaWakewordAdapter: wakeword triggered (rms={:.0f})", rms);
        reset();
        return true;
    }

    if (m_impl->silence_counter > Impl::k_trigger_threshold * 3) {
        reset();
    }

    return false;
}

void SherpaWakewordAdapter::reset() {
    m_impl->energy_counter = 0;
    m_impl->silence_counter = 0;
    m_impl->cooldown = Impl::k_cooldown_frames;
}

void SherpaWakewordAdapter::setSensitivity(float value) {
    m_impl->sensitivity = std::clamp(value, 0.0f, 1.0f);
    auto* gs = g_settings_new("io.github.fleischerdesign.Rook");
    g_settings_set_double(gs, "wakeword-sensitivity",
                          static_cast<double>(m_impl->sensitivity));
    g_object_unref(gs);
}

std::string SherpaWakewordAdapter::defaultModelPath() {
    return {};
}

std::string SherpaWakewordAdapter::defaultModelUrl() {
    return {};
}

void SherpaWakewordAdapter::downloadModel(ProgressFn, DoneFn on_done) {
    if (on_done) on_done(true);
}

} // namespace rook::adapters::audio
