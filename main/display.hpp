#pragma once
#include "app_types.hpp"
#include "text_input.hpp"
#include "face_store.hpp"
class Display {
public:
    void begin();
    void setFaceStore(FaceStore* store) { face_store_ = store; }
    void draw(const AppState&, const Settings&, const TextInput&);
    void drawWifi(WifiStatus);
    void drawBattery(int);
    void drawDate(bool);
    void drawFace(FaceExpression);
    void drawMouth(FaceExpression);
    void drawRollingFace(FaceExpression, float cx, float cy, float angle, float scale);
    void drawConfigButton(bool);
    void drawForgetButton(bool);
    void drawConfirmButtons(bool yes_selected);
    void drawVoiceStatus(VoiceState, const std::string&, const std::string&);
private:
    FaceStore* face_store_ = nullptr;
};
