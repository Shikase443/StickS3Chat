#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
#include "sensor_types.hpp"

// Decoded values for SwitchBot/EnOcean (up to 3 fields)
struct SensorValues {
    float value[3]{};
    bool valid[3]{};
    size_t count = 0;
};

// Decoded Xiaomi S400 values
struct S400Values {
    bool weight_valid = false, low_valid = false, high_valid = false;
    float weight = 0.0f, impedance_low = 0.0f, impedance_high = 0.0f;
};

// Find Manufacturer Specific Data (AD type 0xff). Returns payload after 2-byte company ID.
bool advFindManufacturer(const uint8_t* adv, size_t len, uint16_t* company_id,
                         const uint8_t** payload, size_t* payload_len);
// Find 16-bit Service Data (AD type 0x16) matching uuid.
bool advFindServiceData16(const uint8_t* adv, size_t len, uint16_t uuid,
                          const uint8_t** payload, size_t* payload_len);
// Decode SwitchBot/EnOcean payload into up to 3 values.
SensorValues sensorDecode(SensorType type, const uint8_t* payload, size_t len);
// Decrypt + decode Xiaomi S400 MiBeacon v4/v5 (AES-128-CCM, 4-byte tag).
bool s400Decode(const uint8_t* data, size_t len, const std::array<uint8_t,6>& mac,
                const std::string& key_hex, S400Values* out);
