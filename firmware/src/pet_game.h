#pragma once

#include <stdint.h>

struct PetSnapshot {
    uint8_t fullness = 80;
    uint8_t happiness = 80;
    uint8_t energy = 80;
    uint16_t xp = 0;
    uint32_t careCount = 0;
};

enum class PetActivity : uint8_t { Wander, Happy, Eating, Sleeping };

// Pure game state: no Arduino, drawing, networking, or filesystem dependencies.
// The firmware persists only data; session timers start fresh after boot.
class PetGame {
public:
    PetSnapshot data;
    bool active = false;
    int16_t x = 60;
    int16_t y = 90;

    static constexpr int16_t MIN_X = 8;
    static constexpr int16_t MAX_X = 145;
    static constexpr int16_t MIN_Y = 74;
    static constexpr int16_t MAX_Y = 118;

    PetActivity activity() const { return activity_; }
    uint8_t level() const { return uint8_t(1 + data.xp / 100); }
    bool lowNeeds() const { return data.fullness < 25 || data.happiness < 25 || data.energy < 25; }
    bool facingLeft() const { return dx_ < 0; }
    const char* activityName() const { return activityName(activity_); }

    static const char* activityName(PetActivity activity) {
        switch (activity) {
            case PetActivity::Happy: return "happy";
            case PetActivity::Eating: return "eating";
            case PetActivity::Sleeping: return "sleeping";
            default: return "wander";
        }
    }

    void sanitize() {
        data.fullness = clampStat(data.fullness);
        data.happiness = clampStat(data.happiness);
        data.energy = clampStat(data.energy);
        if (data.xp > MAX_XP) data.xp = MAX_XP;
        if (x < MIN_X) x = MIN_X;
        if (x > MAX_X) x = MAX_X;
        if (y < MIN_Y) y = MIN_Y;
        if (y > MAX_Y) y = MAX_Y;
    }

    bool feed(uint32_t nowMs) {
        if (!canCare(nowMs)) return false;
        data.fullness = addStat(data.fullness, 22);
        data.happiness = addStat(data.happiness, 4);
        care(nowMs, 8, PetActivity::Eating);
        return true;
    }

    bool play(uint32_t nowMs) {
        if (!canCare(nowMs) || data.energy < 8 || data.fullness < 4) return false;
        data.fullness = subtractStat(data.fullness, 4);
        data.energy = subtractStat(data.energy, 8);
        data.happiness = addStat(data.happiness, 16);
        care(nowMs, 12, PetActivity::Happy);
        return true;
    }

    bool sleep(uint32_t nowMs) {
        if (!canCare(nowMs) || activity_ == PetActivity::Sleeping) return false;
        care(nowMs, 4, PetActivity::Sleeping);
        sleepRegenAtMs_ = nowMs;
        return true;
    }

