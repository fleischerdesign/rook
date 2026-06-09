#include "rook/domain/audio_pipeline.hpp"

#include "rook/ports/wakeword_port.hpp"
#include "rook/ports/text_to_speech_port.hpp"
#include "rook/ports/audio_device_port.hpp"

#include <spdlog/spdlog.h>
#include <gio/gio.h>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace rook::domain {

AudioPipeline::AudioPipeline(ports::WakewordPort& wakeword,
                             ports::SpeechToTextPort& stt,
                             ports::TextToSpeechPort& tts,
                             ports::AudioDevicePort& audio_device)
    : m_wakeword(wakeword)
    , m_stt(stt)
    , m_tts(tts)
    , m_audio_device(audio_device)
{
    m_recording_buffer.reserve(16000 * 15);
}

AudioPipeline::~AudioPipeline() {
    disable();
}

void AudioPipeline::setEvents(AudioPipelineEvents events) {
    m_events = std::move(events);
}

void AudioPipeline::enable() {
    if (m_enabled.load(std::memory_order_acquire)) return;

    auto mode = m_mode.load(std::memory_order_acquire);
    if (mode == VoiceMode::Wakeword && !m_wakeword.isReady()) {
        SPDLOG_WARN("AudioPipeline: wakeword engine not ready, wakeword detection disabled");
    }

    m_enabled.store(true, std::memory_order_release);
    SPDLOG_INFO("AudioPipeline: voice enabled (mode={})",
                 mode == VoiceMode::Wakeword ? "wakeword" : "live");

    if (!m_muted.load(std::memory_order_acquire)) {
        startListening();
    } else {
        transition(ports::AudioState::Inactive);
    }
}

void AudioPipeline::disable() {
    if (!m_enabled.load(std::memory_order_acquire)) return;

    m_muted.store(true, std::memory_order_release);
    m_enabled.store(false, std::memory_order_release);
    m_live_mode_active.store(false, std::memory_order_release);
    stopListening();
    m_audio_device.stopCapture();
    m_audio_device.stopPlayback();
    transition(ports::AudioState::Inactive);

    SPDLOG_INFO("AudioPipeline: voice disabled");
}

void AudioPipeline::setMode(VoiceMode mode) {
    m_mode.store(mode, std::memory_order_release);
}

void AudioPipeline::startLiveMode() {
    setMode(VoiceMode::LiveChat);
    m_live_mode_active.store(true, std::memory_order_release);
    unmute();
    enable();
}

void AudioPipeline::stopLiveMode() {
    m_live_mode_active.store(false, std::memory_order_release);
    setMode(VoiceMode::Wakeword);

    auto state = m_state.load(std::memory_order_acquire);
    if (state == ports::AudioState::Recording && !m_recording_buffer.empty()) {
        auto audio = std::move(m_recording_buffer);
        m_recording_buffer.clear();
        transition(ports::AudioState::Processing);
        SPDLOG_INFO("AudioPipeline: live-chat recording stopped, transcribing {} samples", audio.size());

        auto cur_mode = VoiceMode::LiveChat;
        m_stt.transcribe(audio.data(), audio.size(), 16000,
            [this, cur_mode](ports::SttResult result) {
                if (m_events.on_stt_result)
                    m_events.on_stt_result(std::move(result.transcript),
                                           result.is_final, cur_mode);
            });
        return;
    }

    if (m_enabled.load(std::memory_order_acquire) &&
        !m_muted.load(std::memory_order_acquire)) {
        stopListening();
        startListening();
    }
}

void AudioPipeline::onBargeInDetected() {
    auto state = m_state.load(std::memory_order_acquire);
    if (state != ports::AudioState::Speaking) return;
    if (m_mode.load(std::memory_order_acquire) != VoiceMode::LiveChat) return;

    SPDLOG_INFO("AudioPipeline: barge-in detected");
    stopSpeaking();
    m_recording_buffer.clear();
    m_silence_counter = 0;
    m_recording_frames = 0;
    transition(ports::AudioState::Recording);
}

void AudioPipeline::mute() {
    if (m_muted.exchange(true, std::memory_order_acq_rel)) return;

    stopListening();
    m_audio_device.stopCapture();
    m_live_mode_active.store(false, std::memory_order_release);
    transition(ports::AudioState::Inactive);
    SPDLOG_DEBUG("AudioPipeline: muted");
}

