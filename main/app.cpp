#include "app.hpp"
#include "M5Unified.hpp"
#include "esp_random.h"
#include "esp_timer.h"
#include <algorithm>
#include <cmath>
#include <ctime>

namespace {
std::string dateKey(bool synced){if(!synced)return "----/--/--";char value[16];std::time_t now=std::time(nullptr);std::tm local{};localtime_r(&now,&local);std::strftime(value,sizeof(value),"%Y/%m/%d",&local);return value;}
std::string minuteKey(bool synced){if(!synced)return "--:--";char value[8];std::time_t now=std::time(nullptr);std::tm local{};localtime_r(&now,&local);std::strftime(value,sizeof(value),"%H:%M",&local);return value;}
bool mouthFace(FaceExpression face){return face==FaceExpression::NORMAL||face==FaceExpression::MOUTH_MEDIUM||face==FaceExpression::MOUTH_LARGE;}
// IMU重力 → 画面座標重力（符号は実機360°回転で確定）
// M5Stick S3: IMU X=水平, IMU Y=垂直, IMU Z=画面法線
constexpr float IMU_TO_SCREEN_X=-1.0f; // 実機で逆なら+1.0f
constexpr float IMU_TO_SCREEN_Y=1.0f;  // 実機で逆なら-1.0f
void mapImuGravityToScreen(float gx,float gy,float& sx,float& sy){sx=IMU_TO_SCREEN_X*gx;sy=IMU_TO_SCREEN_Y*gy;}
// Rolling screensaver physics (unified 2D model)
constexpr int64_t ROLLING_IDLE_US=10000000;
constexpr float ROLLING_SCALE=1.0f;
constexpr int ROLLING_FACE=114;
constexpr float ROLLING_RADIUS=ROLLING_FACE/2.0f;   // 57
constexpr int ROLLING_SCREEN_W=135;
constexpr int ROLLING_AREA_TOP=18, ROLLING_AREA_BOTTOM=210;
constexpr int ROLLING_H_OVERFLOW=15;
// Physics bounds (center coords, full screen x=0..134 y=0..239)
constexpr float ROLLING_X_MIN=ROLLING_RADIUS-ROLLING_H_OVERFLOW;            // 42
constexpr float ROLLING_X_MAX=ROLLING_SCREEN_W-ROLLING_RADIUS+ROLLING_H_OVERFLOW; // 93
constexpr float ROLLING_Y_MIN=ROLLING_AREA_TOP+ROLLING_RADIUS;              // 75
constexpr float ROLLING_Y_MAX=ROLLING_AREA_BOTTOM-ROLLING_RADIUS;           // 153
// Gravity / damping
constexpr float GRAVITY_GAIN=1400.0f;
constexpr float DAMPING_COEFF=0.6f;
constexpr float MAX_SPEED=700.0f;
constexpr float WALL_RESTITUTION=0.35f;
constexpr float WALL_FRICTION=0.06f;
constexpr float STOP_SPEED=6.0f;
constexpr float CONTACT_EPSILON=2.0f;
constexpr float CONTACT_HYSTERESIS=6.0f;
constexpr float RAD_TO_DEG=57.29578f;
// IMU gravity low-pass
constexpr float LPF_ALPHA=0.15f;
// Shake
constexpr float SHAKE_THRESHOLD=0.9f;
constexpr int64_t SHAKE_COOLDOWN_US=300000;
constexpr float JUMP_IMPULSE=320.0f;
// Expression hysteresis
constexpr float MOVE_ON_THRESHOLD=15.0f;
constexpr float MOVE_OFF_THRESHOLD=8.0f;
constexpr int64_t MOVE_STABLE_US=200000;
// Random 90-degree extra rotation (separate from physics)
constexpr float EXTRA_ROTATION_DEG=60.0f;
constexpr int64_t EXTRA_ROTATION_DURATION_US=350000; // 350ms（演出を滑らかに）
constexpr int64_t EXTRA_ROTATION_COOLDOWN_US=2000000;
constexpr float EXTRA_ROTATION_PROBABILITY=0.3f;
constexpr int64_t EXTRA_ROTATION_CHECK_US=500000;
// 追加回転の発生条件（明確な転がりの閾値・安定継続時間）
constexpr float EXTRA_MIN_LINEAR_SPEED=20.0f;
constexpr float EXTRA_MIN_ANGULAR_SPEED=20.0f;
constexpr int64_t EXTRA_ROLLING_STABLE_US=150000;
// Frame timing
constexpr int64_t ROLLING_FRAME_US=40000;
constexpr float DT_MAX=0.05f;
}

