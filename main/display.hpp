#pragma once
#include "app_types.hpp"
#include "text_input.hpp"
class Display {
public:
    void begin();
    void draw(const AppState&, const Settings&, const TextInput&);
    void drawWifi(WifiStatus);
    void drawBattery(int);
    void drawDate(bool);
    void drawClock(bool);
    void drawFace(FaceExpression);
    void drawMouth(FaceExpression);
    void drawConfigButton(bool);
    void drawVoiceStatus(VoiceState, const std::string&, const std::string&);
};
