#include "wifi_manager.hpp"
#include <cstdio>
#include <cstring>
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

bool WifiManager::begin() {
    if (esp_netif_init() != ESP_OK) return false;
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, this);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, this);
    esp_wifi_set_mode(WIFI_MODE_STA); return esp_wifi_start() == ESP_OK;
}

void WifiManager::connect(const Settings& value) {
    wifi_config_t cfg{};
    std::strncpy(reinterpret_cast<char*>(cfg.sta.ssid), value.ssid.c_str(), sizeof(cfg.sta.ssid)-1);
    std::strncpy(reinterpret_cast<char*>(cfg.sta.password), value.pass.c_str(), sizeof(cfg.sta.password)-1);
    cfg.sta.threshold.authmode = value.pass.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;
    esp_wifi_disconnect(); esp_wifi_set_config(WIFI_IF_STA, &cfg);
    ip_.clear(); status_ = WifiStatus::CONNECTING; started_us_ = esp_timer_get_time(); esp_wifi_connect();
}

void WifiManager::update() {
    if (status_ == WifiStatus::CONNECTING && esp_timer_get_time() - started_us_ > 15000000) {
        status_ = WifiStatus::FAILED; esp_wifi_disconnect();
    }
}

void WifiManager::event(void* arg, esp_event_base_t base, int32_t id, void* data) {
    auto* self = static_cast<WifiManager*>(arg);
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* got = static_cast<ip_event_got_ip_t*>(data); char ip[16];
        std::snprintf(ip, sizeof(ip), IPSTR, IP2STR(&got->ip_info.ip));
        self->ip_ = ip; self->status_ = WifiStatus::CONNECTED;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && self->status_ == WifiStatus::CONNECTED) {
        self->ip_.clear(); self->status_ = WifiStatus::FAILED;
    }
}
