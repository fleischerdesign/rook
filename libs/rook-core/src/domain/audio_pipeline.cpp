#include "rook/domain/audio_pipeline.hpp"

#include "rook/ports/wakeword_port.hpp"
#include "rook/ports/text_to_speech_port.hpp"
#include "rook/ports/audio_device_port.hpp"

#include <spdlog/spdlog.h>
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
    if (!m_wakeword.isReady()) {
        SPDLOG_WARN("AudioPipeline: wakeword engine not ready");
        return;
    }

    m_enabled.store(true, std::memory_order_release);
    SPDLOG_DEBUG("AudioPipeline: voice enabled");

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
    stopListening();
    m_audio_device.stopCapture();
    m_audio_device.stopPlayback();
    transition(ports::AudioState::Inactive);

    SPDLOG_DEBUG("AudioPipeline: voice disabled");
}

void AudioPipeline::mute() {
    if (m_muted.exchange(true, std::memory_order_acq_rel)) return;

    stopListening();
    m_audio_device.stopCapture();
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

    m_audio_device.startCapture("", [this](const int16_t* pcm, std::size_t frame_count) {
        m_ring_buffer.write(pcm, frame_count);
    });

    m_worker_running.store(true, std::memory_order_release);
    m_worker = std::jthread(&AudioPipeline::runWorker, this);

    transition(ports::AudioState::WaitingForWake);
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
            state == ports::AudioState::Processing ||
            state == ports::AudioState::Speaking) {
            m_ring_buffer.drain(512);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
        SPDLOG_DEBUG("AudioPipeline: wakeword detected");
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
        m_stt.transcribe(audio.data(), audio.size(), sr,
            [this](ports::SttResult result) {
                if (m_events.on_stt_result)
                    m_events.on_stt_result(std::move(result.transcript), result.is_final);
            });

        SPDLOG_DEBUG("AudioPipeline: recording done ({} samples, reason={})",
                     audio.size(), timeout ? "silence" : "max_dur");
    }
}

void AudioPipeline::onResponseReady(std::string_view text) {
    auto state = m_state.load(std::memory_order_acquire);
    if (state != ports::AudioState::Processing) return;

    if (m_tts.isReady() && !m_muted.load(std::memory_order_acquire)) {
        startSpeaking(std::string(text));
    } else {
        transition(ports::AudioState::WaitingForWake);
    }
}

void AudioPipeline::startSpeaking(std::string text) {
    transition(ports::AudioState::Speaking);

    m_audio_device.startPlayback("", 22050);

    m_tts.speak(text, [this](const float* pcm, std::size_t sample_count,
                              int sample_rate, bool is_last) {
        (void)sample_rate;
        m_audio_device.writePlayback(pcm, sample_count);
        if (is_last) {
            SPDLOG_DEBUG("AudioPipeline: TTS finished");
            m_audio_device.stopPlayback();
            if (m_events.on_tts_done) {
                m_events.on_tts_done();
            }
            if (m_enabled.load(std::memory_order_acquire) &&
                !m_muted.load(std::memory_order_acquire)) {
                startListening();
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

    SPDLOG_DEBUG("AudioPipeline: {} -> {}", static_cast<int>(from), static_cast<int>(to));

    if (m_events.on_state_change)
        m_events.on_state_change(from, to);
}

} // namespace rook::domain
