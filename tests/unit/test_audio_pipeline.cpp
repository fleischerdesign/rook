#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include "rook/domain/audio_pipeline.hpp"
#include "tests/mocks/mock_audio_ports.hpp"

using namespace rook::domain;
using namespace rook::ports;
using namespace rook::test;
using namespace std::chrono_literals;

namespace {

template<typename F>
bool waitUntil(F condition, int max_ms = 1000) {
    for (int i = 0; i < max_ms; ++i) {
        if (condition()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

void pushSilence(MockAudioDevicePort& dev, int frames) {
    int16_t silence[512] = {};
    for (int i = 0; i < frames; ++i)
        dev.pushAudio(silence, 512);
}

struct TestEvents {
    std::atomic<int> wake_count{0};
    std::atomic<int> stt_result_count{0};
    std::atomic<int> tts_done_count{0};
    std::atomic<int> state_change_count{0};
    AudioState last_from{AudioState::Inactive};
    AudioState last_to{AudioState::Inactive};
    std::string last_stt_transcript;
    bool last_stt_final = false;
};

AudioPipelineEvents makeEvents(TestEvents& te) {
    return {
        .on_wake = [&](std::string) { te.wake_count.fetch_add(1); },
        .on_stt_result = [&](std::string t, bool f) {
            te.stt_result_count.fetch_add(1);
            te.last_stt_transcript = t;
            te.last_stt_final = f;
        },
        .on_tts_done = [&]() { te.tts_done_count.fetch_add(1); },
        .on_state_change = [&](AudioState from, AudioState to) {
            te.state_change_count.fetch_add(1);
            te.last_from = from;
            te.last_to = to;
        },
    };
}

} // anonymous namespace

TEST(AudioPipelineTest, InitialStateIsInactive) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    EXPECT_EQ(pipeline.state(), AudioState::Inactive);
    EXPECT_FALSE(pipeline.isVoiceEnabled());
    EXPECT_TRUE(pipeline.isMuted());
}

TEST(AudioPipelineTest, EnableTransitionsToWaitingForWake) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    pipeline.unmute();
    pipeline.enable();

    EXPECT_EQ(pipeline.state(), AudioState::WaitingForWake);
    EXPECT_TRUE(pipeline.isVoiceEnabled());
    EXPECT_FALSE(pipeline.isMuted());
    EXPECT_GE(dev.m_capture_start_count.load(), 1);
}

TEST(AudioPipelineTest, EnableWhenWakewordNotReadyStaysInactive) {
    MockWakewordPort ww;
    ww.m_ready.store(false);
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    pipeline.unmute();
    pipeline.enable();

    EXPECT_EQ(pipeline.state(), AudioState::Inactive);
    EXPECT_FALSE(pipeline.isVoiceEnabled());
}

TEST(AudioPipelineTest, WakewordDetectionTransitionsToRecording) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    TestEvents events;
    pipeline.setEvents(makeEvents(events));
    pipeline.unmute();
    pipeline.enable();

    EXPECT_EQ(pipeline.state(), AudioState::WaitingForWake);

    ww.triggerDetection();
    pushSilence(dev, 1);

    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Recording; }));
    EXPECT_EQ(events.wake_count.load(), 1);
    EXPECT_GE(events.state_change_count.load(), 1);
    EXPECT_EQ(events.last_to, AudioState::Recording);
}

TEST(AudioPipelineTest, SilenceTimeoutTransitionsToProcessing) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    TestEvents events;
    pipeline.setEvents(makeEvents(events));
    pipeline.unmute();
    pipeline.enable();

    ww.triggerDetection();
    pushSilence(dev, 1);
    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Recording; }));

    pushSilence(dev, 120);

    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Processing; }, 3000));
    EXPECT_EQ(events.last_to, AudioState::Processing);
    EXPECT_GE(dev.m_capture_stop_count.load(), 0);
}

TEST(AudioPipelineTest, ResponseReadyWithTtsReadyTransitionsToSpeaking) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    TestEvents events;
    pipeline.setEvents(makeEvents(events));
    pipeline.unmute();
    pipeline.enable();

    ww.triggerDetection();
    pushSilence(dev, 1);
    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Recording; }));
    pushSilence(dev, 120);
    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Processing; }, 3000));

    pipeline.onResponseReady("Hallo Welt!");

    EXPECT_EQ(pipeline.state(), AudioState::Speaking);
    EXPECT_EQ(events.last_to, AudioState::Speaking);
    EXPECT_EQ(tts.m_speak_count.load(), 1);
    EXPECT_EQ(tts.m_last_text, "Hallo Welt!");
    EXPECT_GE(dev.m_playback_start_count.load(), 1);
}

