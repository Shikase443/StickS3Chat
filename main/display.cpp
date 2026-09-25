#include "display.hpp"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <cstring>
#include "M5Unified.hpp"

namespace {
constexpr uint32_t HOME_BG=TFT_BLACK, BG=0x1082, PANEL=0x2124, ACCENT=0x05B9, MUTED=0x8410;
constexpr int FACE_WIDTH=114, FACE_HEIGHT=114, FACE_Y=58;
constexpr int FACE_X=(135-FACE_WIDTH)/2;
constexpr int MOUTH_X=42, MOUTH_Y=52, MOUTH_WIDTH=30, MOUTH_HEIGHT=20;
constexpr uint32_t FACE_COLOR=0x18C7A6;
constexpr int CAPTION_Y=172, CAPTION_HEIGHT=39, CAPTION_ROWS=3;

size_t utf8Length(uint8_t first){if((first&0x80)==0)return 1;if((first&0xE0)==0xC0)return 2;if((first&0xF0)==0xE0)return 3;if((first&0xF8)==0xF0)return 4;return 1;}

void drawOpenEyes(M5Canvas& canvas,bool surprised) {
    const int radius_x=surprised?10:8;
    const int radius_y=radius_x;
    constexpr int eye_y=23;
    for(int eye_x:{36,78}){
        canvas.fillEllipse(eye_x,eye_y,radius_x,radius_y,FACE_COLOR);
        canvas.fillCircle(eye_x-3,eye_y-4,2,TFT_WHITE);
        canvas.drawPixel(eye_x+3,eye_y+4,TFT_WHITE);
    }
}

void drawSmileEyes(M5Canvas& canvas) {
    for(int offset:{0,42}){
        canvas.drawFastHLine(32+offset,17,8,FACE_COLOR);
        canvas.drawFastHLine(29+offset,19,3,FACE_COLOR);
        canvas.drawFastHLine(40+offset,19,3,FACE_COLOR);
        canvas.drawFastHLine(27+offset,22,3,FACE_COLOR);
        canvas.drawFastHLine(43+offset,22,3,FACE_COLOR);
    }
}

void drawMouthShape(M5Canvas& canvas,FaceExpression expression,int center_x,int base_y) {
    if(expression==FaceExpression::SMILE){
        canvas.drawFastHLine(center_x-5,base_y+3,11,FACE_COLOR);
        canvas.drawFastHLine(center_x-9,base_y,4,FACE_COLOR);
        canvas.drawFastHLine(center_x+6,base_y,4,FACE_COLOR);
        canvas.drawFastHLine(center_x-11,base_y-3,3,FACE_COLOR);
        canvas.drawFastHLine(center_x+10,base_y-3,3,FACE_COLOR);
    }else if(expression==FaceExpression::SURPRISED){
        canvas.fillEllipse(center_x,base_y,5,7,FACE_COLOR);
    }else if(expression==FaceExpression::MOUTH_MEDIUM){
        canvas.fillEllipse(center_x,base_y,8,4,FACE_COLOR);
    }else if(expression==FaceExpression::MOUTH_LARGE){
        canvas.fillEllipse(center_x,base_y,9,7,FACE_COLOR);
    }else{
        canvas.fillRoundRect(center_x-8,base_y-1,16,3,1,FACE_COLOR);
    }
}
void button(int y, const char* text, bool on) {
    int w=M5.Display.width()-16; M5.Display.fillRoundRect(8,y,w,28,7,on?ACCENT:PANEL);
    M5.Display.setTextColor(on?TFT_BLACK:TFT_LIGHTGREY); M5.Display.setTextDatum(middle_center); M5.Display.drawString(text,M5.Display.width()/2,y+14);
}
void levelButton(int y,const char* text,bool on,uint8_t level) {
    int w=M5.Display.width()-16;M5.Display.fillRoundRect(8,y,w,28,7,on?ACCENT:PANEL);
    uint32_t color=on?TFT_BLACK:TFT_CYAN;M5.Display.setTextColor(on?TFT_BLACK:TFT_LIGHTGREY);M5.Display.setTextDatum(middle_left);M5.Display.drawString(text,15,y+14);
    int bx=M5.Display.width()-38;for(int i=0;i<4;++i){int h=4+i*3;M5.Display.drawRect(bx+i*6,y+22-h,4,h,color);if(i<=level)M5.Display.fillRect(bx+i*6+1,y+23-h,2,h-2,color);}
}
M5Canvas& faceSprite() {
    static M5Canvas sprite(&M5.Display);
    static bool ready = false;
    if (!ready) { sprite.setColorDepth(16); sprite.setPsram(true); ready = sprite.createSprite(FACE_WIDTH, FACE_HEIGHT) != nullptr; }
    return sprite;
}
}

