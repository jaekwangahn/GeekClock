#include "../src/pomodoro.h"
#include "../src/room_decor.h"

#include <cassert>
#include <cstring>
#include <iostream>

static void focusAndRewards() {
    PomodoroTimer timer;
    RoomDecor room;
    assert(timer.data.remainingMs == 1500000 && !timer.data.running);
    assert(timer.start(100));
    assert(!timer.start(101));
    assert(!timer.configure(1, 1, 1, 200));
    assert(timer.tick(1500099) == PomodoroEvent::None);
    assert(timer.data.remainingMs == 1);
    assert(timer.tick(1500100) == PomodoroEvent::FocusComplete);
    assert(!timer.data.running && timer.data.phase == 1);
    assert(timer.data.remainingMs == 300000 && timer.data.cycle == 1);
    assert(timer.data.completedFocus == 1 && timer.data.totalFocusMinutes == 25);
    room.credit(focusRewardCoins(timer.lastCompletedFocusMinutes()));
    assert(room.data.coins == 30);
    assert(timer.tick(UINT32_MAX) == PomodoroEvent::None);
    assert(timer.data.completedFocus == 1 && room.data.coins == 30);

    assert(timer.start(1500100));
    assert(timer.tick(1800100) == PomodoroEvent::ShortBreakComplete);
    assert(timer.data.phase == 0 && !timer.data.running);
    assert(timer.data.remainingMs == 1500000 && timer.data.completedFocus == 1);
    assert(timer.tick(3800100) == PomodoroEvent::None);
    assert(focusRewardCoins(25) == 10 && focusRewardCoins(1) == 1);
    assert(focusRewardCoins(0) == 1 && focusRewardCoins(90) == 36);
    assert(focusRewardCoins(UINT16_MAX) == 36);
}

static void pauseResetSkipAndConfigure() {
    PomodoroTimer timer;
    assert(timer.configure(1, 1, 2, 0));
    assert(timer.start(10));
    assert(timer.tick(1010) == PomodoroEvent::None);
    assert(timer.pause(2010));
    assert(timer.data.remainingMs == 58000);
    assert(!timer.pause(3010));
    assert(timer.tick(100000) == PomodoroEvent::None);
    assert(timer.data.remainingMs == 58000);
    assert(timer.start(101000));
    assert(timer.tick(158999) == PomodoroEvent::None);
    assert(timer.tick(159000) == PomodoroEvent::FocusComplete);
    assert(timer.data.completedFocus == 1 && timer.data.totalFocusMinutes == 1);

    assert(timer.reset(200000));
    assert(timer.data.phase == 0 && timer.data.cycle == 0 && timer.data.remainingMs == 60000);
    assert(timer.data.completedFocus == 1 && timer.data.totalFocusMinutes == 1);
    assert(timer.start(200000));
    assert(timer.skip(210000));
    assert(!timer.data.running && timer.data.phase == 1 && timer.data.cycle == 0);
    assert(timer.tick(999999) == PomodoroEvent::None);
    assert(timer.data.completedFocus == 1 && timer.data.totalFocusMinutes == 1);
    assert(timer.skip(1000000));
    assert(timer.data.phase == 0 && timer.data.remainingMs == 60000);

    const PomodoroData before = timer.data;
    assert(!timer.configure(0, 5, 15, 0));
    assert(!timer.configure(91, 5, 15, 0));
    assert(!timer.configure(25, 0, 15, 0));
    assert(!timer.configure(25, 31, 15, 0));
    assert(!timer.configure(25, 5, 0, 0));
    assert(!timer.configure(25, 5, 61, 0));
    assert(timer.data.focusMinutes == before.focusMinutes && timer.data.remainingMs == before.remainingMs);
    assert(timer.configure(90, 30, 60, 0));
    assert(timer.data.remainingMs == 5400000);
}

static void cyclesAndLargeElapsed() {
    PomodoroTimer timer;
    timer.configure(1, 1, 1, 0);
    uint32_t now = 1000;
    for (unsigned session = 1; session <= 4; ++session) {
        assert(timer.data.phase == 0);
        assert(timer.start(now));
        now += 60000;
        assert(timer.tick(now) == PomodoroEvent::FocusComplete);
        assert(timer.data.phase == (session == 4 ? 2 : 1));
        assert(timer.data.cycle == session % 4);
        assert(timer.start(now));
        now += 60000;
        assert(timer.tick(now) == (session == 4 ? PomodoroEvent::LongBreakComplete : PomodoroEvent::ShortBreakComplete));
    }
    assert(timer.data.completedFocus == 4 && timer.data.totalFocusMinutes == 4);
    assert(timer.data.phase == 0 && timer.data.cycle == 0);

    assert(timer.start(now));
    assert(timer.tick(now + 3600000) == PomodoroEvent::FocusComplete);
    assert(timer.data.completedFocus == 5 && timer.data.remainingMs == 60000 && !timer.data.running);
    assert(timer.tick(now + 7200000) == PomodoroEvent::None);
    // Skipping a focus cannot advance the long-break cycle or lifetime totals.
    timer.skip(now + 7200001);
    const uint8_t cycle = timer.data.cycle;
    for (unsigned n = 0; n < 50; ++n) timer.skip(now + 7200002 + n);
    assert(timer.data.cycle == cycle && timer.data.completedFocus == 5);
}