TEST(AudioPipelineTest, TtsLastChunkTransitionsToWaitingForWake) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    TestEvents events;
    pipeline.setEvents(makeEvents(events));
    pipeline.unmute();
    pipeline.enable();

    ww.triggerDetection();
    pushSilence(dev, 1);
    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Recording; }));
    pushSilence(dev, 120);
    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Processing; }, 3000));
    pipeline.onResponseReady("Test");

    EXPECT_EQ(pipeline.state(), AudioState::Speaking);

    tts.deliverChunks(3);

    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::WaitingForWake; }, 500));
    EXPECT_EQ(events.tts_done_count.load(), 1);
    EXPECT_GE(dev.m_playback_stop_count.load(), 1);
}

TEST(AudioPipelineTest, MuteTransitionsToInactiveAndStopsCapture) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    pipeline.unmute();
    pipeline.enable();
    EXPECT_EQ(pipeline.state(), AudioState::WaitingForWake);

    pipeline.mute();

    EXPECT_EQ(pipeline.state(), AudioState::Inactive);
    EXPECT_TRUE(pipeline.isMuted());
    EXPECT_GE(dev.m_capture_stop_count.load(), 1);
}

TEST(AudioPipelineTest, UnmuteReturnsToWaitingForWake) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    TestEvents events;
    pipeline.setEvents(makeEvents(events));
    pipeline.unmute();
    pipeline.enable();
    EXPECT_EQ(pipeline.state(), AudioState::WaitingForWake);

    int start_count_before = dev.m_capture_start_count.load();
    pipeline.mute();
    EXPECT_EQ(pipeline.state(), AudioState::Inactive);

    pipeline.unmute();

    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::WaitingForWake; }, 200));
    EXPECT_GE(dev.m_capture_start_count.load(), start_count_before + 1);
}

TEST(AudioPipelineTest, MuteWhileRecordingReturnsToInactive) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    TestEvents events;
    pipeline.setEvents(makeEvents(events));
    pipeline.unmute();
    pipeline.enable();

    ww.triggerDetection();
    pushSilence(dev, 1);
    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Recording; }));

    pipeline.mute();

    EXPECT_EQ(pipeline.state(), AudioState::Inactive);
}

TEST(AudioPipelineTest, DisableStopsEverything) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    pipeline.unmute();
    pipeline.enable();
    EXPECT_EQ(pipeline.state(), AudioState::WaitingForWake);

    pipeline.disable();

    EXPECT_EQ(pipeline.state(), AudioState::Inactive);
    EXPECT_FALSE(pipeline.isVoiceEnabled());
    EXPECT_TRUE(pipeline.isMuted());
}

TEST(AudioPipelineTest, ResponseReadySkipsTtsWhenTtsNotReady) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;
    tts.m_ready.store(false);

    AudioPipeline pipeline(ww, stt, tts, dev);
    TestEvents events;
    pipeline.setEvents(makeEvents(events));
    pipeline.unmute();
    pipeline.enable();

    ww.triggerDetection();
    pushSilence(dev, 1);
    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Recording; }));
    pushSilence(dev, 120);
    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Processing; }, 3000));

    pipeline.onResponseReady("Hallo");

    EXPECT_EQ(pipeline.state(), AudioState::WaitingForWake);
    EXPECT_EQ(events.last_to, AudioState::WaitingForWake);
}

TEST(AudioPipelineTest, ResponseReadySkipsTtsWhenMuted) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    TestEvents events;
    pipeline.setEvents(makeEvents(events));
    pipeline.unmute();
    pipeline.enable();

    ww.triggerDetection();
    pushSilence(dev, 1);
    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Recording; }));
    pushSilence(dev, 120);
    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Processing; }, 3000));

    pipeline.mute();

    EXPECT_EQ(pipeline.state(), AudioState::Inactive);
    EXPECT_TRUE(pipeline.isMuted());
    EXPECT_EQ(tts.m_speak_count.load(), 0);
}

TEST(AudioPipelineTest, StopSpeakingCancelsTtsAndPlayback) {
    MockWakewordPort ww;
    MockSpeechToTextPort stt;
    MockTextToSpeechPort tts;
    MockAudioDevicePort dev;

    AudioPipeline pipeline(ww, stt, tts, dev);
    pipeline.unmute();
    pipeline.enable();

    ww.triggerDetection();
    pushSilence(dev, 1);
    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Recording; }));
    pushSilence(dev, 120);
    EXPECT_TRUE(waitUntil([&] { return pipeline.state() == AudioState::Processing; }, 3000));
    pipeline.onResponseReady("Stop me");

    pipeline.stopSpeaking();

    EXPECT_GE(tts.m_stop_count.load(), 1);
    EXPECT_GE(dev.m_playback_stop_count.load(), 1);
}
