#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <semaphore>
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

enum class VoiceMode {
    Wakeword,
    LiveChat,
};

struct AudioPipelineEvents {
    std::function<void(std::string keyword)> on_wake;
    std::function<void(std::string transcript, bool is_final, VoiceMode mode)> on_stt_result;
    std::function<void()> on_tts_done;
    std::function<void(ports::AudioState old_state, ports::AudioState new_state)> on_state_change;
    std::function<void(std::string partial)> on_partial_stt;
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

    void setMode(VoiceMode mode);
    VoiceMode mode() const { return m_mode.load(std::memory_order_acquire); }
    void startLiveMode();
    void stopLiveMode();
    void onBargeInDetected();

    ports::AudioState state() const { return m_state.load(std::memory_order_acquire); }
    bool isVoiceEnabled() const { return m_enabled.load(std::memory_order_acquire); }
    bool isMuted() const { return m_muted.load(std::memory_order_acquire); }
    bool isLiveModeActive() const { return m_live_mode_active.load(std::memory_order_acquire); }

    void onResponseReady(std::string_view text);

    void stopSpeaking();

private:
    void runWorker();
    void processWakeword(const int16_t* pcm, std::size_t);
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

    std::atomic<VoiceMode> m_mode{VoiceMode::Wakeword};
    std::atomic<bool> m_live_mode_active{false};
    std::atomic<bool> m_speaking_setup{false};
    std::atomic<int> m_barge_in_threshold{500};
    std::counting_semaphore<1024> m_data_sem{0};

    std::recursive_mutex m_capture_mutex;

    int m_silence_counter = 0;
    int m_recording_frames = 0;
    std::string m_last_partial;

    static constexpr int k_silence_threshold = 100;
    static constexpr int k_silence_max = 78;
    static constexpr int k_live_silence_max = 78;
    static constexpr int k_max_recording_frames = 750;
};

} // namespace rook::domain