void Display::begin() { M5.Display.setRotation(0); M5.Display.setBrightness(200); M5.Display.setFont(&fonts::efontJA_12); }

void Display::drawWifi(WifiStatus status) {
    static M5Canvas sprite(&M5.Display);static bool ready=false;if(!ready){sprite.setColorDepth(16);sprite.setPsram(true);ready=sprite.createSprite(24,18)!=nullptr;}if(!ready)return;
    sprite.fillSprite(HOME_BG);uint32_t color=status==WifiStatus::CONNECTED?TFT_GREEN:0x0320;
    sprite.drawArc(8,9,8,7,215,325,color);sprite.drawArc(8,9,5,4,215,325,color);sprite.fillCircle(8,11,1,color);sprite.pushSprite(75,0);
}

void Display::drawBattery(int level) {
    static M5Canvas sprite(&M5.Display);static bool ready=false;if(!ready){sprite.setColorDepth(16);sprite.setPsram(true);ready=sprite.createSprite(34,18)!=nullptr;sprite.setFont(&fonts::efontJA_12);}if(!ready)return;
    level=std::clamp(level,0,100);sprite.fillSprite(HOME_BG);sprite.drawRect(1,3,28,12,TFT_GREEN);sprite.fillRect(29,6,3,6,TFT_GREEN);int fill=level*24/100;if(fill>0)sprite.fillRect(3,5,fill,8,TFT_GREEN);char text[8];std::snprintf(text,sizeof(text),"%d%%",level);sprite.setTextColor(TFT_BLACK);sprite.setTextSize(1);sprite.setTextDatum(middle_left);sprite.drawString(text,4,9);sprite.pushSprite(M5.Display.width()-34,0);
}

void Display::drawDate(bool synced) {
    static M5Canvas sprite(&M5.Display);static bool ready=false;if(!ready){sprite.setColorDepth(16);sprite.setPsram(true);ready=sprite.createSprite(70,18)!=nullptr;sprite.setFont(&fonts::efontJA_12_b);}if(!ready)return;
    char text[48]="-:-- -/-";if(synced){std::time_t now=std::time(nullptr);std::tm local{};localtime_r(&now,&local);std::snprintf(text,sizeof(text),"%d:%02d %d/%d",local.tm_hour,local.tm_min,local.tm_mon+1,local.tm_mday);}sprite.fillSprite(HOME_BG);sprite.setTextColor(TFT_WHITE);sprite.setTextSize(1);sprite.setTextDatum(top_left);sprite.drawString(text,0,2);sprite.pushSprite(5,0);
}


void Display::drawFace(FaceExpression expression) {
    M5Canvas& sprite = faceSprite();
    uint8_t* buffer = static_cast<uint8_t*>(sprite.frameBuffer(0));
    if (!buffer) return;
    if (face_store_ && face_store_->loadFace(expression, buffer, FaceStore::FACE_BYTES)) {
        sprite.pushSprite(FACE_X, FACE_Y);
        return;
    }
    sprite.fillSprite(HOME_BG);
    if (expression == FaceExpression::SMILE) drawSmileEyes(sprite); else drawOpenEyes(sprite, expression == FaceExpression::SURPRISED);
    drawMouthShape(sprite, expression, 57, 59);
    sprite.pushSprite(FACE_X, FACE_Y);
}

void Display::drawMouth(FaceExpression expression) {
    drawFace(expression);
}

void Display::drawRollingFace(FaceExpression expression, float cx, float cy, float angle, float scale) {
    static M5Canvas rolling(&M5.Display);
    static bool rolling_ready = false;
    if (!rolling_ready) { rolling.setColorDepth(16); rolling.setPsram(true); rolling_ready = rolling.createSprite(135, 193) != nullptr; }
    if (!rolling_ready) return;
    rolling.fillSprite(HOME_BG);
    M5Canvas& face = faceSprite();
    uint8_t* buffer = static_cast<uint8_t*>(face.frameBuffer(0));
    if (!buffer) return;
    if (!(face_store_ && face_store_->loadFace(expression, buffer, FaceStore::FACE_BYTES))) {
        face.fillSprite(HOME_BG);
        if (expression == FaceExpression::SMILE) drawSmileEyes(face); else drawOpenEyes(face, expression == FaceExpression::SURPRISED);
        drawMouthShape(face, expression, 57, 59);
    }
    face.pushRotateZoom(&rolling, cx, cy, angle, scale, scale);
    rolling.pushSprite(0, 18);
}