static void restoreWrapAndBoundaryActions() {
    PomodoroTimer timer;
    timer.configure(1, 1, 1, 0);
    assert(timer.start(UINT32_MAX - 999));
    assert(timer.tick(0) == PomodoroEvent::None);
    assert(timer.data.remainingMs == 59000);
    assert(timer.tick(59000) == PomodoroEvent::FocusComplete);
    assert(timer.tick(59001) == PomodoroEvent::None);

    timer.reset(60000);
    timer.start(60000);
    timer.tick(65000);
    PomodoroTimer rebooted;
    rebooted.data = timer.data;
    rebooted.restore();
    assert(!rebooted.data.running && rebooted.data.remainingMs == 55000);
    assert(rebooted.tick(900000) == PomodoroEvent::None);
    assert(rebooted.start(900000));
    assert(rebooted.tick(955000) == PomodoroEvent::FocusComplete);

    // A pause that crosses the exact deadline queues, rather than loses, the
    // completion. The caller can settle it before its atomic persistence write.
    assert(timer.tick(1000000) == PomodoroEvent::FocusComplete);
    timer.reset(1000000);
    timer.start(1000000);
    assert(timer.pause(1060000));
    assert(!timer.start(1060000));
    assert(timer.tick(1060000) == PomodoroEvent::FocusComplete);
    assert(timer.lastCompletedFocusMinutes() == 1);
    assert(timer.tick(1060001) == PomodoroEvent::None);

    timer.reset(1100000);
    timer.start(1100000);
    assert(!timer.skip(1160000));
    assert(timer.data.phase == 1);
    assert(timer.tick(1160000) == PomodoroEvent::FocusComplete);
    timer.reset(1200000);
    timer.start(1200000);
    timer.reset(1260000);
    assert(timer.data.phase == 0 && timer.data.cycle == 0);
    assert(timer.tick(1260000) == PomodoroEvent::FocusComplete);

    PomodoroTimer invalid;
    invalid.data.focusMinutes = 0;
    invalid.data.shortBreakMinutes = UINT16_MAX;
    invalid.data.longBreakMinutes = 0;
    invalid.data.phase = 255;
    invalid.data.cycle = 255;
    invalid.data.remainingMs = UINT32_MAX;
    invalid.data.running = true;
    invalid.restore();
    assert(invalid.data.focusMinutes == 1 && invalid.data.shortBreakMinutes == 30 && invalid.data.longBreakMinutes == 1);
    assert(invalid.data.phase == 0 && invalid.data.cycle == 3 && invalid.data.remainingMs == 60000);
    assert(!invalid.data.running);

    invalid.data.completedFocus = UINT32_MAX;
    invalid.data.totalFocusMinutes = UINT32_MAX - 1;
    invalid.start(0);
    invalid.tick(60000);
    assert(invalid.data.completedFocus == UINT32_MAX && invalid.data.totalFocusMinutes == UINT32_MAX);
}

static void shopAndSanitize() {
    RoomDecor room;
    assert(room.data.coins == 20 && room.data.ownedMask == 0);
    assert(room.buy(0) == RoomResult::InvalidItem && room.buy(9) == RoomResult::InvalidItem);
    assert(room.equip(7) == RoomResult::NotOwned);
    assert(room.buy(6) == RoomResult::InsufficientCoins);
    assert(room.data.coins == 20 && room.data.ownedMask == 0);
    assert(room.buy(7) == RoomResult::Ok);
    assert(room.data.coins == 5 && room.data.decor == 7 && room.owns(7));
    assert(room.buy(7) == RoomResult::AlreadyOwned && room.data.coins == 5);
    assert(room.unequip(3) == RoomResult::Ok && room.data.decor == 0);
    assert(room.equip(7) == RoomResult::Ok && room.data.decor == 7);
    assert(room.unequip(4) == RoomResult::InvalidItem);
    assert(room.equip(0) == RoomResult::InvalidItem && !room.owns(0) && !room.owns(255));

    room.credit(1000);
    for (uint8_t id = 1; id <= 8; ++id) {
        const RoomItem* item = roomItem(id);
        assert(item && item->id == id && item->slot == (id - 1) / 2);
        const uint32_t before = room.data.coins;
        assert(room.buy(id) == (id == 7 ? RoomResult::AlreadyOwned : RoomResult::Ok));
        assert(room.data.coins == before - (id == 7 ? 0 : item->price));
    }
    assert(room.data.ownedMask == 0xff);
    assert(room.data.wallpaper == 2 && room.data.rug == 4 && room.data.bed == 6 && room.data.decor == 8);
    for (uint8_t id = 1; id <= 8; ++id) assert(room.equip(id) == RoomResult::Ok);
    room.credit(UINT32_MAX);
    assert(room.data.coins == RoomDecor::MAX_COINS);
    room.credit(1);
    assert(room.data.coins == RoomDecor::MAX_COINS);

    RoomDecor restored;
    restored.data = room.data;
    restored.sanitize();
    assert(restored.data.ownedMask == room.data.ownedMask && restored.data.coins == room.data.coins);
    restored.data.coins = UINT32_MAX;
    restored.data.ownedMask = 0xff00 | 1;
    restored.data.wallpaper = 1;
    restored.data.rug = 2;
    restored.data.bed = 5;
    restored.data.decor = 255;
    restored.sanitize();
    assert(restored.data.coins == 99999 && restored.data.ownedMask == 1);
    assert(restored.data.wallpaper == 1 && restored.data.rug == 0 && restored.data.bed == 0 && restored.data.decor == 0);
}

int main() {
    focusAndRewards();
    pauseResetSkipAndConfigure();
    cyclesAndLargeElapsed();
    restoreWrapAndBoundaryActions();
    shopAndSanitize();
    std::cout << "PASS: countdown/reward/cycle/pause/skip/restore/wrap boundaries and room wallet/inventory/equip.\n";
}
