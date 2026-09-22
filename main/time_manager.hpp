#pragma once
#include <atomic>
#include <string>
#include "app_types.hpp"

class TimeManager {
public:
    void begin(const Settings& settings);
    void setWifiConnected(bool connected);
    void apply(const Settings& settings);
    bool synced() const { return synced_.load(); }
private:
    static void syncCallback(struct timeval*);
    void restart();
    static TimeManager* instance_;
    std::string server_;
    bool wifi_connected_ = false;
    bool running_ = false;
    std::atomic<bool> synced_{false};
};
