#pragma once
#include <cstddef>
#include <cstdint>
#include "app_types.hpp"

class FaceStore {
public:
    static constexpr size_t FACE_BYTES = 114 * 114 * 2;
    bool begin();
    bool saveFace(FaceExpression expression, const uint8_t* data, size_t length);
    bool loadFace(FaceExpression expression, uint8_t* buffer, size_t length) const;
    bool hasFace(FaceExpression expression) const;
    bool deleteFace(FaceExpression expression);
    bool deleteAllFaces();
private:
    static const char* path(FaceExpression expression);
    bool mounted_ = false;
};
