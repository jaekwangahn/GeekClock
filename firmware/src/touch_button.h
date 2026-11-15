#pragma once

#include <stdint.h>

// Classic ESP32 touch readings decrease on contact. Feed filtered samples at
// 20 ms intervals; emit one event per debounced press, then wait for release.
class TouchButton {
public:
    bool update(uint16_t raw, uint32_t nowMs) {
        raw_ = raw;
        if (raw == 0 || raw == UINT16_MAX) {
            // Invalid reads must not count toward a stable press or release.
            candidate_ = pressed_;
            candidateSinceMs_ = nowMs;
            return false;
        }
        if (!ready()) {
            calibrationSum_ += raw;
            if (++calibrationSamples_ == 24) {
                baselineQ8_ = (calibrationSum_ << 8) / calibrationSamples_;
            }
            return false;
        }

        const uint32_t threshold = baselineQ8_ * (pressed_ ? 90 : 85) / 100;
        const bool contact = (uint32_t(raw) << 8) < threshold;
        if (contact != candidate_) {
            candidate_ = contact;
            candidateSinceMs_ = nowMs;
        }
        if (candidate_ != pressed_ && uint32_t(nowMs - candidateSinceMs_) >= 60) {
            pressed_ = candidate_;
            if (pressed_) {
                ++pressCount_;
                return true;
            }
        }
        // Slowly follow ambient drift only while released, never during a hold.
        if (!pressed_ && !candidate_) {
            baselineQ8_ = (baselineQ8_ * 63 + (uint32_t(raw) << 8)) / 64;
        }
        return false;
    }

    bool ready() const { return calibrationSamples_ == 24; }
    bool pressed() const { return pressed_; }
    uint16_t raw() const { return raw_; }
    uint16_t baseline() const { return baselineQ8_ >> 8; }
    uint32_t pressCount() const { return pressCount_; }

private:
    uint16_t raw_ = 0;
    uint8_t calibrationSamples_ = 0;
    uint32_t calibrationSum_ = 0;
    uint32_t baselineQ8_ = 0;
    uint32_t candidateSinceMs_ = 0;
    uint32_t pressCount_ = 0;
    bool pressed_ = false;
    bool candidate_ = false;
};
