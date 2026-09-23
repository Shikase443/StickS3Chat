#include "face_store.hpp"
#include <cstdio>
#include "esp_littlefs.h"

namespace {
constexpr const char* MOUNT = "/faces";
constexpr const char* PARTITION = "storage";
}

const char* FaceStore::path(FaceExpression expression) {
    switch (expression) {
        case FaceExpression::NORMAL: return "/faces/normal.rgb565";
        case FaceExpression::SMILE: return "/faces/smile.rgb565";
        case FaceExpression::SURPRISED: return "/faces/surprised.rgb565";
        case FaceExpression::MOUTH_MEDIUM: return "/faces/mouth_medium.rgb565";
        case FaceExpression::MOUTH_LARGE: return "/faces/mouth_large.rgb565";
    }
    return nullptr;
}

bool FaceStore::begin() {
    if (mounted_) return true;
    esp_vfs_littlefs_conf_t conf{};
    conf.base_path = MOUNT;
    conf.partition_label = PARTITION;
    conf.format_if_mount_failed = true;
    conf.grow_on_mount = true;
    if (esp_vfs_littlefs_register(&conf) != ESP_OK) return false;
    mounted_ = true;
    return true;
}

bool FaceStore::saveFace(FaceExpression expression, const uint8_t* data, size_t length) {
    if (!mounted_ || !data || length != FACE_BYTES) return false;
    const char* file = path(expression);
    if (!file) return false;
    FILE* stream = std::fopen(file, "wb");
    if (!stream) return false;
    size_t written = std::fwrite(data, 1, FACE_BYTES, stream);
    std::fclose(stream);
    return written == FACE_BYTES;
}

bool FaceStore::loadFace(FaceExpression expression, uint8_t* buffer, size_t length) const {
    if (!mounted_ || !buffer || length != FACE_BYTES) return false;
    const char* file = path(expression);
    if (!file) return false;
    FILE* stream = std::fopen(file, "rb");
    if (!stream) return false;
    if (std::fseek(stream, 0, SEEK_END) != 0) { std::fclose(stream); return false; }
    long size = std::ftell(stream);
    if (size != (long)FACE_BYTES) { std::fclose(stream); return false; }
    std::fseek(stream, 0, SEEK_SET);
    size_t read = std::fread(buffer, 1, FACE_BYTES, stream);
    std::fclose(stream);
    return read == FACE_BYTES;
}

bool FaceStore::hasFace(FaceExpression expression) const {
    if (!mounted_) return false;
    const char* file = path(expression);
    if (!file) return false;
    FILE* stream = std::fopen(file, "rb");
    if (!stream) return false;
    bool ok = false;
    if (std::fseek(stream, 0, SEEK_END) == 0) {
        long size = std::ftell(stream);
        ok = size == (long)FACE_BYTES;
    }
    std::fclose(stream);
    return ok;
}



bool FaceStore::deleteFace(FaceExpression expression) {
    if (!mounted_) return false;
    const char* file = path(expression);
    if (!file) return false;
    return std::remove(file) == 0;
}

bool FaceStore::deleteAllFaces() {
    if (!mounted_) return false;
    bool ok = true;
    ok &= deleteFace(FaceExpression::NORMAL);
    ok &= deleteFace(FaceExpression::SMILE);
    ok &= deleteFace(FaceExpression::SURPRISED);
    ok &= deleteFace(FaceExpression::MOUTH_MEDIUM);
    ok &= deleteFace(FaceExpression::MOUTH_LARGE);
    return ok;
}
