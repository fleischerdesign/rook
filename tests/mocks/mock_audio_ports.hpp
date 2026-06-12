#pragma once

#include "rook/ports/wakeword_port.hpp"
#include "rook/ports/speech_to_text_port.hpp"
#include "rook/ports/text_to_speech_port.hpp"
#include "rook/ports/audio_device_port.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rook::test {

class MockAudioDevicePort final : public rook::ports::AudioDevicePort {
public:
    std::vector<ports::DeviceInfo> enumerateInputs() const override {
        return {{"default", "Mock Mic", {}, true}};
    }
    std::vector<ports::DeviceInfo> enumerateOutputs() const override {
        return {{"default", "Mock Speaker", {}, true}};
    }

    bool startCapture(std::string_view device_id,
                      ports::AudioCaptureCallback callback) override {
        (void)device_id;
        m_capture_callback = std::move(callback);
        m_capture_active.store(true);
        m_capture_start_count++;
        return true;
    }

    void stopCapture() override {
        m_capture_callback = nullptr;
        m_capture_active.store(false);
        m_capture_stop_count++;
    }

    bool isCaptureActive() const override {
        return m_capture_active.load();
    }

    bool startPlayback(std::string_view device_id, int sample_rate) override {
        (void)device_id; (void)sample_rate;
        m_playback_active.store(true);
        m_playback_drained.store(false);
        m_playback_start_count++;
        return true;
    }

    bool writePlayback(const float* pcm, std::size_t sample_count) override {
        (void)pcm;
        m_playback_samples_written += sample_count;
        return true;
    }

    void stopPlayback() override {
        m_playback_active.store(false);
        m_playback_stop_count++;
    }

    bool isPlaybackDrained() const override {
        return m_playback_drained.load();
    }

    void finishPlayback() override {
        m_playback_drained.store(true);
    }

    bool isPlaybackActive() const override {
        return m_playback_active.load();
    }

    void setCaptureVolume(float factor) override {
        m_capture_volume.store(factor);
    }

    float captureVolume() const override {
        return m_capture_volume.load();
    }

    float level() const override {
        return m_level.load();
    }

    void pushAudio(const int16_t* pcm, std::size_t count) {
        if (m_capture_callback)
            m_capture_callback(pcm, count);
    }

    std::atomic<int> m_capture_start_count{0};
    std::atomic<int> m_capture_stop_count{0};
    std::atomic<int> m_playback_start_count{0};
    std::atomic<int> m_playback_stop_count{0};
    std::atomic<std::size_t> m_playback_samples_written{0};
    std::atomic<float> m_capture_volume{1.0f};
    std::atomic<float> m_level{-60.0f};
    std::atomic<bool> m_playback_drained{false};

private:
    ports::AudioCaptureCallback m_capture_callback;
    std::atomic<bool> m_capture_active{false};
    std::atomic<bool> m_playback_active{false};
};

class MockWakewordPort final : public rook::ports::WakewordPort {
public:
    std::string id() const override { return "mock_wakeword"; }
    std::string engineName() const override { return "Mock Wakeword"; }

    bool isReady() const override { return m_ready.load(); }
    bool needsKey() const override { return false; }
    std::size_t frameSize() const override { return m_frame_size; }

    bool processFrame(const int16_t* pcm) override {
        (void)pcm;
        if (m_detect_on_next && m_detections_remaining > 0) {
            m_detections_remaining--;
            m_detect_on_next = false;
            return true;
        }
        return false;
    }

    void reset() override {
        m_detect_on_next = false;
        m_detections_remaining = 0;
    }

    void setSensitivity(float value) override {
        m_sensitivity = value;
    }

    void triggerDetection() {
        m_detect_on_next = true;
        m_detections_remaining = 1;
    }

    std::atomic<bool> m_ready{true};
    std::size_t m_frame_size = 512;
    bool m_detect_on_next = false;
    int m_detections_remaining = 0;
    float m_sensitivity = 0.5f;
};

class MockSpeechToTextPort final : public rook::ports::SpeechToTextPort {
public:
    std::string id() const override { return "mock_stt"; }
    std::string engineName() const override { return "Mock STT"; }

    bool isReady() const override { return m_ready.load(); }

    void transcribe(const int16_t* audio, std::size_t sample_count,
                    int sample_rate,
                    std::function<void(ports::SttResult)> on_result) override {
        m_last_audio_size = sample_count;
        m_last_sample_rate = sample_rate;
        m_on_result = std::move(on_result);
        (void)audio;

        if (m_result.has_value()) {
            auto cb = m_on_result;
            m_on_result = nullptr;
            auto r = *m_result;
            cb(r);
        }
    }

    void cancel() override {
        m_on_result = nullptr;
        m_cancel_count++;
    }

    void setModel(std::string_view path) override {
        m_model_path = path;
    }

    std::vector<std::string> availableModels() const override {
        return {"small", "medium", "large"};
    }

    void setNextResult(ports::SttResult result) {
        m_result = std::move(result);
    }

    void deliverResult() {
        if (m_on_result && m_result.has_value()) {
            auto cb = m_on_result;
            m_on_result = nullptr;
            cb(*m_result);
        }
    }

    std::atomic<bool> m_ready{true};
    std::optional<ports::SttResult> m_result;
    std::function<void(ports::SttResult)> m_on_result;
    std::size_t m_last_audio_size = 0;
    int m_last_sample_rate = 0;
    std::string m_model_path;
    std::atomic<int> m_cancel_count{0};
};

class MockTextToSpeechPort final : public rook::ports::TextToSpeechPort {
public:
    std::string id() const override { return "mock_tts"; }
    std::string engineName() const override { return "Mock TTS"; }

    bool isReady() const override { return m_ready.load(); }

    std::vector<ports::VoiceInfo> listVoices() const override {
        return {{"de_thorsten", "Thorsten", "de"}, {"de_eva", "Eva", "de"}};
    }

    void setVoice(std::string_view voice_id) override {
        m_voice = voice_id;
    }

    void speak(std::string_view text,
               std::function<void(const float* pcm, std::size_t sample_count,
                                  int sample_rate, bool is_last)> on_chunk) override {
        m_last_text = text;
        m_on_chunk = std::move(on_chunk);
        m_speak_count++;
    }

    void stop() override {
        m_on_chunk = nullptr;
        m_stop_count++;
    }

    void deliverChunks(int count) {
        if (!m_on_chunk) return;
        float buf[128] = {};
        for (int i = 0; i < count - 1; ++i)
            m_on_chunk(buf, 128, 22050, false);
        m_on_chunk(buf, 128, 22050, true);
    }

    std::atomic<bool> m_ready{true};
    std::string m_voice;
    std::string m_last_text;
    std::function<void(const float*, std::size_t, int, bool)> m_on_chunk;
    std::atomic<int> m_speak_count{0};
    std::atomic<int> m_stop_count{0};
};

} // namespace rook::test
