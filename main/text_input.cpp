#include "text_input.hpp"
void TextInput::begin(const std::string& value, bool secret) {
    value_ = value; secret_ = secret; index_ = 3; tilt_direction_ = 0; vertical_direction_ = 0; next_move_us_ = 0; items_.clear();
    for (char c = 'a'; c <= 'z'; ++c) items_.emplace_back(1, c);
    for (char c = 'A'; c <= 'Z'; ++c) items_.emplace_back(1, c);
    for (char c = '0'; c <= '9'; ++c) items_.emplace_back(1, c);
    constexpr char SYMBOLS[] = " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    for (const char* c = SYMBOLS; *c; ++c) items_.emplace_back(1, *c);
    items_.push_back("DEL"); items_.push_back("OK"); items_.push_back("CANCEL");
}

bool TextInput::moveByTilt(float horizontal_accel, float forward_accel, int64_t now) {
    constexpr float START_THRESHOLD = 0.28f;
    constexpr float STOP_THRESHOLD = 0.18f;
    constexpr float VERTICAL_START_THRESHOLD = 0.48f;
    constexpr float VERTICAL_STOP_THRESHOLD = 0.32f;
    constexpr int64_t FIRST_REPEAT_US = 450000;
    constexpr int64_t REPEAT_US = 200000;

    int vertical = 0;
    if (vertical_direction_ == 0) {
        if (forward_accel > VERTICAL_START_THRESHOLD) vertical = 1;
        else if (forward_accel < -VERTICAL_START_THRESHOLD) vertical = -1;
    } else if (forward_accel > VERTICAL_STOP_THRESHOLD) vertical = 1;
    else if (forward_accel < -VERTICAL_STOP_THRESHOLD) vertical = -1;

    if (vertical != 0) {
        tilt_direction_ = 0;
        next_move_us_ = 0;
        if (vertical == vertical_direction_) return false;
        vertical_direction_ = vertical;
        const std::string target = vertical > 0 ? "OK" : "DEL";
        for (size_t i = 0; i < items_.size(); ++i) {
            if (items_[i] == target) { index_ = static_cast<int>(i); return true; }
        }
    }
    vertical_direction_ = 0;

    int direction = 0;
    if (tilt_direction_ == 0) {
        if (horizontal_accel > START_THRESHOLD) direction = -1;
        else if (horizontal_accel < -START_THRESHOLD) direction = 1;
    } else if (horizontal_accel > STOP_THRESHOLD) direction = -1;
    else if (horizontal_accel < -STOP_THRESHOLD) direction = 1;

    if (direction == 0) {
        tilt_direction_ = 0;
        next_move_us_ = 0;
        return false;
    }
    if (direction != tilt_direction_) {
        tilt_direction_ = direction;
        next_move_us_ = now + FIRST_REPEAT_US;
    } else if (now < next_move_us_) {
        return false;
    } else {
        next_move_us_ = now + REPEAT_US;
    }
    index_ = (index_ + static_cast<int>(items_.size()) + direction) % items_.size();
    return true;
}

TextInput::Action TextInput::activate() {
    const auto& selected = items_[index_];
    if (selected == "OK") return Action::OK;
    if (selected == "CANCEL") return Action::CANCEL;
    if (selected == "DEL") { if (!value_.empty()) value_.pop_back(); return Action::CHANGED; }
    if (value_.size() < 63) value_ += selected;
    return Action::CHANGED;
}

std::string TextInput::item(int offset) const {
    int n = items_.size(); return items_[(index_ + offset % n + n) % n];
}
