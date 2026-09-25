#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// BLE sensor device types (decode method per type)
enum class SensorType { ILLUMINANCE, ENV, ENOCEAN, XIAOMI_S400 };

inline constexpr size_t MAX_SENSORS = 5;
inline constexpr size_t MAX_CHANNELS = 3;

// One measurement channel: independent full-name index + field.
// index -> ES destination (URL), field -> JSON document key. Not concatenated.
struct SensorChannel {
    std::string index;  // full ES index name
    std::string field;  // full ES field name
};

// One registered BLE sensor device
struct SensorDevice {
    std::string name;
    std::array<uint8_t,6> mac{};
    SensorType type = SensorType::ILLUMINANCE;
    uint32_t rate_limit_ms = 30000;
    std::vector<SensorChannel> channels;  // per measurement channel (max MAX_CHANNELS)
    // Xiaomi S400 specific (single index, fixed fields weight_kg/body_fat_percent)
    std::string index;
    std::string bindkey;        // 32 hex chars (16 bytes)
    float height_cm = 0.0f;
    std::string birth_date;     // YYYY-MM-DD
    double body_fat_offset = 0.0;
};

// Elasticsearch connection
struct EsSettings {
    bool enabled = false;  // master switch for BLE reception + ES sending
    std::string url;
    std::string user;
    std::string password;
};