void Display::drawConfigButton(bool selected) {
    static M5Canvas sprite(&M5.Display);static bool ready=false;if(!ready){sprite.setColorDepth(16);sprite.setPsram(true);ready=sprite.createSprite(60,16)!=nullptr;sprite.setFont(&fonts::efontJA_12);}if(!ready)return;
    sprite.fillSprite(HOME_BG);sprite.fillRoundRect(0,0,60,16,5,selected?ACCENT:PANEL);sprite.setTextColor(selected?TFT_BLACK:TFT_LIGHTGREY);sprite.setTextDatum(middle_center);sprite.drawString("Config",30,8);sprite.pushSprite(5,M5.Display.height()-21);
}

void Display::drawForgetButton(bool selected) {
    static M5Canvas sprite(&M5.Display);static bool ready=false;if(!ready){sprite.setColorDepth(16);sprite.setPsram(true);ready=sprite.createSprite(60,16)!=nullptr;sprite.setFont(&fonts::efontJA_12);}if(!ready)return;
    sprite.fillSprite(HOME_BG);sprite.fillRoundRect(0,0,60,16,5,selected?ACCENT:PANEL);sprite.setTextColor(selected?TFT_BLACK:TFT_LIGHTGREY);sprite.setTextDatum(middle_center);sprite.drawString("Forget",30,8);sprite.pushSprite(70,M5.Display.height()-21);
}

void Display::drawConfirmButtons(bool yes_selected) {
    static M5Canvas sprite(&M5.Display);static bool ready=false;if(!ready){sprite.setColorDepth(16);sprite.setPsram(true);ready=sprite.createSprite(60,16)!=nullptr;sprite.setFont(&fonts::efontJA_12);}if(!ready)return;
    sprite.fillSprite(HOME_BG);sprite.fillRoundRect(0,0,60,16,5,yes_selected?TFT_RED:PANEL);sprite.setTextColor(yes_selected?TFT_WHITE:TFT_LIGHTGREY);sprite.setTextDatum(middle_center);sprite.drawString("YES",30,8);sprite.pushSprite(5,M5.Display.height()-21);
    sprite.fillSprite(HOME_BG);sprite.fillRoundRect(0,0,60,16,5,!yes_selected?TFT_RED:PANEL);sprite.setTextColor(!yes_selected?TFT_WHITE:TFT_LIGHTGREY);sprite.setTextDatum(middle_center);sprite.drawString("NO",30,8);sprite.pushSprite(70,M5.Display.height()-21);
}

void Display::drawVoiceStatus(VoiceState state,const std::string& message,const std::string& caption) {
    static M5Canvas sprite(&M5.Display);static bool ready=false;if(!ready){sprite.setColorDepth(16);sprite.setPsram(true);ready=sprite.createSprite(135,CAPTION_HEIGHT)!=nullptr;sprite.setFont(&fonts::efontJA_12);}if(!ready)return;
    sprite.fillSprite(HOME_BG);sprite.setTextSize(1);sprite.setTextDatum(top_left);
    const auto diagnostic=message.find("HTTP:");
    if(state==VoiceState::ERROR&&diagnostic!=std::string::npos){
        sprite.setFont(&fonts::Font0);
        sprite.setTextColor(TFT_RED,HOME_BG);
        sprite.setTextWrap(false,false);
        sprite.setCursor(2,2);
        sprite.print(message.substr(diagnostic).c_str());
        sprite.pushSprite(0,CAPTION_Y);
        sprite.setFont(&fonts::efontJA_12);
        return;
    }
    const bool status=state==VoiceState::STT_PROCESSING||state==VoiceState::LLM_PROCESSING||state==VoiceState::TTS_PROCESSING||state==VoiceState::ERROR;
    const std::string& text=status?message:caption;uint32_t color=state==VoiceState::ERROR?TFT_RED:TFT_WHITE;
    std::vector<std::string> lines(1);std::vector<int> widths(1,0);
    for(size_t offset=0;offset<text.size();){size_t length=std::min(utf8Length(static_cast<uint8_t>(text[offset])),text.size()-offset);std::string glyph=text.substr(offset,length);offset+=length;if(glyph=="\n"){lines.emplace_back();widths.push_back(0);continue;}int width=sprite.textWidth(glyph.c_str());if(!lines.back().empty()&&widths.back()+width>131){lines.emplace_back();widths.push_back(0);}lines.back()+=glyph;widths.back()+=width;}
    size_t visible=std::min<size_t>(CAPTION_ROWS,lines.size()),first=lines.size()-visible;int first_row=CAPTION_ROWS-static_cast<int>(visible);sprite.setTextColor(color,HOME_BG);
    for(size_t i=0;i<visible;++i)sprite.drawString(lines[first+i].c_str(),2,(first_row+static_cast<int>(i))*13);
    sprite.pushSprite(0,CAPTION_Y);
}

