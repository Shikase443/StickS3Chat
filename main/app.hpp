#pragma once
#include "display.hpp"
#include "settings_store.hpp"
#include "time_manager.hpp"
#include "web_server.hpp"
#include "wifi_manager.hpp"
#include "voice_chat.hpp"
#include <string>

class App {
public:
    void begin();
    void update();
private:
    void normalInput(bool a, bool b);
    void textInput(bool a);
    void sync();
    void enterSettings();
    void leaveSettings();
    static void saveWebSettings(void*, const Settings&);
    AppState state_;
    Settings settings_;
    SettingsStore store_;
    WifiManager wifi_;
    TimeManager time_;
    WebServer web_;
    VoiceChat voice_;
    TextInput input_;
    Display display_;
    int64_t volume_changed_us_=0;
    bool volume_dirty_=false;
    int64_t next_smile_us_=0;
    int64_t smile_until_us_=0;
    int64_t last_battery_poll_us_=0;
    bool home_full_dirty_=true;
    bool wifi_dirty_=false;
    bool battery_dirty_=false;
    bool date_dirty_=false;
    bool clock_dirty_=false;
    bool menu_dirty_=false;
    bool face_dirty_=false;
    bool voice_dirty_=false;
    FaceExpression previous_face_=FaceExpression::NORMAL;
    std::string previous_date_;
    std::string previous_minute_;
};
