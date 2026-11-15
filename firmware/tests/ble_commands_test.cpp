#include "../src/ble_commands.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using BleCommands::Command;
using BleCommands::Kind;

static void accept(const char* text, Kind kind, unsigned value = 0) {
    Command command;
    assert(BleCommands::parse(text, command));
    assert(command.kind == kind && command.value == value);
    // Every normal command fits a single default20-byte ATT write with newline.
    assert(std::strlen(text) + 1 <= 20);
}

static void reject(const char* text) {
    Command command;
    command.kind = Kind::Brightness;
    command.value = 77;
    assert(!BleCommands::parse(text, command));
    assert(command.kind == Kind::Brightness && command.value == 77);
}

static void acceptedCommands() {
    accept("status", Kind::Status);
    const char* screens[] = {"clock", "pet", "exchange", "mario", "pomodoro"};
    const char* characters[] = {"next", "chiikawa", "hachiware", "usagi", "trio"};
    const char* pets[] = {"feed", "play", "sleep"};
    const char* timers[] = {"start", "pause", "reset", "skip"};
    for (unsigned i = 0; i < 5; ++i) {
        accept((std::string("screen ") + screens[i]).c_str(), Kind::Screen, i);
        accept((std::string("char ") + characters[i]).c_str(), Kind::Character, i);
    }
    for (unsigned i = 0; i < 3; ++i)
        accept((std::string("pet ") + pets[i]).c_str(), Kind::Pet, i);
    for (unsigned i = 0; i < 4; ++i)
        accept((std::string("pomo ") + timers[i]).c_str(), Kind::Pomodoro, i);
    for (unsigned i = 3; i <= 100; ++i)
        accept((std::string("bright ") + std::to_string(i)).c_str(), Kind::Brightness, i);
    for (unsigned i = 1; i <= 8; ++i) {
        accept((std::string("room buy ") + std::to_string(i)).c_str(), Kind::RoomBuy, i);
        accept((std::string("room equip ") + std::to_string(i)).c_str(), Kind::RoomEquip, i);
    }
    for (unsigned i = 0; i <= 3; ++i)
        accept((std::string("room clear ") + std::to_string(i)).c_str(), Kind::RoomClear, i);
    accept("fx refresh", Kind::ExchangeRefresh);
    accept("mario jump", Kind::MarioJump);
}

static void whitespaceAndBoundaries() {
    Command command;
    assert(BleCommands::parse(" \troom\t buy\t8 \t\r \t", command));
    assert(command.kind == Kind::RoomBuy && command.value == 8);
    assert(BleCommands::parse("\t status\r", command) && command.kind == Kind::Status);
    std::string longest(BleCommands::MAX_COMMAND_BYTES - 6, ' ');
    longest += "status";
    assert(longest.size() == BleCommands::MAX_COMMAND_BYTES);
    assert(BleCommands::parse(longest.c_str(), command));
    longest.insert(0, 1, ' ');
    reject(longest.c_str());
    const char exact[] = {'s','t','a','t','u','s'};
    assert(BleCommands::parse(exact, sizeof exact, command));
    const char embeddedNul[] = {'s','t','a','t','u','s','\0','x'};
    assert(!BleCommands::parse(embeddedNul, sizeof embeddedNul, command));
    char unterminated[BleCommands::MAX_COMMAND_BYTES + 1];
    std::memset(unterminated, 'a', sizeof unterminated);
    assert(!BleCommands::parse(unterminated, command));
    assert(!BleCommands::parse(nullptr, command));
    assert(!BleCommands::parse(nullptr, 0, command));
}

static void invalidCommands() {
    const char* invalid[] = {
        "", " ", "\t\r ", "Status", "STATUS", "screen", "screen unknown", "screen Pet",
        "screen pet extra", "status now", "status status status status", "char", "char mario",
        "bright", "bright 0", "bright 2", "bright 101", "bright -1", "bright +3", "bright 3.0",
        "bright 3e1", "bright 0x10", "bright 03", "bright 00000000000000000000000000000003",
        "bright 4294967299", "bright 9999999999999999999999999999999999999999999999999999999",
        "pet", "pet reset", "pet feed;", "pet feed extra", "pomo", "pomo configure", "pomo stop",
        "room", "room buy", "room buy 0", "room buy 9", "room buy -1", "room buy +1", "room buy 1.0",
        "room buy 01", "room equip 0", "room equip 9", "room clear -1", "room clear 4", "room clear 00",
        "room clear 0 extra", "room sell 1", "fx", "fx reset", "mario", "mario walk", "mario jump extra",
        "status\n", "status\r\n", "status\r\r", "sta\rtus", "sta\ntus", "status\v", "status\f",
        "status\x7f", "char 치이카와", "status;room buy 1", "status&&room buy 1", "{\"screen\":\"pet\"}"
    };
    for (const char* text : invalid) reject(text);
    // Prefixes must never become a different accepted command while receiving
    // a fragmented write. The transport waits for newline before calling parse.
    const char* complete[] = {"screen pomodoro", "char hachiware", "room equip 8", "pomo pause"};
    for (const char* text : complete) {
        const std::string full(text);
        for (size_t length = 0; length < full.size(); ++length)
            reject(full.substr(0, length).c_str());
    }
}

int main() {
    acceptedCommands();
    whitespaceAndBoundaries();
    invalidCommands();
    std::cout << "BLE command parser: accepted commands, numeric bounds, malformed input, framing boundaries PASS\n";
}
