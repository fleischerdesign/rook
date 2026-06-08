#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "rook/core/lockfree_ring_buffer.hpp"
#include "rook/ports/audio_port.hpp"
#include "rook/ports/speech_to_text_port.hpp"

namespace rook::ports {
class WakewordPort;
class SpeechToTextPort;
class TextToSpeechPort;
class AudioDevicePort;
}

namespace rook::domain {

struct AudioPipelineEvents {
    std::function<void(std::string keyword)> on_wake;
    std::function<void(std::string transcript, bool is_final)> on_stt_result;
    std::function<void()> on_tts_done;
    std::function<void(ports::AudioState old_state, ports::AudioState new_state)> on_state_change;
};

class AudioPipeline {
public:
    AudioPipeline(ports::WakewordPort& wakeword,
                  ports::SpeechToTextPort& stt,
                  ports::TextToSpeechPort& tts,
                  ports::AudioDevicePort& audio_device);
    ~AudioPipeline();

    AudioPipeline(const AudioPipeline&) = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;

    void setEvents(AudioPipelineEvents events);

    void enable();
    void disable();
    void mute();
    void unmute();

    ports::AudioState state() const { return m_state.load(std::memory_order_acquire); }
    bool isVoiceEnabled() const { return m_enabled.load(std::memory_order_acquire); }
    bool isMuted() const { return m_muted.load(std::memory_order_acquire); }

    void onResponseReady(std::string_view text);

    void stopSpeaking();

private:
    void runWorker();
    void processWakeword(const int16_t* pcm, std::size_t count);
    void processRecording(const int16_t* pcm, std::size_t count);
    void transition(ports::AudioState to);
    void startListening();
    void stopListening();
    void startSpeaking(std::string text);

    ports::WakewordPort& m_wakeword;
    ports::SpeechToTextPort& m_stt;
    ports::TextToSpeechPort& m_tts;
    ports::AudioDevicePort& m_audio_device;

    AudioPipelineEvents m_events;

    core::SpScRingBuffer<int16_t> m_ring_buffer;
    std::vector<int16_t> m_recording_buffer;

    std::jthread m_worker;
    std::atomic<bool> m_worker_running{false};
    std::atomic<bool> m_voice_active{false};

    std::atomic<ports::AudioState> m_state{ports::AudioState::Inactive};
    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_muted{true};

    std::atomic<bool> m_listening{false};
    std::atomic<bool> m_speaking{false};

    int m_silence_counter = 0;
    int m_recording_frames = 0;

    static constexpr int k_silence_threshold = 100;
    static constexpr int k_silence_max = 60;
    static constexpr int k_max_recording_frames = 750;
};

} // namespace rook::domain
