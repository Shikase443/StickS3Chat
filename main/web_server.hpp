#pragma once
#include <string>
#include "esp_http_server.h"
#include "app_types.hpp"

class WebServer {
public:
    using SaveCallback = void (*)(void*, const Settings&);
    bool start(const Settings& settings, SaveCallback callback, void* context);
    void stop();
    bool running() const { return server_ != nullptr; }
    const std::string& password() const { return password_; }
private:
    static esp_err_t root(httpd_req_t*);
    static esp_err_t login(httpd_req_t*);
    static esp_err_t settings(httpd_req_t*);
    static esp_err_t save(httpd_req_t*);
    static bool authenticated(httpd_req_t*, const WebServer*);
    httpd_handle_t server_ = nullptr;
    std::string password_, token_;
    uint32_t previous_ = 0;
    Settings settings_;
    SaveCallback save_callback_ = nullptr;
    void* save_context_ = nullptr;
};
