#pragma once
#include <string>
#include "app_types.hpp"
#include "esp_event.h"

class WifiManager {
public:
    bool begin();
    void connect(const Settings&);
    void update();
    WifiStatus status() const { return status_; }
    const std::string& ip() const { return ip_; }
private:
    static void event(void*, esp_event_base_t, int32_t, void*);
    WifiStatus status_ = WifiStatus::IDLE;
    std::string ip_;
    int64_t started_us_ = 0;
};
