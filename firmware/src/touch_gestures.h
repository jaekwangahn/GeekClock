#pragma once

#include <stdint.h>

enum class Gesture : uint8_t { None, Single, Double, Long };

// Consumes an already-debounced touch level. A single tap waits for the double
// window, so the first half of a double never triggers the single-tap action.
class TouchGestures {
public:
    Gesture update(bool pressed, uint32_t nowMs) {
        Gesture result = Gesture::None;
        if (pressed && !pressed_) {
            // Expire an old tap even if this call is the next touch edge.
            if (pendingTap_ && uint32_t(nowMs - releasedAtMs_) > DOUBLE_MS) {
                pendingTap_ = false;
                result = Gesture::Single;
            }
            secondTap_ = pendingTap_;
            if (secondTap_) pendingTap_ = false;
            pressedAtMs_ = nowMs;
            longSent_ = false;
        }

        if (pressed && !longSent_ && uint32_t(nowMs - pressedAtMs_) >= LONG_MS) {
            longSent_ = true;
            pendingTap_ = false;
            secondTap_ = false;
            result = Gesture::Long;
        }

        if (!pressed && pressed_) {
            if (!longSent_) {
                // Account for a blocked loop that missed the held-time tick.
                if (uint32_t(nowMs - pressedAtMs_) >= LONG_MS) {
                    pendingTap_ = false;
                    secondTap_ = false;
                    result = Gesture::Long;
                } else if (secondTap_) {
                    secondTap_ = false;
                    result = Gesture::Double;
                } else {
                    pendingTap_ = true;
                    releasedAtMs_ = nowMs;
                }
            }
            longSent_ = false;
        }
        pressed_ = pressed;

        if (!pressed && pendingTap_ && uint32_t(nowMs - releasedAtMs_) > DOUBLE_MS) {
            pendingTap_ = false;
            result = Gesture::Single;
        }
        return result;
    }

    static constexpr uint32_t DOUBLE_MS = 350;
    static constexpr uint32_t LONG_MS = 900;

private:
    uint32_t pressedAtMs_ = 0;
    uint32_t releasedAtMs_ = 0;
    bool pressed_ = false;
    bool pendingTap_ = false;
    bool secondTap_ = false;
    bool longSent_ = false;
};
