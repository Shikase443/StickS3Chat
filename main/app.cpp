#include "app.hpp"
#include "M5Unified.hpp"
#include "esp_random.h"
#include "esp_timer.h"
#include <ctime>

namespace {
std::string dateKey(bool synced){if(!synced)return "----/--/--";char value[16];std::time_t now=std::time(nullptr);std::tm local{};localtime_r(&now,&local);std::strftime(value,sizeof(value),"%Y/%m/%d",&local);return value;}
std::string minuteKey(bool synced){if(!synced)return "--:--";char value[8];std::time_t now=std::time(nullptr);std::tm local{};localtime_r(&now,&local);std::strftime(value,sizeof(value),"%H:%M",&local);return value;}
bool mouthFace(FaceExpression face){return face==FaceExpression::NORMAL||face==FaceExpression::MOUTH_MEDIUM||face==FaceExpression::MOUTH_LARGE;}
}

void App::begin() {
    auto cfg=M5.config();
    cfg.fallback_board=m5::board_t::board_M5StickS3;
    M5.begin(cfg);
    auto speaker_config=M5.Speaker.config();
    speaker_config.magnification=4;
    M5.Speaker.config(speaker_config);
    display_.begin();
    store_.begin();settings_=store_.load();time_.begin(settings_);wifi_.begin();voice_.begin();if(!settings_.ssid.empty())wifi_.connect(settings_);
    static constexpr uint8_t volumes[]{64,128,192,255};M5.Speaker.setVolume(volumes[settings_.volume_level]);
    static constexpr uint8_t brightness[]{64,128,192,255};M5.Display.setBrightness(brightness[settings_.brightness_level]);
    int64_t now=esp_timer_get_time();next_smile_us_=now+4000000+(esp_random()%5000000);last_battery_poll_us_=now;state_.battery_percent=static_cast<int>(M5.Power.getBatteryLevel());previous_face_=state_.face;previous_date_=dateKey(state_.time_synced);previous_minute_=minuteKey(state_.time_synced);
    display_.draw(state_,settings_,input_);state_.redraw=false;home_full_dirty_=false;
}

void App::update() {
    M5.update();wifi_.update();voice_.tick();bool a=M5.BtnA.wasPressed(),released=M5.BtnA.wasReleased(),b=M5.BtnB.wasPressed();
    if(state_.screen==Screen::TEXT_INPUT_SSID||state_.screen==Screen::TEXT_INPUT_PASS)textInput(a);
    else if(state_.screen==Screen::HOME&&state_.home_selection<0){voice_.update(a,released,state_.wifi==WifiStatus::CONNECTED,settings_);if(voice_.state()==VoiceState::IDLE&&b)normalInput(false,true);}
    else if(voice_.state()==VoiceState::IDLE)normalInput(a,b);
    auto voice_state=voice_.state();auto voice_message=voice_.message();auto voice_caption=voice_.caption();
    if(state_.voice!=voice_state||state_.voice_message!=voice_message||state_.voice_caption!=voice_caption){
        state_.voice=voice_state;state_.voice_message=voice_message;state_.voice_caption=voice_caption;
        if(state_.screen==Screen::HOME)voice_dirty_=true;else state_.redraw=true;
    }
    sync();int64_t now=esp_timer_get_time();
    if(state_.screen==Screen::HOME){
        if(now-last_battery_poll_us_>=30000000){
            last_battery_poll_us_=now;int level=static_cast<int>(M5.Power.getBatteryLevel());
            if(level!=state_.battery_percent){state_.battery_percent=level;battery_dirty_=true;}
        }
        auto date=dateKey(state_.time_synced);if(date!=previous_date_){previous_date_=date;date_dirty_=true;}
        auto minute=minuteKey(state_.time_synced);if(minute!=previous_minute_){previous_minute_=minute;clock_dirty_=true;}
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
            wifi_dirty_=battery_dirty_=date_dirty_=clock_dirty_=menu_dirty_=face_dirty_=voice_dirty_=false;
        }else{
            if(wifi_dirty_){display_.drawWifi(state_.wifi);wifi_dirty_=false;}
            if(battery_dirty_){display_.drawBattery(state_.battery_percent);battery_dirty_=false;}
            if(date_dirty_){display_.drawDate(state_.time_synced);date_dirty_=false;}
            if(clock_dirty_){display_.drawClock(state_.time_synced);clock_dirty_=false;}
            if(menu_dirty_){display_.drawConfigButton(state_.home_selection==0);menu_dirty_=false;}
            if(voice_dirty_){display_.drawVoiceStatus(state_.voice,state_.voice_message,state_.voice_caption);voice_dirty_=false;}
            if(face_dirty_){if(mouthFace(previous_face_)&&mouthFace(state_.face))display_.drawMouth(state_.face);else display_.drawFace(state_.face);previous_face_=state_.face;face_dirty_=false;}
        }
    }else if(state_.redraw){display_.draw(state_,settings_,input_);state_.redraw=false;}
}

void App::normalInput(bool a,bool b) {
    if(state_.screen==Screen::HOME){
        if(b){state_.home_selection=state_.home_selection+1;if(state_.home_selection>=static_cast<int>(HOME_MENU.size()))state_.home_selection=-1;menu_dirty_=true;}
        if(!a||state_.home_selection<0)return;
        auto action=HOME_MENU[state_.home_selection].action;
        if(action==HomeAction::SETTINGS)enterSettings();
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
void App::leaveSettings(){web_.stop();state_.web_running=false;state_.web_password.clear();state_.screen=Screen::HOME;state_.home_selection=-1;home_full_dirty_=true;state_.redraw=true;}

void App::sync(){
    if(state_.wifi!=wifi_.status()||state_.ip!=wifi_.ip()){
        state_.wifi=wifi_.status();state_.ip=wifi_.ip();
        if(state_.screen==Screen::HOME)wifi_dirty_=true;else state_.redraw=true;
    }
    time_.setWifiConnected(state_.wifi==WifiStatus::CONNECTED);
    if(state_.time_synced!=time_.synced()){
        state_.time_synced=time_.synced();
        if(state_.screen==Screen::HOME){date_dirty_=true;clock_dirty_=true;}else state_.redraw=true;
    }
    bool in_settings=state_.screen==Screen::SETTINGS;
    if(in_settings&&state_.wifi==WifiStatus::CONNECTED&&!web_.running()){web_.start(settings_,saveWebSettings,this);state_.web_running=web_.running();state_.web_password=web_.password();state_.redraw=true;}
    else if((!in_settings||state_.wifi!=WifiStatus::CONNECTED)&&web_.running()){web_.stop();state_.web_running=false;state_.web_password.clear();state_.redraw=true;}
}

void App::saveWebSettings(void* context,const Settings& settings){
    auto* self=static_cast<App*>(context);self->settings_=settings;self->store_.save(self->settings_);self->time_.apply(self->settings_);self->state_.time_synced=false;self->state_.redraw=true;
}
