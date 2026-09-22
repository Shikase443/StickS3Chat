#include "time_manager.hpp"
#include <cstdlib>
#include <ctime>
#include "esp_sntp.h"

TimeManager* TimeManager::instance_ = nullptr;

void TimeManager::begin(const Settings& settings) { instance_ = this; apply(settings); }

void TimeManager::setWifiConnected(bool connected) {
    if (wifi_connected_ == connected) return;
    wifi_connected_ = connected;
    if (connected) restart();
}

void TimeManager::apply(const Settings& settings) {
    server_ = settings.ntp_server.empty() ? "pool.ntp.org" : settings.ntp_server;
    setenv("TZ", settings.timezone.empty() ? "UTC0" : settings.timezone.c_str(), 1);
    tzset(); synced_.store(false);
    if (wifi_connected_) restart();
}

void TimeManager::restart() {
    if (running_) esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, const_cast<char*>(server_.c_str()));
    esp_sntp_set_time_sync_notification_cb(syncCallback);
    esp_sntp_init(); running_ = true;
}

void TimeManager::syncCallback(struct timeval*) { if (instance_) instance_->synced_.store(true); }
