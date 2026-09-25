#pragma once
#include <cstdint>
#include <mutex>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "app_types.hpp"

struct cJSON;  // forward declaration

// BLE sensor reception + Elasticsearch transmission.
// Scan callback (NimBLE host task) -> queue -> worker task (decode + HTTP POST).
class SensorManager {
public:
    bool begin();
    void configure(const Settings& settings);
    void stop();
    bool running() const { return running_; }
private:
    struct RxItem {
        int sensor_index;
        int8_t rssi;
        uint16_t company_id;
        uint16_t payload_len;
        uint8_t payload[255];
    };
    struct S400Pending {
        bool active = false, weight = false, low = false, high = false;
        int64_t updated_us = 0;
        float weight_kg = 0, impedance_low = 0, impedance_high = 0;
    };
    static int gapEvent(struct ble_gap_event* event, void* arg);
    static void hostTask(void*);
    static void onSync();
    static void workerTask(void*);
    void startScan();
    void stopScan();
    void postJson(const std::string& index, cJSON* doc);
    void postValue(const SensorDevice& cfg, const SensorChannel& ch, float value);
    void processS400(int idx, const RxItem& item);
    int findSensor(const uint8_t addr[6]) const;
    static void isoUtc(char out[25]);
    static int ageFromBirth(const std::string& birth);

    Settings settings_;
    mutable std::mutex settings_mutex_;
    QueueHandle_t rx_queue_ = nullptr;
    uint32_t last_emit_ms[MAX_SENSORS]{};
    uint8_t last_s400_packet[MAX_SENSORS]{};
    bool have_s400_packet[MAX_SENSORS]{};
    S400Pending s400_pending_[MAX_SENSORS]{};
    bool running_ = false;
    bool host_ready_ = false;
    bool scanning_ = false;
};
