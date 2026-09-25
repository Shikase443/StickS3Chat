#include "sensor_decode.hpp"
#include <cstdio>
#include <cstring>
#include "psa/crypto.h"

namespace {
// Walk BLE AD structures; find the one with the wanted AD type.
bool findAd(const uint8_t* adv, size_t len, uint8_t wanted,
            const uint8_t** value, size_t* value_len) {
    for (size_t i = 0; i < len;) {
        uint8_t n = adv[i];
        if (!n || i + 1U + n > len) return false;
        if (n >= 1 && adv[i + 1] == wanted) {
            *value = &adv[i + 2]; *value_len = n - 1U; return true;
        }
        i += 1U + n;
    }
    return false;
}

bool hexKey(const char* s, uint8_t key[16]) {
    if (!s || std::strlen(s) != 32) return false;
    for (int i = 0; i < 16; i++) {
        unsigned v;
        if (std::sscanf(s + i * 2, "%2x", &v) != 1) return false;
        key[i] = (uint8_t)v;
    }
    return true;
}
}  // namespace

bool advFindManufacturer(const uint8_t* adv, size_t len, uint16_t* company_id,
                         const uint8_t** payload, size_t* payload_len) {
    const uint8_t* v; size_t n;
    if (!findAd(adv, len, 0xff, &v, &n) || n < 2) return false;
    *company_id = (uint16_t)(v[0] | ((uint16_t)v[1] << 8));
    *payload = v + 2; *payload_len = n - 2; return true;
}

bool advFindServiceData16(const uint8_t* adv, size_t len, uint16_t uuid,
                          const uint8_t** payload, size_t* payload_len) {
    const uint8_t* v; size_t n;
    if (!findAd(adv, len, 0x16, &v, &n) || n < 2) return false;
    if ((uint16_t)(v[0] | ((uint16_t)v[1] << 8)) != uuid) return false;
    *payload = v + 2; *payload_len = n - 2; return true;
}

SensorValues sensorDecode(SensorType type, const uint8_t* p, size_t n) {
    SensorValues out;
    if (type == SensorType::ILLUMINANCE && n >= 10) {
        uint16_t raw = (uint16_t)(((uint16_t)p[8] << 8) | p[9]) & 0x0fff;
        out.value[0] = (raw >> 11) ? (float)((int)raw - 2047) * 50 + 2000 : (float)raw;
        out.valid[0] = true; out.count = 1;
    } else if (type == SensorType::ENV && n >= 11) {
        out.value[0] = (float)(p[10] & 0x7f);
        float t = (float)(p[9] & 0x7f) + (float)(p[8] & 0x07) / 10.0f;
        out.value[1] = (p[9] & 0x80) ? t : -t;
        out.valid[0] = out.valid[1] = true; out.count = 2;
    } else if (type == SensorType::ENOCEAN && n >= 12) {
        out.value[0] = (float)p[8] * 0.5f;
        out.value[1] = (float)(((uint16_t)p[6] << 8) | p[5]) * 0.01f;
        out.value[2] = (float)(((uint16_t)p[11] << 8) | p[10]);
        out.valid[0] = out.valid[1] = out.valid[2] = true; out.count = 3;
    }
    return out;
}

bool s400Decode(const uint8_t* d, size_t n, const std::array<uint8_t,6>& mac,
                const std::string& key_hex, S400Values* out) {
    *out = S400Values{};
    if (n < 14) return false;
    uint16_t fc = (uint16_t)(d[0] | ((uint16_t)d[1] << 8));
    if ((fc >> 12) < 4 || !(fc & (1 << 6)) || !(fc & (1 << 3))) return false;  // v4/v5, encrypted, has payload
    size_t i = 5;
    if (fc & (1 << 4)) {  // has MAC
        if (n < i + 6) return false;
        for (int k = 0; k < 6; k++) if (d[i + k] != mac[5 - k]) return false;
        i += 6;
    }
    if (fc & (1 << 5)) {  // has capabilities
        if (n < i + 1) return false;
        uint8_t cap = d[i++];
        if (cap & 0x20) i++;
    }
    if (n < i + 7) return false;
    uint8_t key[16], nonce[12], plain[255], cipher_and_tag[259];
    if (!hexKey(key_hex.c_str(), key) || n - i - 7 > sizeof(plain)) return false;
    for (int k = 0; k < 6; k++) nonce[k] = mac[5 - k];
    std::memcpy(nonce + 6, d + 2, 3);
    std::memcpy(nonce + 9, d + n - 7, 3);
    static const uint8_t aad = 0x11;
    size_t cipher_len = n - i - 7, plain_len = 0;
    std::memcpy(cipher_and_tag, d + i, cipher_len);
    std::memcpy(cipher_and_tag + cipher_len, d + n - 4, 4);
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id = 0;
    psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attr, 128);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DECRYPT);
    psa_algorithm_t alg = PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 4);
    psa_set_key_algorithm(&attr, alg);
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_import_key(&attr, key, sizeof(key), &key_id) != PSA_SUCCESS) {
        psa_reset_key_attributes(&attr);
        return false;
    }
    psa_status_t status = psa_aead_decrypt(key_id, alg, nonce, sizeof(nonce), &aad, 1,
                                           cipher_and_tag, cipher_len + 4,
                                           plain, sizeof(plain), &plain_len);
    psa_destroy_key(key_id);
    psa_reset_key_attributes(&attr);
    if (status != PSA_SUCCESS || plain_len != cipher_len) return false;
    for (size_t off = 0; off + 3 <= cipher_len;) {
        uint16_t id = (uint16_t)(plain[off] | ((uint16_t)plain[off + 1] << 8));
        uint8_t l = plain[off + 2];
        if (off + 3U + l > cipher_len) break;
        if (id == 0x6e16 && l == 9) {
            uint32_t packed = (uint32_t)plain[off + 4] | ((uint32_t)plain[off + 5] << 8) |
                              ((uint32_t)plain[off + 6] << 16) | ((uint32_t)plain[off + 7] << 24);
            uint32_t mass = packed & 0x7ff, imp = packed >> 18;
            if (mass) { out->weight = (float)mass / 10.0f; out->weight_valid = true; }
            if (imp) {
                if (mass) { out->impedance_low = (float)imp / 10.0f; out->low_valid = true; }
                else { out->impedance_high = (float)imp / 10.0f; out->high_valid = true; }
            }
        }
        off += 3U + l;
    }
    return true;
}