void App::begin() {
    auto cfg=M5.config();
    cfg.fallback_board=m5::board_t::board_M5StickS3;
    M5.begin(cfg);
    M5.Power.setExtOutput(true);  // IR回路用の外部出力
    auto speaker_config=M5.Speaker.config();
    speaker_config.magnification=4;
    M5.Speaker.config(speaker_config);
    display_.begin();
    face_store_.begin();display_.setFaceStore(&face_store_);web_.setFaceStore(&face_store_);
    store_.begin();settings_=store_.load();time_.begin(settings_);wifi_.begin();voice_.begin();sensor_.configure(settings_);sensor_.begin();ir_api_.configure(settings_);web_.setIrApi(&ir_api_);web_.setVoiceChat(&voice_);voice_.setIrLearningActive(&ir_api_.learningActive());if(!settings_.ssid.empty())wifi_.connect(settings_);
    static constexpr uint8_t volumes[]{64,128,192,255};M5.Speaker.setVolume(volumes[settings_.volume_level]);
    static constexpr uint8_t brightness[]{64,128,192,255};M5.Display.setBrightness(brightness[settings_.brightness_level]);
    int64_t now=esp_timer_get_time();next_smile_us_=now+4000000+(esp_random()%5000000);last_battery_poll_us_=now;last_activity_us_=now;state_.battery_percent=static_cast<int>(M5.Power.getBatteryLevel());previous_face_=state_.face;previous_date_=dateKey(state_.time_synced);previous_minute_=minuteKey(state_.time_synced);
    display_.draw(state_,settings_,input_);state_.redraw=false;home_full_dirty_=false;
}