void AudioPipeline::unmute() {
    if (!m_muted.exchange(false, std::memory_order_acq_rel)) return;

    if (m_enabled.load(std::memory_order_acquire))
        startListening();
    SPDLOG_DEBUG("AudioPipeline: unmuted");
}

void AudioPipeline::startListening() {
    stopListening();
    m_ring_buffer.clear();
    m_voice_active.store(true, std::memory_order_release);

    auto* s = g_settings_new("io.github.fleischerdesign.Rook");
    char* mic_device = g_settings_get_string(s, "microphone-device");
    std::string device_id(mic_device ? mic_device : "");
    g_free(mic_device);
    g_object_unref(s);

    bool capture_ok = m_audio_device.startCapture(device_id,
        [this](const int16_t* pcm, std::size_t frame_count) {
            m_ring_buffer.write(pcm, frame_count);
        });

    if (!capture_ok) {
        SPDLOG_ERROR("AudioPipeline: capture start failed, voice unavailable");
        m_voice_active.store(false, std::memory_order_release);
        transition(ports::AudioState::Inactive);
        return;
    }

    m_worker_running.store(true, std::memory_order_release);
    m_worker = std::jthread(&AudioPipeline::runWorker, this);

    auto mode = m_mode.load(std::memory_order_acquire);
    SPDLOG_INFO("AudioPipeline: capture started, worker running (mode={})",
                mode == VoiceMode::LiveChat ? "live" : "wakeword");

    if (mode == VoiceMode::LiveChat) {
        m_recording_buffer.clear();
        m_silence_counter = 0;
        m_recording_frames = 0;
        transition(ports::AudioState::Recording);
        SPDLOG_INFO("AudioPipeline: live-chat recording started (mic active)");
    } else {
        transition(ports::AudioState::WaitingForWake);
    }
}

void AudioPipeline::stopListening() {
    m_voice_active.store(false, std::memory_order_release);
    m_worker_running.store(false, std::memory_order_release);
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_audio_device.stopCapture();
}

