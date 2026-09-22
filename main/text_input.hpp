#pragma once
#include <string>
#include <vector>

class TextInput {
public:
    enum class Action { NONE, CHANGED, OK, CANCEL };
    void begin(const std::string& value, bool secret);
    bool moveByTilt(float horizontal_accel, float forward_accel, int64_t now);
    Action activate();
    std::string item(int offset = 0) const;
    const std::string& value() const { return value_; }
    bool secret() const { return secret_; }
private:
    std::vector<std::string> items_;
    std::string value_;
    int index_ = 0;
    bool secret_ = false;
    int tilt_direction_ = 0;
    int vertical_direction_ = 0;
    int64_t next_move_us_ = 0;
};