void App::update() {
    M5.update();wifi_.update();voice_.tick();bool a=M5.BtnA.wasPressed(),released=M5.BtnA.wasReleased(),b=M5.BtnB.wasPressed();
    if(state_.screen==Screen::TEXT_INPUT_SSID||state_.screen==Screen::TEXT_INPUT_PASS)textInput(a);
    else if(state_.screen==Screen::HOME&&state_.home_selection<0){
        if(rolling_&&(a||b))exitRolling();
        if(a||b)markActivity();
        voice_.update(a,released,state_.wifi==WifiStatus::CONNECTED,settings_);
        if(voice_.state()==VoiceState::IDLE&&b)normalInput(false,true);
    }
    else if(voice_.state()==VoiceState::IDLE){if(a||b)markActivity();normalInput(a,b);}
    auto voice_state=voice_.state();auto voice_message=voice_.message();auto voice_caption=voice_.caption();
    if(state_.voice!=voice_state||state_.voice_message!=voice_message||state_.voice_caption!=voice_caption){
        bool was_idle=state_.voice==VoiceState::IDLE;
        state_.voice=voice_state;state_.voice_message=voice_message;state_.voice_caption=voice_caption;
        if(!was_idle&&voice_state==VoiceState::IDLE)markActivity();
        if(state_.screen==Screen::HOME)voice_dirty_=true;else state_.redraw=true;
    }
    sync();int64_t now=esp_timer_get_time();
    if(state_.screen==Screen::HOME){
        if(now-last_battery_poll_us_>=30000000){
            last_battery_poll_us_=now;int level=static_cast<int>(M5.Power.getBatteryLevel());
            if(level!=state_.battery_percent){state_.battery_percent=level;battery_dirty_=true;}
        }
        auto date=dateKey(state_.time_synced);if(date!=previous_date_){previous_date_=date;date_dirty_=true;}
        auto minute=minuteKey(state_.time_synced);if(minute!=previous_minute_){previous_minute_=minute;date_dirty_=true;}
    }
    if(state_.screen==Screen::HOME&&state_.home_selection<0&&voice_state==VoiceState::IDLE){
        if(!rolling_&&now-last_activity_us_>=ROLLING_IDLE_US)enterRolling();
    }else if(rolling_){
        exitRolling();
    }
    if(rolling_){
        if(state_.redraw||home_full_dirty_){
            display_.draw(state_,settings_,input_);state_.redraw=false;home_full_dirty_=false;
            wifi_dirty_=battery_dirty_=date_dirty_=menu_dirty_=face_dirty_=voice_dirty_=false;
        }else{
            if(wifi_dirty_){display_.drawWifi(state_.wifi);wifi_dirty_=false;}
            if(battery_dirty_){display_.drawBattery(state_.battery_percent);battery_dirty_=false;}
            if(date_dirty_){display_.drawDate(state_.time_synced);date_dirty_=false;}
            if(menu_dirty_){if(state_.confirm_forget)display_.drawConfirmButtons(state_.home_selection==0);else{display_.drawConfigButton(state_.home_selection==0);display_.drawForgetButton(state_.home_selection==1);}menu_dirty_=false;}
        }
        updateRolling();
        return;
    }
    FaceExpression face=FaceExpression::NORMAL;
    if(voice_state==VoiceState::RECORDING)face=FaceExpression::SURPRISED;
    else if(voice_state==VoiceState::PLAYING){auto level=voice_.mouthLevel();face=level>=2?FaceExpression::MOUTH_LARGE:level==1?FaceExpression::MOUTH_MEDIUM:FaceExpression::NORMAL;}
    else if(voice_state==VoiceState::IDLE&&state_.screen==Screen::HOME){if(now<smile_until_us_)face=FaceExpression::SMILE;else if(now>=next_smile_us_){face=FaceExpression::SMILE;smile_until_us_=now+300000;next_smile_us_=now+4000000+(esp_random()%5000000);}}
    if(state_.face!=face){state_.face=face;if(state_.screen==Screen::HOME)face_dirty_=true;}
    if(volume_dirty_&&now-volume_changed_us_>=1000000){store_.save(settings_);volume_dirty_=false;}
    if(state_.screen==Screen::HOME){
        if(state_.redraw||home_full_dirty_){
            display_.draw(state_,settings_,input_);state_.redraw=false;home_full_dirty_=false;
            previous_face_=state_.face;
            wifi_dirty_=battery_dirty_=date_dirty_=menu_dirty_=face_dirty_=voice_dirty_=false;
        }else{
            if(wifi_dirty_){display_.drawWifi(state_.wifi);wifi_dirty_=false;}
            if(battery_dirty_){display_.drawBattery(state_.battery_percent);battery_dirty_=false;}
            if(date_dirty_){display_.drawDate(state_.time_synced);date_dirty_=false;}
            if(menu_dirty_){if(state_.confirm_forget)display_.drawConfirmButtons(state_.home_selection==0);else{display_.drawConfigButton(state_.home_selection==0);display_.drawForgetButton(state_.home_selection==1);}menu_dirty_=false;}
            if(voice_dirty_){display_.drawVoiceStatus(state_.voice,state_.voice_message,state_.voice_caption);voice_dirty_=false;}
            if(face_dirty_){if(mouthFace(previous_face_)&&mouthFace(state_.face))display_.drawMouth(state_.face);else display_.drawFace(state_.face);previous_face_=state_.face;face_dirty_=false;}
        }
    }else if(state_.redraw){display_.draw(state_,settings_,input_);state_.redraw=false;}

}