    // Returns whether state or the visible scene changed. Catch-up work is
    // bounded: need decay uses arithmetic; movement executes at most 10 steps.
    bool tick(uint32_t nowMs) {
        if (!started_) {
            started_ = true;
            motionAtMs_ = fullnessAtMs_ = happinessAtMs_ = energyAtMs_ = nowMs;
            return false;
        }

        bool changed = decay(data.fullness, fullnessAtMs_, nowMs, 60000);
        changed = decay(data.happiness, happinessAtMs_, nowMs, 120000) || changed;
        if (activity_ == PetActivity::Sleeping) {
            const uint32_t sleepingMs = uint32_t(nowMs - activityAtMs_);
            // A delayed tick restores only the portion of the 30-second nap
            // that actually belongs to sleep, not all of the elapsed time.
            const uint32_t sleepUntilMs = activityAtMs_ + (sleepingMs < 30000 ? sleepingMs : 30000);
            const uint32_t steps = uint32_t(sleepUntilMs - sleepRegenAtMs_) / 2000;
            if (steps) {
                const uint8_t before = data.energy;
                data.energy = addStat(data.energy, steps >= 50 ? 100 : steps * 2);
                sleepRegenAtMs_ += steps * 2000;
                changed = changed || data.energy != before;
            }
            energyAtMs_ = nowMs;
        } else {
            changed = decay(data.energy, energyAtMs_, nowMs, 180000) || changed;
        }

        const uint32_t activityDuration = activity_ == PetActivity::Sleeping ? 30000 : 2400;
        if (activity_ != PetActivity::Wander && uint32_t(nowMs - activityAtMs_) >= activityDuration) {
            activity_ = PetActivity::Wander;
            changed = true;
        }
        // Physical-button players can recover energy without a separate sleep
        // gesture. Automatic naps are rest, never a rewarded care interaction.
        if (active && activity_ == PetActivity::Wander && data.energy < 15) {
            activity_ = PetActivity::Sleeping;
            activityAtMs_ = sleepRegenAtMs_ = energyAtMs_ = nowMs;
            changed = true;
        }

        const uint32_t elapsedSteps = uint32_t(nowMs - motionAtMs_) / 100;
        if (elapsedSteps) {
            motionAtMs_ = nowMs;
            if (active) {
                // Still animate sleeping/eating expressions, without walking.
                changed = true;
                if (activity_ == PetActivity::Wander || activity_ == PetActivity::Happy) {
                    const uint32_t steps = elapsedSteps > 10 ? 10 : elapsedSteps;
                    for (uint32_t i = 0; i < steps; ++i) moveStep();
                }
            }
        }
        return changed;
    }

private:
    static constexpr uint16_t MAX_XP = 9899;
    PetActivity activity_ = PetActivity::Wander;
    uint32_t activityAtMs_ = 0;
    uint32_t sleepRegenAtMs_ = 0;
    uint32_t lastCareMs_ = 0;
    uint32_t motionAtMs_ = 0;
    uint32_t fullnessAtMs_ = 0;
    uint32_t happinessAtMs_ = 0;
    uint32_t energyAtMs_ = 0;
    uint32_t random_ = 0x63a17e4b;
    uint8_t stepsToTurn_ = 25;
    int8_t dx_ = 1;
    int8_t dy_ = 1;
    bool started_ = false;
    bool cared_ = false;

    static uint8_t clampStat(uint32_t value) { return value > 100 ? 100 : uint8_t(value); }
    static uint8_t addStat(uint8_t value, uint32_t amount) { return clampStat(uint32_t(value) + amount); }
    static uint8_t subtractStat(uint8_t value, uint32_t amount) { return amount >= value ? 0 : uint8_t(value - amount); }

    static bool decay(uint8_t& value, uint32_t& atMs, uint32_t nowMs, uint32_t interval) {
        const uint32_t steps = uint32_t(nowMs - atMs) / interval;
        if (!steps) return false;
        atMs += steps * interval;
        const uint8_t before = value;
        value = subtractStat(value, steps);
        return before != value;
    }

    bool canCare(uint32_t nowMs) const { return !cared_ || uint32_t(nowMs - lastCareMs_) >= 1000; }

    void care(uint32_t nowMs, uint8_t xp, PetActivity activity) {
        cared_ = true;
        lastCareMs_ = activityAtMs_ = nowMs;
        activity_ = activity;
        const uint32_t earned = uint32_t(data.xp) + xp;
        data.xp = earned > MAX_XP ? MAX_XP : uint16_t(earned);
        if (data.careCount != UINT32_MAX) ++data.careCount;
    }

    uint32_t nextRandom() {
        random_ ^= random_ << 13;
        random_ ^= random_ >> 17;
        random_ ^= random_ << 5;
        return random_;
    }

    void moveStep() {
        if (--stepsToTurn_ == 0) {
            const uint32_t r = nextRandom();
            dx_ = (r & 1) ? 1 : -1;
            dy_ = (r & 2) ? 1 : -1;
            stepsToTurn_ = uint8_t(20 + r % 35);
        }
        x += dx_;
        y += dy_;
        if (x < MIN_X) { x = MIN_X; dx_ = 1; }
        if (x > MAX_X) { x = MAX_X; dx_ = -1; }
        if (y < MIN_Y) { y = MIN_Y; dy_ = 1; }
        if (y > MAX_Y) { y = MAX_Y; dy_ = -1; }
    }
};
