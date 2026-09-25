#pragma once
#include <atomic>
#include <string>
#include "esp_http_server.h"
#include "app_types.hpp"
#include "face_store.hpp"
#include "ir_api.hpp"
#include "voice_chat.hpp"

class WebServer {
public:
    using SaveCallback = void (*)(void*, const Settings&);
    bool start(const Settings& settings, SaveCallback callback, void* context);
    void stop();
    bool running() const { return server_ != nullptr; }
    const std::string& password() const { return password_; }
    void setFaceStore(FaceStore* store) { face_store_ = store; }
    void setIrApi(IrApi* ir) { ir_api_ = ir; }
    void setVoiceChat(VoiceChat* voice) { voice_ = voice; }
    // 設定画面のアクセス許可（Config画面遷移時のみtrue）。
    void setSettingsEnabled(bool enabled) { settings_enabled_.store(enabled); }
private:
    static esp_err_t root(httpd_req_t*);
    static esp_err_t login(httpd_req_t*);
    static esp_err_t settings(httpd_req_t*);
    static esp_err_t save(httpd_req_t*);
    static esp_err_t upload(httpd_req_t*);
    static esp_err_t irSend(httpd_req_t*);
    static esp_err_t irLearn(httpd_req_t*);
    static esp_err_t apiSpeak(httpd_req_t*);
    static bool authenticated(httpd_req_t*, const WebServer*);
    static bool settingsAccessDenied(httpd_req_t*, const WebServer*);
    static bool irAuthorized(httpd_req_t*, const WebServer*);
    httpd_handle_t server_ = nullptr;
    std::string password_, token_;
    uint32_t previous_ = 0;
    Settings settings_;
    SaveCallback save_callback_ = nullptr;
    void* save_context_ = nullptr;
    FaceStore* face_store_ = nullptr;
    IrApi* ir_api_ = nullptr;
    VoiceChat* voice_ = nullptr;
    std::atomic<bool> settings_enabled_{false};
};
