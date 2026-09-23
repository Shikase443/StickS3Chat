#pragma once
#include <array>
#include <string>

enum class Screen { HOME, SETTINGS, TEXT_INPUT_SSID, TEXT_INPUT_PASS };
enum class WifiStatus { IDLE, CONNECTING, CONNECTED, FAILED };
enum class VoiceState { IDLE, RECORDING, STT_PROCESSING, LLM_PROCESSING, TTS_PROCESSING, PLAYING, ERROR };
enum class FaceExpression { NORMAL, SMILE, SURPRISED, MOUTH_MEDIUM, MOUTH_LARGE };
enum class HomeAction { SETTINGS, FORGET };
struct HomeMenuItem { const char* label; HomeAction action; };
inline constexpr std::array<HomeMenuItem,2> HOME_MENU{{{"Config",HomeAction::SETTINGS},{"Forget",HomeAction::FORGET}}};

struct ServiceSettings {
    std::string provider = "openai";
    std::string url;
    std::string model;
    std::string voice;
    std::string api_key;
    std::string language = "Auto";
    std::string instructions;
};
inline constexpr const char* DEFAULT_TTS_INSTRUCTIONS = "Speak in a cheerful and positive tone.";
struct IntegratedSettings {
    std::string url;
    std::string api_key;
    std::string user;
    std::string session_key;
    std::string device_id;
    std::string voice;
    bool correct_transcript = true;
};

struct Settings {
    std::string ssid;
    std::string pass;
    std::string ntp_server = "pool.ntp.org";
    std::string timezone = "UTC0";
    ServiceSettings stt{"openai", "https://api.openai.com/v1/audio/transcriptions", "gpt-4o-transcribe", "", "", "Auto", ""};
    ServiceSettings llm{"openai", "https://api.openai.com/v1/responses", "gpt-5.6-luna", "", "", "Auto", ""};
    ServiceSettings tts{"openai", "https://api.openai.com/v1/audio/speech", "gpt-4o-mini-tts", "marin", "", "Auto", DEFAULT_TTS_INSTRUCTIONS};
    std::string llm_agent = "none";
    std::string llm_session_id;
    std::string connection_mode = "separate";
    IntegratedSettings integrated;
    uint8_t volume_level = 3;
    uint8_t brightness_level = 3;
};
struct AppState {
    Screen screen = Screen::HOME;
    int home_selection = -1;
    bool confirm_forget = false;
    int settings_selection = -1;
    WifiStatus wifi = WifiStatus::IDLE;
    std::string ip;
    bool web_running = false;
    std::string web_password;
    bool time_synced = false;
    int battery_percent = -1;
    VoiceState voice = VoiceState::IDLE;
    FaceExpression face = FaceExpression::NORMAL;
    std::string voice_message;
    std::string voice_caption;
    bool redraw = true;
};