void App::normalInput(bool a,bool b) {
    if(state_.screen==Screen::HOME){
        if(state_.confirm_forget){
            if(b){state_.home_selection=1-state_.home_selection;menu_dirty_=true;}
            if(a){if(state_.home_selection==0)voice_.clearHistory();state_.confirm_forget=false;state_.home_selection=-1;menu_dirty_=true;}
            return;
        }
        if(b){state_.home_selection=state_.home_selection+1;if(state_.home_selection>=static_cast<int>(HOME_MENU.size()))state_.home_selection=-1;menu_dirty_=true;}
        if(!a||state_.home_selection<0)return;
        auto action=HOME_MENU[state_.home_selection].action;
        if(action==HomeAction::SETTINGS)enterSettings();
        else if(action==HomeAction::FORGET){state_.confirm_forget=true;state_.home_selection=0;menu_dirty_=true;}
        return;
    }
    if(b){state_.settings_selection=(state_.settings_selection+2)%6-1;state_.redraw=true;}if(!a)return;
    if(state_.settings_selection==0){input_.begin(settings_.ssid,false);state_.screen=Screen::TEXT_INPUT_SSID;state_.redraw=true;}
    else if(state_.settings_selection==1){input_.begin(settings_.pass,true);state_.screen=Screen::TEXT_INPUT_PASS;state_.redraw=true;}
    else if(state_.settings_selection==2){static constexpr uint8_t volumes[]{64,128,192,255};settings_.volume_level=(settings_.volume_level+1)%4;M5.Speaker.setVolume(volumes[settings_.volume_level]);volume_changed_us_=esp_timer_get_time();volume_dirty_=true;state_.redraw=true;}
    else if(state_.settings_selection==3){static constexpr uint8_t brightness[]{64,128,192,255};settings_.brightness_level=(settings_.brightness_level+1)%4;M5.Display.setBrightness(brightness[settings_.brightness_level]);volume_changed_us_=esp_timer_get_time();volume_dirty_=true;state_.redraw=true;}
    else if(state_.settings_selection==4)leaveSettings();
}

void App::textInput(bool a) {
    float x=0,y=0,z=0;if(M5.Imu.getAccel(&x,&y,&z)&&input_.moveByTilt(x,y,esp_timer_get_time()))state_.redraw=true;if(!a)return;
    bool pass=state_.screen==Screen::TEXT_INPUT_PASS;auto action=input_.activate();
    if(action==TextInput::Action::OK){if(pass)settings_.pass=input_.value();else settings_.ssid=input_.value();store_.save(settings_);state_.screen=Screen::SETTINGS;state_.settings_selection=-1;if(pass&&!settings_.ssid.empty())wifi_.connect(settings_);}
    else if(action==TextInput::Action::CANCEL){state_.screen=Screen::SETTINGS;state_.settings_selection=-1;}
    state_.redraw=true;
}

void App::enterSettings(){state_.screen=Screen::SETTINGS;state_.settings_selection=-1;state_.redraw=true;if(!settings_.ssid.empty()&&wifi_.status()!=WifiStatus::CONNECTED)wifi_.connect(settings_);}
void App::leaveSettings(){state_.screen=Screen::HOME;state_.home_selection=-1;home_full_dirty_=true;state_.redraw=true;markActivity();}

void App::markActivity(){last_activity_us_=esp_timer_get_time();}

void App::enterRolling(){
    rolling_=true;
    last_rolling_frame_us_=esp_timer_get_time()-ROLLING_FRAME_US;
    face_x_=static_cast<float>((ROLLING_X_MIN+ROLLING_X_MAX)/2);
    face_y_=static_cast<float>((ROLLING_Y_MIN+ROLLING_Y_MAX)/2);
    vel_x_=0.0f;vel_y_=0.0f;prev_face_x_=face_x_;prev_face_y_=face_y_;
    last_shake_us_=0;
    rolling_moving_=false;
    stopped_since_us_=esp_timer_get_time();
    physical_angle_=0.0f;physical_angular_velocity_=0.0f;support_nx_=0.0f;support_ny_=0.0f;has_support_=false;
    extra_angle_=0.0f;extra_target_=0.0f;extra_start_us_=0;extra_active_=false;
    extra_start_angle_=0.0f;extra_rotation_dir_=0.0f;extra_stable_since_us_=0;
    last_extra_check_us_=esp_timer_get_time();last_extra_done_us_=0;
    float ax=0.0f,ay=0.0f,az=0.0f;
    if(M5.Imu.getAccel(&ax,&ay,&az)){gravity_x_=ax;gravity_y_=ay;gravity_z_=az;}
    else{gravity_x_=0.0f;gravity_y_=0.0f;gravity_z_=0.0f;}
    state_.voice_caption.clear();
    home_full_dirty_=true;
}

