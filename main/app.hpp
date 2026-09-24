#pragma once
#include "display.hpp"
#include "face_store.hpp"
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
    void updateRolling();
    void enterRolling();
    void exitRolling();
    void markActivity();
    static void saveWebSettings(void*, const Settings&);
    AppState state_;
    Settings settings_;
    SettingsStore store_;
    FaceStore face_store_;
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
    bool menu_dirty_=false;
    bool face_dirty_=false;
    bool voice_dirty_=false;
    FaceExpression previous_face_=FaceExpression::NORMAL;
    std::string previous_date_;
    std::string previous_minute_;
    bool rolling_=false;
    int64_t last_activity_us_=0;
    int64_t last_rolling_frame_us_=0;
    float face_x_=0.0f, face_y_=0.0f;      // 中心座標（画面全体 x=0..134 y=0..239）
    float vel_x_=0.0f, vel_y_=0.0f;
    float prev_face_x_=0.0f, prev_face_y_=0.0f;
    float gravity_x_=0.0f, gravity_y_=0.0f, gravity_z_=0.0f; // LPF重力（IMU座標）
    float physical_angular_velocity_=0.0f;  // deg/s
    float physical_angle_=0.0f;             // deg
    float support_nx_=0.0f, support_ny_=0.0f; // 支持面の内向き法線
    bool has_support_=false;
    int64_t last_shake_us_=0;
    bool rolling_moving_=false;
    int64_t stopped_since_us_=0;
    float extra_angle_=0.0f;
    float extra_target_=0.0f;
    int64_t extra_start_us_=0;
    bool extra_active_=false;
    int64_t last_extra_check_us_=0;
    int64_t last_extra_done_us_=0;
    float extra_start_angle_=0.0f;   // 追加回転開始時の角度
    float extra_rotation_dir_=0.0f;  // 追加回転開始時の方向(+1/-1)
    int64_t extra_stable_since_us_=0; // 明確な転がりの継続開始時刻
};
