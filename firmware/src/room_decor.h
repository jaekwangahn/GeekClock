#pragma once

#include <stdint.h>

struct RoomData {
    uint32_t coins = 20;
    uint16_t ownedMask = 0;
    uint8_t wallpaper = 0;
    uint8_t rug = 0;
    uint8_t bed = 0;
    uint8_t decor = 0;
};

struct RoomItem {
    uint8_t id;
    uint8_t slot;  // wallpaper, rug, bed, decor
    uint16_t price;
    const char* key;
    const char* name;
};

constexpr RoomItem ROOM_ITEMS[8] = {
    {1, 0, 20, "soda_wallpaper", "소다 벽지"},
    {2, 0, 35, "night_wallpaper", "별밤 벽지"},
    {3, 1, 20, "berry_rug", "딸기 러그"},
    {4, 1, 30, "star_rug", "별 러그"},
    {5, 2, 40, "cloud_bed", "구름 침대"},
    {6, 2, 60, "moon_bed", "달빛 침대"},
    {7, 3, 15, "plant", "초록 화분"},
    {8, 3, 25, "star_lamp", "별빛 스탠드"}
};

inline const RoomItem* roomItem(uint8_t id) {
    return id >= 1 && id <= 8 ? &ROOM_ITEMS[id - 1] : nullptr;
}

enum class RoomResult : uint8_t { Ok, AlreadyOwned, InsufficientCoins, InvalidItem, NotOwned };

class RoomDecor {
public:
    RoomData data;
    static constexpr uint32_t MAX_COINS = 99999;

    bool owns(uint8_t id) const {
        return roomItem(id) && (data.ownedMask & (uint16_t(1) << (id - 1))) != 0;
    }

    void sanitize() {
        if (data.coins > MAX_COINS) data.coins = MAX_COINS;
        data.ownedMask &= 0x00ff;
        for (uint8_t slot = 0; slot < 4; ++slot) {
            uint8_t& id = equipped(slot);
            const RoomItem* item = roomItem(id);
            if (id && (!item || item->slot != slot || !owns(id))) id = 0;
        }
    }

    void credit(uint32_t amount) {
        if (data.coins > MAX_COINS) data.coins = MAX_COINS;
        const uint32_t remaining = MAX_COINS - data.coins;
        data.coins += amount > remaining ? remaining : amount;
    }

    // Purchase and initial equip are one mutation for the caller to persist.
    RoomResult buy(uint8_t id) {
        const RoomItem* item = roomItem(id);
        if (!item) return RoomResult::InvalidItem;
        if (owns(id)) return RoomResult::AlreadyOwned;
        if (data.coins < item->price) return RoomResult::InsufficientCoins;
        data.coins -= item->price;
        data.ownedMask |= uint16_t(1) << (id - 1);
        equipped(item->slot) = id;
        return RoomResult::Ok;
    }

    RoomResult equip(uint8_t id) {
        const RoomItem* item = roomItem(id);
        if (!item) return RoomResult::InvalidItem;
        if (!owns(id)) return RoomResult::NotOwned;
        equipped(item->slot) = id;
        return RoomResult::Ok;
    }

    RoomResult unequip(uint8_t slot) {
        if (slot >= 4) return RoomResult::InvalidItem;
        equipped(slot) = 0;
        return RoomResult::Ok;
    }

private:
    uint8_t& equipped(uint8_t slot) {
        return slot == 0 ? data.wallpaper : slot == 1 ? data.rug : slot == 2 ? data.bed : data.decor;
    }
};
