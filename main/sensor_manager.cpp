#include "sensor_manager.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <time.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include "nimble/nimble_port.h"
#include "host/util/util.h"
#include "nimble/nimble_port_freertos.h"
#include "sensor_decode.hpp"

namespace {
constexpr char TAG[] = "sensor";
constexpr uint16_t MIBEACON_UUID = 0xfe95;
constexpr size_t QUEUE_LEN = 30;
constexpr size_t PAYLOAD_MAX = 255;
}  // namespace

static SensorManager* g_sensor = nullptr;

void SensorManager::isoUtc(char out[25]) {
    std::time_t now; std::tm tm;
    std::time(&now); gmtime_r(&now, &tm);
    std::strftime(out, 25, "%Y-%m-%dT%H:%M:%S.000Z", &tm);
}

int SensorManager::ageFromBirth(const std::string& birth) {
    int y, m, d;
    if (std::sscanf(birth.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return -1;
    std::time_t now; std::tm t;
    std::time(&now); gmtime_r(&now, &t);
    int age = (t.tm_year + 1900) - y;
    if ((t.tm_mon + 1) < m || ((t.tm_mon + 1) == m && t.tm_mday < d)) age--;
    return age;
}

int SensorManager::findSensor(const uint8_t addr[6]) const {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    for (size_t s = 0; s < settings_.sensors.size(); s++) {
        const auto& dev = settings_.sensors[s];
        bool same = true, reversed = true;
        for (int i = 0; i < 6; i++) {
            same &= (addr[i] == dev.mac[i]);
            reversed &= (addr[i] == dev.mac[5 - i]);
        }
        if (same || reversed) return (int)s;
    }
    return -1;
}

void SensorManager::postJson(const std::string& index, cJSON* doc) {
    std::string url = settings_.es.url + "/" + index + "/_doc/";
    char* body = cJSON_PrintUnformatted(doc);
    if (!body) return;
    std::string credentials = settings_.es.user + ":" + settings_.es.password;
    std::string auth("Basic ");
    size_t olen = 0;
    auth.resize(6 + credentials.size() * 2);
    mbedtls_base64_encode(reinterpret_cast<unsigned char*>(auth.data() + 6), auth.size() - 6, &olen,
                          reinterpret_cast<const unsigned char*>(credentials.data()), credentials.size());
    auth.resize(6 + olen);
    esp_http_client_config_t cfg{};
    cfg.url = url.c_str();
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) { cJSON_free(body); return; }
    esp_http_client_set_method(h, HTTP_METHOD_POST);
    esp_http_client_set_header(h, "Content-Type", "application/json");
    esp_http_client_set_header(h, "Authorization", auth.c_str());
    esp_http_client_set_post_field(h, body, strlen(body));
    esp_err_t err = esp_http_client_perform(h);
    int status = esp_http_client_get_status_code(h);
    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "POST %s failed: %s HTTP %d", index.c_str(), esp_err_to_name(err), status);
    } else {
        ESP_LOGI(TAG, "POST %s: HTTP %d", index.c_str(), status);
    }
    esp_http_client_cleanup(h);
    cJSON_free(body);
}

void SensorManager::postValue(const SensorDevice& cfg, const SensorChannel& ch, float value) {
    char date[25];
    isoUtc(date);
    cJSON* doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "date", date);
    char mac[13];
    std::snprintf(mac, sizeof(mac), "%02x%02x%02x%02x%02x%02x",
                  cfg.mac[0], cfg.mac[1], cfg.mac[2], cfg.mac[3], cfg.mac[4], cfg.mac[5]);
    cJSON_AddStringToObject(doc, "device", mac);
    if (ch.field.empty() || ch.index.empty()) { cJSON_Delete(doc); return; }
    cJSON_AddNumberToObject(doc, ch.field.c_str(), value);
    postJson(ch.index, doc);
    cJSON_Delete(doc);
}

void SensorManager::processS400(int idx, const RxItem& item) {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    if (idx < 0 || idx >= (int)settings_.sensors.size()) return;
    const SensorDevice& cfg = settings_.sensors[idx];
    S400Values v;
    if (!s400Decode(item.payload, item.payload_len, cfg.mac, cfg.bindkey, &v)) {
        ESP_LOGW(TAG, "S400 decrypt failed (check bindkey)");
        return;
    }
    S400Pending& p = s400_pending_[idx];
    int64_t now = esp_timer_get_time();
    if (!p.active || now - p.updated_us > 30000000LL) p = S400Pending{};
    p.active = true;
    p.updated_us = now;
    if (v.weight_valid) { p.weight = true; p.weight_kg = v.weight; }
    if (v.low_valid) { p.low = true; p.impedance_low = v.impedance_low; }
    if (v.high_valid) { p.high = true; p.impedance_high = v.impedance_high; }
    if (!(p.weight && p.low && p.high)) return;
    float w = p.weight_kg;
    float imp = std::fmaxf(p.impedance_low, p.impedance_high);
    int age = ageFromBirth(cfg.birth_date);
    if (age < 0) age = 0;
    float lean = (cfg.height_cm * 9.058f / 100.0f) * (cfg.height_cm / 100.0f)
                 + w * 0.32f + 12.226f - imp * 0.0068f - age * 0.0542f;
    lean = std::fminf(lean, w * 0.98f);
    float fat = (w - lean) / w * 100.0f + cfg.body_fat_offset;
    fat = std::roundf(std::fmaxf(5.0f, std::fminf(fat, 75.0f)) * 10.0f) / 10.0f;
    char date[25];
    isoUtc(date);
    cJSON* doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "date", date);
    cJSON_AddNumberToObject(doc, "weight_kg", w);
    cJSON_AddNumberToObject(doc, "body_fat_percent", fat);
    postJson(cfg.index, doc);
    cJSON_Delete(doc);
    p = S400Pending{};
}

