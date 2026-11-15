#include "../src/pet_game.h"
#include "../src/touch_button.h"
#include "../src/touch_gestures.h"

#include <assert.h>
#include <iostream>
#include <vector>

struct RawTouchHarness {
    TouchButton button;
    TouchGestures gestures;
    uint32_t nowMs = 0;
    std::vector<Gesture> events;

    RawTouchHarness() {
        sampleFor(480, 1000); // 24 idle samples, matching the firmware cadence
        assert(button.ready());
        assert(events.empty());
    }

    void sampleFor(uint32_t durationMs, uint16_t raw) {
        assert(durationMs % 20 == 0);
        for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += 20) {
            button.update(raw, nowMs);
            const Gesture event = gestures.update(button.pressed(), nowMs);
            if (event != Gesture::None) events.push_back(event);
            nowMs += 20;
        }
    }
};

static void rawTouchIntegration() {
    RawTouchHarness single;
    single.sampleFor(100, 600);
    single.sampleFor(500, 1000);
    assert(single.events.size() == 1 && single.events[0] == Gesture::Single);
    assert(single.button.pressCount() == 1 && !single.button.pressed());

    RawTouchHarness twice;
    twice.sampleFor(100, 600);
    twice.sampleFor(120, 1000);
    twice.sampleFor(100, 600);
    twice.sampleFor(500, 1000);
    assert(twice.events.size() == 1 && twice.events[0] == Gesture::Double);
    assert(twice.button.pressCount() == 2 && !twice.button.pressed());

    RawTouchHarness held;
    held.sampleFor(1000, 600);
    held.sampleFor(500, 1000);
    assert(held.events.size() == 1 && held.events[0] == Gesture::Long);
    assert(held.button.pressCount() == 1 && !held.button.pressed());

    RawTouchHarness noisy;
    for (int i = 0; i < 10; ++i) {
        noisy.sampleFor(20, 600); // below 60ms debounce: never a physical press
        noisy.sampleFor(80, 1000);
    }
    noisy.sampleFor(500, 1000);
    assert(noisy.events.empty() && noisy.button.pressCount() == 0);
    assert(!noisy.button.pressed());
}

static void gestures() {
    TouchGestures taps;
    assert(taps.update(true, 100) == Gesture::None);
    assert(taps.update(false, 180) == Gesture::None);
    assert(taps.update(false, 530) == Gesture::None);
    assert(taps.update(false, 531) == Gesture::Single);
    assert(taps.update(false, 1000) == Gesture::None);
    assert(taps.update(true, 1100) == Gesture::None);
    assert(taps.update(false, 1180) == Gesture::None);
    assert(taps.update(true, 1400) == Gesture::None);
    assert(taps.update(false, 1500) == Gesture::Double);
    assert(taps.update(false, 2000) == Gesture::None);

    assert(taps.update(true, 2100) == Gesture::None);
    assert(taps.update(true, 2999) == Gesture::None);
    assert(taps.update(true, 3000) == Gesture::Long);
    assert(taps.update(true, 5000) == Gesture::None);
    assert(taps.update(false, 5100) == Gesture::None);
    assert(taps.update(false, 5600) == Gesture::None);

    // A hold on the second touch replaces the pending tap; no single/double.
    assert(taps.update(true, 6000) == Gesture::None);
    assert(taps.update(false, 6100) == Gesture::None);
    assert(taps.update(true, 6200) == Gesture::None);
    assert(taps.update(true, 7100) == Gesture::Long);
    assert(taps.update(false, 7200) == Gesture::None);
    assert(taps.update(false, 8000) == Gesture::None);

    TouchGestures wrap;
    assert(wrap.update(true, UINT32_MAX - 100) == Gesture::None);
    assert(wrap.update(false, UINT32_MAX - 20) == Gesture::None);
    assert(wrap.update(true, 100) == Gesture::None);
    assert(wrap.update(false, 180) == Gesture::Double);
    assert(wrap.update(false, 900) == Gesture::None);
    TouchGestures wrapLong;
    assert(wrapLong.update(true, UINT32_MAX - 100) == Gesture::None);
    assert(wrapLong.update(true, 800) == Gesture::Long);

    // A new touch after the old window emits the old single, not a double.
    TouchGestures late;
    late.update(true, 100);
    late.update(false, 200);
    assert(late.update(true, 600) == Gesture::Single);
    assert(late.update(false, 700) == Gesture::None);
    assert(late.update(false, 1051) == Gesture::Single);

    TouchGestures stalled;
    stalled.update(true, 0);
    assert(stalled.update(false, 1000) == Gesture::Long);
    assert(stalled.update(false, 1500) == Gesture::None);
}