void AudioPipeline::runWorker() {
    auto frame_size = m_wakeword.frameSize();
    std::vector<int16_t> frame(frame_size);

    while (m_worker_running.load(std::memory_order_acquire)) {
        auto state = m_state.load(std::memory_order_acquire);

        if (state == ports::AudioState::Inactive ||
            state == ports::AudioState::Processing) {
            m_ring_buffer.drain(512);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (state == ports::AudioState::Speaking) {
            auto mode = m_mode.load(std::memory_order_acquire);
            if (mode == VoiceMode::LiveChat) {
                std::size_t pos = 0;
                while (pos < frame_size && m_worker_running.load(std::memory_order_acquire)) {
                    auto n = m_ring_buffer.read(frame.data() + pos, frame_size - pos);
                    if (n == 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                        continue;
                    }
                    pos += n;
                }
                if (pos < frame_size) continue;

                double sum = 0.0;
                for (std::size_t i = 0; i < frame_size; ++i)
                    sum += static_cast<double>(frame[i]) * static_cast<double>(frame[i]);
                double rms = std::sqrt(sum / static_cast<double>(frame_size));

                if (rms > k_barge_in_threshold) {
                    onBargeInDetected();
                }
            } else {
                m_ring_buffer.drain(512);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }

        std::size_t pos = 0;
        while (pos < frame_size && m_worker_running.load(std::memory_order_acquire)) {
            auto n = m_ring_buffer.read(frame.data() + pos, frame_size - pos);
            if (n == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                continue;
            }
            pos += n;
        }
        if (pos < frame_size) continue;

        state = m_state.load(std::memory_order_acquire);

        if (state == ports::AudioState::WaitingForWake) {
            processWakeword(frame.data(), frame_size);
        } else if (state == ports::AudioState::Recording) {
            processRecording(frame.data(), frame_size);
        }
    }
}

void AudioPipeline::processWakeword(const int16_t* pcm, std::size_t) {
    if (m_wakeword.processFrame(pcm)) {
        SPDLOG_INFO("AudioPipeline: wakeword detected");
        m_recording_buffer.clear();
        m_silence_counter = 0;
        m_recording_frames = 0;
        m_wakeword.reset();

        transition(ports::AudioState::Recording);

        if (m_events.on_wake) {
            m_events.on_wake("default");
        }
    }
}

void AudioPipeline::processRecording(const int16_t* pcm, std::size_t count) {
    m_recording_buffer.insert(m_recording_buffer.end(), pcm, pcm + count);
    m_recording_frames++;

    auto mode = m_mode.load(std::memory_order_acquire);
    if (mode == VoiceMode::LiveChat) return;

    double sum = 0.0;
    for (std::size_t i = 0; i < count; ++i)
        sum += static_cast<double>(pcm[i]) * static_cast<double>(pcm[i]);
    double rms = std::sqrt(sum / static_cast<double>(count));

    if (rms < k_silence_threshold) {
        m_silence_counter++;
    } else {
        m_silence_counter = 0;
    }

    bool timeout = m_silence_counter >= k_silence_max;
    bool max_dur = m_recording_frames >= k_max_recording_frames;

    if (timeout || max_dur) {
        std::vector<int16_t> audio = std::move(m_recording_buffer);
        m_recording_buffer.clear();

        transition(ports::AudioState::Processing);

        int sr = 16000;
        auto cur_mode = m_mode.load(std::memory_order_acquire);
        m_stt.transcribe(audio.data(), audio.size(), sr,
            [this, cur_mode](ports::SttResult result) {
                if (m_events.on_stt_result)
                    m_events.on_stt_result(std::move(result.transcript),
                                           result.is_final, cur_mode);
            });

        SPDLOG_INFO("AudioPipeline: recording done ({} samples, reason={})",
                     audio.size(), timeout ? "silence" : "max_dur");
    }
}

void AudioPipeline::onResponseReady(std::string_view text) {
    auto state = m_state.load(std::memory_order_acquire);
    if (state != ports::AudioState::Processing) return;

    if (m_tts.isReady() && !m_muted.load(std::memory_order_acquire)) {
        startSpeaking(std::string(text));
    } else {
        auto mode = m_mode.load(std::memory_order_acquire);
        if (mode == VoiceMode::LiveChat) {
            m_recording_buffer.clear();
            m_silence_counter = 0;
            m_recording_frames = 0;
            transition(ports::AudioState::Recording);
        } else {
            transition(ports::AudioState::WaitingForWake);
        }
    }
}

void AudioPipeline::startSpeaking(std::string text) {
    transition(ports::AudioState::Speaking);

    auto* s = g_settings_new("io.github.fleischerdesign.Rook");
    char* spk_device = g_settings_get_string(s, "speaker-device");
    std::string device_id(spk_device ? spk_device : "");
    g_free(spk_device);
    g_object_unref(s);

    m_audio_device.startPlayback(device_id, 22050);

    m_tts.speak(text, [this](const float* pcm, std::size_t sample_count,
                              int sample_rate, bool is_last) {
        (void)sample_rate;
        m_audio_device.writePlayback(pcm, sample_count);
        if (is_last) {
            SPDLOG_INFO("AudioPipeline: TTS finished");
            m_audio_device.stopPlayback();
            if (m_events.on_tts_done) {
                m_events.on_tts_done();
            }
            if (m_enabled.load(std::memory_order_acquire) &&
                !m_muted.load(std::memory_order_acquire)) {

                auto mode = m_mode.load(std::memory_order_acquire);
                if (mode == VoiceMode::LiveChat) {
                    m_recording_buffer.clear();
                    m_silence_counter = 0;
                    m_recording_frames = 0;
                    transition(ports::AudioState::Recording);
                } else {
                    startListening();
                }
            }
        }
    });
}

void AudioPipeline::stopSpeaking() {
    m_tts.stop();
    m_audio_device.stopPlayback();
}

void AudioPipeline::transition(ports::AudioState to) {
    auto from = m_state.exchange(to, std::memory_order_acq_rel);
    if (from == to) return;

    SPDLOG_INFO("AudioPipeline: state {} -> {}", static_cast<int>(from), static_cast<int>(to));

    if (m_events.on_state_change)
        m_events.on_state_change(from, to);
}

} // namespace rook::domain