void App::exitRolling(){
    if(!rolling_)return;
    rolling_=false;
    vel_x_=0.0f;vel_y_=0.0f;
    physical_angle_=0.0f;physical_angular_velocity_=0.0f;extra_angle_=0.0f;extra_target_=0.0f;extra_active_=false;
    extra_start_angle_=0.0f;extra_rotation_dir_=0.0f;extra_stable_since_us_=0;
    home_full_dirty_=true;
    markActivity();
}

void App::updateRolling(){
    int64_t now=esp_timer_get_time();
    if(now-last_rolling_frame_us_<ROLLING_FRAME_US)return;
    int64_t elapsed=now-last_rolling_frame_us_;
    last_rolling_frame_us_=now;
    float dt=elapsed*1e-6f;
    if(dt>DT_MAX)dt=DT_MAX;
    // 1. IMU → LPF重力
    float ax=0.0f,ay=0.0f,az=0.0f;
    bool imu_ok=M5.Imu.getAccel(&ax,&ay,&az);
    if(imu_ok){
        gravity_x_+=LPF_ALPHA*(ax-gravity_x_);
        gravity_y_+=LPF_ALPHA*(ay-gravity_y_);
        gravity_z_+=LPF_ALPHA*(az-gravity_z_);
    }
    // 2. 画面座標重力
    float gsx=0.0f,gsy=0.0f;
    if(imu_ok)mapImuGravityToScreen(gravity_x_,gravity_y_,gsx,gsy);
    // 3. shake検出（動的加速度の大きさ）→ 支持面法線方向へジャンプ
    if(imu_ok){
        float dx=ax-gravity_x_,dy=ay-gravity_y_,dz=az-gravity_z_;
        float mag=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(mag>SHAKE_THRESHOLD&&now-last_shake_us_>=SHAKE_COOLDOWN_US){
            float jx=0.0f,jy=-1.0f;
            if(has_support_){jx=support_nx_;jy=support_ny_;}
            else{float gm=std::sqrt(gsx*gsx+gsy*gsy);if(gm>0.01f){jx=-gsx/gm;jy=-gsy/gm;}}
            vel_x_+=jx*JUMP_IMPULSE;
            vel_y_+=jy*JUMP_IMPULSE;
            last_shake_us_=now;
        }
    }
    // 4. 重力加速度（X/Y統一）
    vel_x_+=gsx*GRAVITY_GAIN*dt;
    vel_y_+=gsy*GRAVITY_GAIN*dt;
    // 5. 減衰（dt考慮）+ 速度上限
    float damping=std::exp(-DAMPING_COEFF*dt);
    vel_x_*=damping;vel_y_*=damping;
    float speed=std::sqrt(vel_x_*vel_x_+vel_y_*vel_y_);
    if(speed>MAX_SPEED){float s=MAX_SPEED/speed;vel_x_*=s;vel_y_*=s;}
    // 6. 位置更新
    face_x_+=vel_x_*dt;
    face_y_+=vel_y_*dt;
    // 7. 四辺衝突（法線ベース共通式・左右上下同等）
    bool hit_left=false,hit_right=false,hit_top=false,hit_bottom=false;
    if(face_x_<ROLLING_X_MIN){face_x_=ROLLING_X_MIN;if(vel_x_<0.0f){vel_x_=-vel_x_*WALL_RESTITUTION;vel_y_*=(1.0f-WALL_FRICTION);}hit_left=true;}
    else if(face_x_>ROLLING_X_MAX){face_x_=ROLLING_X_MAX;if(vel_x_>0.0f){vel_x_=-vel_x_*WALL_RESTITUTION;vel_y_*=(1.0f-WALL_FRICTION);}hit_right=true;}
    if(face_y_<ROLLING_Y_MIN){face_y_=ROLLING_Y_MIN;if(vel_y_<0.0f){vel_y_=-vel_y_*WALL_RESTITUTION;vel_x_*=(1.0f-WALL_FRICTION);}hit_top=true;}
    else if(face_y_>ROLLING_Y_MAX){face_y_=ROLLING_Y_MAX;if(vel_y_>0.0f){vel_y_=-vel_y_*WALL_RESTITUTION;vel_x_*=(1.0f-WALL_FRICTION);}hit_bottom=true;}
    // 微小速度の停止
    if(std::fabs(vel_x_)<STOP_SPEED&&std::fabs(vel_y_)<STOP_SPEED){vel_x_=0.0f;vel_y_=0.0f;}
    // 8. 支持面判定（接触壁から重力で押し付けている壁を選択）
    {
        float best_pressure=-1.0f;bool best_has=false;float best_nx=0.0f,best_ny=0.0f;
        auto consider=[&](bool hit,float nx,float ny){
            if(!hit)return;
            float pressure=-(gsx*nx+gsy*ny); // 壁へ押し付ける重力成分
            // ヒステリシス: 現在の支持面を優先
            if(has_support_&&std::fabs(nx-support_nx_)<0.01f&&std::fabs(ny-support_ny_)<0.01f)pressure+=CONTACT_HYSTERESIS;
            if(pressure>best_pressure){best_pressure=pressure;best_has=true;best_nx=nx;best_ny=ny;}
        };
        consider(hit_left,1.0f,0.0f);
        consider(hit_right,-1.0f,0.0f);
        consider(hit_top,0.0f,1.0f);
        consider(hit_bottom,0.0f,-1.0f);
        has_support_=best_has;
        if(best_has){support_nx_=best_nx;support_ny_=best_ny;}
    }
    // 9. 物理回転（支持面に沿った移動から算出・全方向共通）
    float delta_x=face_x_-prev_face_x_;
    float delta_y=face_y_-prev_face_y_;
    if(has_support_){
        // 接線方向の移動量から回転角を算出
        float delta_angle_rad=(support_nx_*delta_y-support_ny_*delta_x)/ROLLING_RADIUS;
        physical_angle_+=delta_angle_rad*RAD_TO_DEG;
        physical_angular_velocity_=delta_angle_rad*RAD_TO_DEG/dt;
    }else{
        // 空中: 角速度を維持・軽く減衰
        physical_angle_+=physical_angular_velocity_*dt;
        physical_angular_velocity_*=std::exp(-1.0f*dt);
        if(std::fabs(physical_angular_velocity_)<2.0f)physical_angular_velocity_=0.0f;
    }
    prev_face_x_=face_x_;
    prev_face_y_=face_y_;
    // 10. 表情（移動中=驚き、停止=笑顔・ヒステリシス）
    float trans_speed=std::sqrt(vel_x_*vel_x_+vel_y_*vel_y_);
    bool moving=trans_speed>MOVE_ON_THRESHOLD||std::fabs(physical_angular_velocity_)>20.0f||extra_active_||!has_support_;
    if(!rolling_moving_){
        if(moving){rolling_moving_=true;stopped_since_us_=0;}
    }else{
        if(!moving){
            if(!stopped_since_us_)stopped_since_us_=now;
            else if(now-stopped_since_us_>=MOVE_STABLE_US){rolling_moving_=false;stopped_since_us_=0;}
        }else{stopped_since_us_=0;}
    }
    // 11. ランダム90°追加回転（物理回転と分離・方向は物理回転方向に追従）
    // 明確な転がりの判定: 直線速度と角速度の両方が閾値超え
    float linear_speed=std::sqrt(vel_x_*vel_x_+vel_y_*vel_y_);
    bool clearly_rolling=linear_speed>=EXTRA_MIN_LINEAR_SPEED&&std::fabs(physical_angular_velocity_)>=EXTRA_MIN_ANGULAR_SPEED;
    if(clearly_rolling){
        if(!extra_stable_since_us_)extra_stable_since_us_=now;
    }else{
        extra_stable_since_us_=0;
    }
    bool stable_rolling=extra_stable_since_us_&&(now-extra_stable_since_us_>=EXTRA_ROLLING_STABLE_US);
    if(stable_rolling&&!extra_active_&&now-last_extra_check_us_>=EXTRA_ROTATION_CHECK_US){
        last_extra_check_us_=now;
        if(now-last_extra_done_us_>=EXTRA_ROTATION_COOLDOWN_US){
            float r=static_cast<float>(esp_random()%1000)/1000.0f;
            if(r<EXTRA_ROTATION_PROBABILITY){
                // 方向は物理回転方向（角速度の符号）。ほぼ停止なら開始しない
                if(physical_angular_velocity_>EXTRA_MIN_ANGULAR_SPEED)extra_rotation_dir_=1.0f;
                else if(physical_angular_velocity_<-EXTRA_MIN_ANGULAR_SPEED)extra_rotation_dir_=-1.0f;
                else extra_rotation_dir_=0.0f;
                if(extra_rotation_dir_!=0.0f){
                    extra_start_angle_=extra_angle_;
                    extra_target_=extra_start_angle_+extra_rotation_dir_*EXTRA_ROTATION_DEG;
                    extra_start_us_=now;
                    extra_active_=true;
                }
            }
        }
    }
    // 90°補間: 開始角度から目標角度へ時間で直接補間（smoothstep）
    if(extra_active_){
        float t=static_cast<float>(now-extra_start_us_)/static_cast<float>(EXTRA_ROTATION_DURATION_US);
        if(t<0.0f)t=0.0f;else if(t>1.0f)t=1.0f;
        float eased=t*t*(3.0f-2.0f*t);
        extra_angle_=extra_start_angle_+(extra_target_-extra_start_angle_)*eased;
        if(t>=1.0f){extra_angle_=extra_target_;extra_active_=false;last_extra_done_us_=now;}
    }
    FaceExpression rolling_face=rolling_moving_?FaceExpression::SURPRISED:FaceExpression::SMILE;
    display_.drawRollingFace(rolling_face,face_x_,face_y_-18.0f,physical_angle_+extra_angle_,ROLLING_SCALE);
}

