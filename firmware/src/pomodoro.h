#pragma once

#include <stdint.h>

struct PomodoroData {
    uint16_t focusMinutes = 25;
    uint16_t shortBreakMinutes = 5;
    uint16_t longBreakMinutes = 15;
    uint8_t phase = 0;  // 0: focus, 1: short break, 2: long break
    uint8_t cycle = 0;  // Completed focus sessions toward the next long break.
    uint32_t remainingMs = 25UL * 60000UL;
    uint32_t completedFocus = 0;
    uint32_t totalFocusMinutes = 0;
    bool running = false;
};

enum class PomodoroEvent : uint8_t {
    None,
    FocusComplete,
    ShortBreakComplete,
    LongBreakComplete
};

inline uint32_t focusRewardCoins(uint16_t minutes) {
    if (minutes > 90) minutes = 90;
    const uint32_t coins = uint32_t(minutes) * 2 / 5;
    return coins ? coins : 1;
}

// Pure countdown state. The caller grants rewards only when tick() returns a
// FocusComplete event, then persists the timer and wallet together.
class PomodoroTimer {
public:
    PomodoroData data;

    static const char* phaseName(uint8_t phase) {
        return phase == 1 ? "short_break" : phase == 2 ? "long_break" : "focus";
    }
    const char* phaseName() const { return phaseName(data.phase); }

    uint32_t phaseDurationMs() const {
        const uint16_t minutes = data.phase == 1 ? data.shortBreakMinutes :
                                 data.phase == 2 ? data.longBreakMinutes : data.focusMinutes;
        return uint32_t(minutes) * 60000UL;
    }
    uint16_t lastCompletedFocusMinutes() const { return lastCompletedFocusMinutes_; }

    // Stored running timers intentionally resume paused. No reward or elapsed
    // time is inferred from time spent powered off or from the wall clock.
    void restore() { sanitize(); }
    void sanitize() {
        data.focusMinutes = clamp(data.focusMinutes, 1, 90);
        data.shortBreakMinutes = clamp(data.shortBreakMinutes, 1, 30);
        data.longBreakMinutes = clamp(data.longBreakMinutes, 1, 60);
        if (data.phase > 2) data.phase = 0;
        if (data.cycle > 3) data.cycle = 3;
        const uint32_t full = phaseDurationMs();
        if (!data.remainingMs || data.remainingMs > full) data.remainingMs = full;
        data.running = false;
        pending_ = PomodoroEvent::None;
        lastCompletedFocusMinutes_ = 0;
        tickAtMs_ = 0;
    }

    bool start(uint32_t nowMs) {
        if (data.running || pending_ != PomodoroEvent::None) return false;
        if (!data.remainingMs) data.remainingMs = phaseDurationMs();
        data.running = true;
        tickAtMs_ = nowMs;
        return true;
    }

    bool pause(uint32_t nowMs) {
        if (!data.running) return false;
        advance(nowMs);
        data.running = false;
        return true;
    }

    bool reset(uint32_t nowMs) {
        advance(nowMs);
        data.running = false;
        data.phase = 0;
        data.cycle = 0;
        data.remainingMs = phaseDurationMs();
        tickAtMs_ = nowMs;
        return true;
    }

    bool skip(uint32_t nowMs) {
        advance(nowMs);
        // If elapsed time just completed a phase, do not skip the following
        // phase too. Its completion event still needs to reach the caller.
        if (pending_ != PomodoroEvent::None) return false;
        data.running = false;
        data.phase = data.phase == 0 ? 1 : 0;
        data.remainingMs = phaseDurationMs();
        tickAtMs_ = nowMs;
        return true;
    }

    bool configure(uint16_t focus, uint16_t shortBreak, uint16_t longBreak, uint32_t nowMs) {
        if (data.running || focus < 1 || focus > 90 || shortBreak < 1 || shortBreak > 30 ||
            longBreak < 1 || longBreak > 60) return false;
        data.focusMinutes = focus;
        data.shortBreakMinutes = shortBreak;
        data.longBreakMinutes = longBreak;
        data.remainingMs = phaseDurationMs();
        tickAtMs_ = nowMs;
        return true;
    }

    PomodoroEvent tick(uint32_t nowMs) {
        if (pending_ == PomodoroEvent::None) advance(nowMs);
        const PomodoroEvent result = pending_;
        pending_ = PomodoroEvent::None;
        return result;
    }

private:
    uint32_t tickAtMs_ = 0;
    uint16_t lastCompletedFocusMinutes_ = 0;
    PomodoroEvent pending_ = PomodoroEvent::None;

    static uint16_t clamp(uint16_t value, uint16_t low, uint16_t high) {
        return value < low ? low : value > high ? high : value;
    }

    void advance(uint32_t nowMs) {
        if (!data.running || pending_ != PomodoroEvent::None) return;
        const uint32_t elapsed = uint32_t(nowMs - tickAtMs_);
        tickAtMs_ = nowMs;
        if (elapsed < data.remainingMs) {
            data.remainingMs -= elapsed;
            return;
        }

        const uint8_t completed = data.phase;
        data.running = false;
        if (completed == 0) {
            lastCompletedFocusMinutes_ = data.focusMinutes;
            if (data.completedFocus != UINT32_MAX) ++data.completedFocus;
            const uint32_t room = UINT32_MAX - data.totalFocusMinutes;
            data.totalFocusMinutes += room < data.focusMinutes ? room : data.focusMinutes;
            if (++data.cycle >= 4) {
                data.cycle = 0;
                data.phase = 2;
            } else {
                data.phase = 1;
            }
            pending_ = PomodoroEvent::FocusComplete;
        } else {
            data.phase = 0;
            pending_ = completed == 1 ? PomodoroEvent::ShortBreakComplete : PomodoroEvent::LongBreakComplete;
        }
        data.remainingMs = phaseDurationMs();
        // Excess time belongs to no active phase: users explicitly start the
        // break/focus that follows, so delayed ticks cannot farm rewards.
    }
};