void SensorManager::workerTask(void* arg) {
    auto* self = static_cast<SensorManager*>(arg);
    RxItem item;
    while (xQueueReceive(self->rx_queue_, &item, portMAX_DELAY) == pdTRUE) {
        std::lock_guard<std::mutex> lock(self->settings_mutex_);
        if (item.sensor_index < 0 || item.sensor_index >= (int)self->settings_.sensors.size()) continue;
        const SensorDevice& cfg = self->settings_.sensors[item.sensor_index];
        ESP_LOGI(TAG, "%s RSSI=%d", cfg.name.c_str(), item.rssi);
        if (cfg.type == SensorType::XIAOMI_S400) {
            self->processS400(item.sensor_index, item);
            continue;
        }
        SensorValues v = sensorDecode(cfg.type, item.payload, item.payload_len);
        if (!v.count) { ESP_LOGW(TAG, "invalid payload for %s", cfg.name.c_str()); continue; }
        for (size_t i = 0; i < v.count && i < cfg.channels.size(); i++)
            if (v.valid[i]) self->postValue(cfg, cfg.channels[i], v.value[i]);
    }
    vTaskDelete(nullptr);
}

int SensorManager::gapEvent(struct ble_gap_event* event, void* arg) {
    if (event->type != BLE_GAP_EVENT_DISC) return 0;
    auto* self = static_cast<SensorManager*>(arg);
    const struct ble_gap_disc_desc* d = &event->disc;
    int idx = self->findSensor(d->addr.val);
    if (idx < 0) return 0;
    const uint8_t* p = nullptr;
    size_t n = 0;
    uint16_t company = 0;
    {
        std::lock_guard<std::mutex> lock(self->settings_mutex_);
        if (idx >= (int)self->settings_.sensors.size()) return 0;
        const SensorDevice& cfg = self->settings_.sensors[idx];
        if (cfg.type == SensorType::XIAOMI_S400) {
            if (!advFindServiceData16(d->data, d->length_data, MIBEACON_UUID, &p, &n) || n < 5 || !(p[0] & (1 << 6))) return 0;
            if (self->have_s400_packet[idx] && self->last_s400_packet[idx] == p[4]) return 0;
            self->have_s400_packet[idx] = true;
            self->last_s400_packet[idx] = p[4];
        } else {
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            if (self->last_emit_ms[idx] && now - self->last_emit_ms[idx] < cfg.rate_limit_ms) return 0;
            if (!advFindManufacturer(d->data, d->length_data, &company, &p, &n)) return 0;
            self->last_emit_ms[idx] = now;
        }
    }
    if (n > PAYLOAD_MAX) return 0;
    RxItem item{};
    item.sensor_index = idx;
    item.rssi = d->rssi;
    item.company_id = company;
    item.payload_len = (uint16_t)n;
    std::memcpy(item.payload, p, n);
    if (xQueueSend(self->rx_queue_, &item, 0) != pdTRUE) {
        RxItem old;
        xQueueReceive(self->rx_queue_, &old, 0);
        xQueueSend(self->rx_queue_, &item, 0);
    }
    return 0;
}

void SensorManager::startScan() {
    uint8_t own;
    if (ble_hs_id_infer_auto(0, &own) != 0) { ESP_LOGE(TAG, "no BLE identity"); return; }
    struct ble_gap_disc_params p{};
    p.passive = 1;
    p.itvl = 0x30;
    p.window = 0x30;
    p.filter_duplicates = 0;
    int rc = ble_gap_disc(own, BLE_HS_FOREVER, &p, gapEvent, this);
    ESP_LOGI(TAG, "BLE scan start rc=%d", rc);
}

void SensorManager::onSync() {
    ble_hs_util_ensure_addr(0);
    if (!g_sensor) return;
    g_sensor->host_ready_ = true;
    std::lock_guard<std::mutex> lock(g_sensor->settings_mutex_);
    if (g_sensor->settings_.es.enabled && !g_sensor->scanning_) {
        g_sensor->startScan();
        g_sensor->scanning_ = true;
    }
}

void SensorManager::stopScan() {
    int rc = ble_gap_disc_cancel();
    ESP_LOGI(TAG, "BLE scan stop rc=%d", rc);
}

void SensorManager::hostTask(void* p) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool SensorManager::begin() {
    if (running_) return true;
    {std::lock_guard<std::mutex> lock(settings_mutex_);
    if (!settings_.es.enabled) return true;  // BLE無効時は起動しない
    }
    rx_queue_ = xQueueCreate(QUEUE_LEN, sizeof(RxItem));
    if (!rx_queue_) return false;
    if (xTaskCreate(workerTask, "sensor_worker", 8192, this, 4, nullptr) != pdPASS) return false;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err)); return false; }
    g_sensor = this;
    ble_hs_cfg.sync_cb = onSync;
    nimble_port_freertos_init(hostTask);
    running_ = true;
    return true;
}

void SensorManager::configure(const Settings& settings) {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    settings_ = settings;
    if (settings_.sensors.size() > MAX_SENSORS) settings_.sensors.resize(MAX_SENSORS);
    if (settings_.es.enabled && host_ready_ && !scanning_) {
        startScan();
        scanning_ = true;
    } else if (!settings_.es.enabled && scanning_) {
        stopScan();
        scanning_ = false;
    }
}

void SensorManager::stop() {
    running_ = false;
}
