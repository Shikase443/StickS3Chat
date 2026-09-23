#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include "app_types.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class HttpReader;

class VoiceChat {
public:
    bool begin();
    void tick();
    void update(bool pressed, bool released, bool wifi_connected, const Settings& settings);
    VoiceState state() const { return state_.load(); }
    uint8_t mouthLevel() const { return mouth_level_.load(); }
    std::string message() const;
    std::string caption() const;
    void clearHistory();
    size_t stackLowWater() const { return stack_low_water_.load(); }
    size_t freeHeap() const { return free_heap_.load(); }
private:
    struct Turn { std::string role, text; };
    static void taskEntry(void*);
    void taskLoop();
    bool startRecording();
    void pumpRecording(bool held);
    void finishRecording();
    bool transcribe(const Settings&, const int16_t*, size_t, std::string&, std::string& error);
    bool complete(const Settings&, const std::string&, std::string&);
    bool synthesize(const Settings&, const std::string&, std::vector<uint8_t>&, bool& raw_pcm, std::string& error);
    bool synthesizeAndPlay(const Settings&, const std::string&, std::string& error);
    bool synthesizeGeminiAndPlay(const Settings&, const std::string&, std::string& error);
    bool integrated(const Settings&, const int16_t*, size_t, std::string&, std::string&);
    bool playStream(HttpReader&, size_t bytes_remaining, bool bounded);
    bool play(const std::vector<uint8_t>&, bool raw_pcm);
    void setState(VoiceState, const std::string& = {});
    void fail(const std::string&);
    void startCaption(const std::string&);
    void advanceCaption();
    void finishCaption();
    void stopCaption();
    void tickCaption();

    static constexpr uint32_t SAMPLE_RATE = 16000;
    static constexpr size_t BLOCK_SAMPLES = 1024;
    static constexpr size_t MAX_SAMPLES = SAMPLE_RATE * 30;
    std::atomic<VoiceState> state_{VoiceState::IDLE};
    std::atomic<uint8_t> mouth_level_{0};
    std::atomic<bool> cancel_requested_{false};
    mutable std::mutex mutex_;
    std::string message_;
    std::string caption_;
    std::string caption_target_;
    bool caption_active_ = false;
    bool caption_playback_finished_ = false;
    int64_t caption_next_us_ = 0;
    int64_t caption_clear_us_ = 0;
    Settings pending_settings_;
    int16_t* recording_ = nullptr;
    size_t recording_samples_ = 0;
    int16_t* capture_ = nullptr;
    int16_t* pending_[2]{};
    size_t head_ = 0, pending_count_ = 0, submitted_ = 0;
    bool release_seen_ = false;
    TaskHandle_t task_ = nullptr;
    std::deque<Turn> history_;
    std::string integrated_session_;
    std::string configured_session_;
    std::atomic<size_t> stack_low_water_{0};
    std::atomic<size_t> free_heap_{0};
};