static void careAndLevels() {
    PetGame p;
    p.tick(0);
    assert(p.feed(0));
    assert(p.data.fullness == 100 && p.data.happiness == 84 && p.data.xp == 8);
    assert(p.activity() == PetActivity::Eating);
    assert(!p.play(999));
    assert(p.play(1000));
    assert(p.data.energy == 72 && p.data.fullness == 96 && p.data.happiness == 100);
    assert(p.data.careCount == 2 && p.data.xp == 20);
    p.tick(3400);
    assert(p.activity() == PetActivity::Wander);
    p.data.xp = 98;
    assert(p.feed(3500));
    assert(p.level() == 2);
    p.data.xp = 9898;
    assert(p.feed(4500));
    assert(p.data.xp == 9899 && p.level() == 99);
    p.data.fullness = p.data.happiness = p.data.energy = 255;
    p.data.xp = 65535;
    p.x = -100;
    p.y = 300;
    p.sanitize();
    assert(p.data.fullness == 100 && p.data.happiness == 100 && p.data.energy == 100);
    assert(p.level() == 99 && p.x == PetGame::MIN_X && p.y == PetGame::MAX_Y);
    p.data.energy = 7;
    assert(!p.play(5500));
    assert(p.lowNeeds());
}

static void movementAndNeeds() {
    PetGame p;
    p.active = true;
    p.tick(1000);
    const int x0 = p.x, y0 = p.y;
    assert(!p.tick(1099));
    assert(p.tick(1100));
    assert(p.x != x0 || p.y != y0);
    int minX = p.x, maxX = p.x, minY = p.y, maxY = p.y;
    for (uint32_t t = 1200; t < 1200000; t += 100) {
        p.tick(t);
        assert(p.x >= PetGame::MIN_X && p.x <= PetGame::MAX_X);
        assert(p.y >= PetGame::MIN_Y && p.y <= PetGame::MAX_Y);
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
        if (p.y < minY) minY = p.y;
        if (p.y > maxY) maxY = p.y;
    }
    assert(maxX - minX > 70 && maxY - minY > 20);
    assert(p.data.fullness == 61 && p.data.happiness == 71 && p.data.energy == 74);
    const int stillX = p.x, stillY = p.y;
    p.active = false;
    p.tick(1200100);
    assert(p.x == stillX && p.y == stillY);

    // A delayed tick is bounded and clamps need depletion instead of looping.
    p.tick(1000000000);
    assert(p.data.fullness == 0 && p.data.happiness == 0 && p.data.energy == 0);
    assert(p.data.xp == 0); // no passive XP farming
}

static void sleepAndWrap() {
    PetGame p;
    p.active = true;
    p.tick(100);
    p.data.energy = 10;
    assert(p.sleep(200));
    const int x0 = p.x, y0 = p.y;
    p.tick(2200);
    assert(p.data.energy == 12 && p.x == x0 && p.y == y0);
    assert(!p.sleep(2300));
    p.tick(30200);
    assert(p.data.energy == 40 && p.activity() == PetActivity::Wander);

    // Sleeping after a long event-loop stall cannot gain beyond a 30s nap.
    PetGame stalled;
    stalled.tick(0);
    stalled.data.energy = 0;
    assert(stalled.sleep(100));
    stalled.tick(100000);
    assert(stalled.data.energy == 30);
    assert(stalled.activity() == PetActivity::Wander);

    PetGame waking;
    waking.tick(0);
    assert(waking.sleep(0));
    assert(waking.feed(1000));
    assert(waking.activity() == PetActivity::Eating);

    PetGame wrap;
    const uint32_t start = UINT32_MAX - 500;
    wrap.tick(start);
    wrap.active = true;
    assert(wrap.feed(start));
    assert(!wrap.feed(400));
    assert(wrap.feed(500));
    wrap.tick(60000);
    assert(wrap.data.fullness == 99);
    assert(wrap.activity() == PetActivity::Wander);
    assert(wrap.x >= PetGame::MIN_X && wrap.x <= PetGame::MAX_X);
}

static void automaticRest() {
    PetGame p;
    p.active = true;
    p.data.energy = 15;
    p.tick(0);
    p.tick(100);
    assert(p.activity() == PetActivity::Wander); // strictly below 15
    p.data.energy = 14;
    assert(p.tick(200));
    assert(p.activity() == PetActivity::Sleeping);
    const int x0 = p.x, y0 = p.y;
    p.tick(2200);
    assert(p.data.energy == 16 && p.x == x0 && p.y == y0);
    p.tick(30200);
    assert(p.data.energy == 44 && p.activity() == PetActivity::Wander);
    assert(p.data.xp == 0 && p.data.careCount == 0);

    PetGame inactive;
    inactive.data.energy = 5;
    inactive.tick(0);
    inactive.tick(100);
    assert(inactive.activity() == PetActivity::Wander);
    inactive.active = true;
    inactive.tick(200);
    assert(inactive.activity() == PetActivity::Sleeping);
    assert(inactive.feed(200)); // an automatic nap does not consume care cooldown
    assert(inactive.activity() == PetActivity::Eating);
}

int main() {
    rawTouchIntegration();
    gestures();
    careAndLevels();
    movementAndNeeds();
    sleepAndWrap();
    automaticRest();
    std::cout << "PASS: raw-touch integration, pet care, need decay, movement, sleep, level caps, gestures, uint32 rollover\n";
}