void App::sync(){
    if(state_.wifi!=wifi_.status()||state_.ip!=wifi_.ip()){
        state_.wifi=wifi_.status();state_.ip=wifi_.ip();
        if(state_.screen==Screen::HOME)wifi_dirty_=true;else state_.redraw=true;
    }
    time_.setWifiConnected(state_.wifi==WifiStatus::CONNECTED);
    if(state_.time_synced!=time_.synced()){
        state_.time_synced=time_.synced();
        if(state_.screen==Screen::HOME){date_dirty_=true;}else state_.redraw=true;
    }
    bool in_settings=state_.screen==Screen::SETTINGS;
    // WebServerはWi-Fi接続中に常時起動（IR APIもポート80で提供）。
    if(state_.wifi==WifiStatus::CONNECTED&&!web_.running()){web_.start(settings_,saveWebSettings,this);state_.web_running=web_.running();state_.web_password=web_.password();state_.redraw=true;}
    else if(state_.wifi!=WifiStatus::CONNECTED&&web_.running()){web_.stop();state_.web_running=false;state_.web_password.clear();state_.redraw=true;}
    // 設定画面はConfig遷移時のみアクセス可能。
    web_.setSettingsEnabled(in_settings);
}

void App::saveWebSettings(void* context,const Settings& settings){
    auto* self=static_cast<App*>(context);self->settings_=settings;self->store_.save(self->settings_);self->time_.apply(self->settings_);self->sensor_.configure(self->settings_);self->ir_api_.configure(self->settings_);self->state_.time_synced=false;self->state_.redraw=true;
}