void Display::draw(const AppState& s, const Settings& cfg, const TextInput& input) {
    auto& d=M5.Display; d.startWrite(); d.fillScreen(s.screen==Screen::HOME?HOME_BG:BG); d.setFont(&fonts::efontJA_12);d.setTextSize(1); d.setTextDatum(top_left);
    if (s.screen==Screen::HOME) {
        drawWifi(s.wifi);drawBattery(s.battery_percent);drawDate(s.time_synced);drawFace(s.face);drawVoiceStatus(s.voice,s.voice_message,s.voice_caption);if(s.confirm_forget)drawConfirmButtons(s.home_selection==0);else{drawConfigButton(s.home_selection==0);drawForgetButton(s.home_selection==1);}
    } else if (s.screen==Screen::SETTINGS) {
        d.setTextColor(TFT_WHITE); d.drawString("Config",8,5); char line[80];
        std::snprintf(line,sizeof(line),"SSID  %s",cfg.ssid.empty()?"(Not set)":cfg.ssid.c_str()); button(20,line,s.settings_selection==0);
        button(50,cfg.pass.empty()?"PASS  (Not set)":"PASS  ********",s.settings_selection==1);
        levelButton(80,"Volume",s.settings_selection==2,cfg.volume_level);
        levelButton(110,"Brightness",s.settings_selection==3,cfg.brightness_level);
        int cx=d.width()/2; d.setTextDatum(top_center); d.setTextColor(TFT_LIGHTGREY); d.drawString("WebUI",cx,141);
        if(s.wifi==WifiStatus::CONNECTING)d.drawString("Wi-Fi connecting...",cx,155);
        else if(s.wifi==WifiStatus::FAILED){d.setTextColor(TFT_RED);d.drawString("Connection failed",cx,155);}
        else if(s.web_running){d.setTextColor(TFT_CYAN);d.drawString(("http://"+s.ip).c_str(),cx,154);d.setFont(&fonts::Font2);d.setTextColor(TFT_YELLOW);const auto password="Password  "+s.web_password;d.drawString(password.c_str(),cx,169);d.drawString(password.c_str(),cx+1,169);d.setFont(&fonts::efontJA_12);}
        button(d.height()-32,"Back",s.settings_selection==4);
    } else {
        bool secret=s.screen==Screen::TEXT_INPUT_PASS; d.setTextColor(TFT_WHITE); d.drawString(secret?"Enter PASS":"Enter SSID",8,7);
        std::string shown=secret?std::string(input.value().size(),'*'):input.value(); d.setTextColor(TFT_CYAN);d.setClipRect(8,30,d.width()-16,30);d.drawString(shown.c_str(),8,34);d.clearClipRect();
        int cx=d.width()/2,y=96;
        d.setTextDatum(middle_center); d.setTextSize(1); d.setTextColor(MUTED);
        for(int o=-3;o<=3;++o){
            if(!o)continue;
            auto value=input.item(o); if(value=="DEL")value="D";else if(value=="CANCEL")value="C";
            d.drawString(value.c_str(),cx+o*21,y+8);
        }
        auto selected=input.item();
        d.fillRoundRect(cx-22,y-13,44,42,6,PANEL); d.drawRoundRect(cx-22,y-13,44,42,6,TFT_YELLOW);
        d.setTextColor(TFT_YELLOW); d.setTextSize(selected.size()>2?1:2);
        d.drawString(selected.c_str(),cx,y+8); d.drawString(selected.c_str(),cx+1,y+8);
        d.setTextSize(1);d.setTextColor(TFT_LIGHTGREY);d.drawCentreString("Tilt L/R  F:OK  B:DEL",cx,d.height()-24);
    }
    d.endWrite();
}
