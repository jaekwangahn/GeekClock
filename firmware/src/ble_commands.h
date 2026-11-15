#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Small, allocation-free BLE remote protocol. Transport removes the newline;
// the parser accepts lowercase ASCII only and never changes output on failure.
namespace BleCommands {

static constexpr size_t MAX_COMMAND_BYTES = 95;

enum class Kind : uint8_t {
    Status, Screen, Character, Brightness, Pet, Pomodoro,
    RoomBuy, RoomEquip, RoomClear, ExchangeRefresh, MarioJump
};

struct Command {
    Kind kind = Kind::Status;
    uint8_t value = 0;
};

struct Token {
    const char* text;
    size_t length;
};

inline bool equals(const Token& token, const char* value) {
    return token.length == strlen(value) &&
           memcmp(token.text, value, token.length) == 0;
}

inline bool choice(const Token& token, const char* const* values, size_t count,
                   uint8_t& result) {
    for (size_t i = 0; i < count; ++i) {
        if (equals(token, values[i])) {
            result = static_cast<uint8_t>(i);
            return true;
        }
    }
    return false;
}

inline bool number(const Token& token, uint8_t minimum, uint8_t maximum,
                   uint8_t& result) {
    if (!token.length || (token.length > 1 && token.text[0] == '0')) return false;
    unsigned value = 0;
    for (size_t i = 0; i < token.length; ++i) {
        const char c = token.text[i];
        if (c < '0' || c > '9') return false;
        value = value * 10u + static_cast<unsigned>(c - '0');
        // Reject before another multiplication could overflow.
        if (value > maximum) return false;
    }
    if (value < minimum) return false;
    result = static_cast<uint8_t>(value);
    return true;
}

inline bool space(char c) { return c == ' ' || c == '\t'; }

inline bool parse(const char* text, size_t length, Command& output) {
    if (!text || !length || length > MAX_COMMAND_BYTES) return false;
    size_t begin = 0, end = length;
    while (begin < end && space(text[begin])) ++begin;
    while (end > begin && space(text[end - 1])) --end;
    if (end > begin && text[end - 1] == '\r') --end;
    while (end > begin && space(text[end - 1])) --end;
    if (begin == end) return false;

    Token tokens[3];
    size_t count = 0, offset = begin;
    while (offset < end) {
        while (offset < end && space(text[offset])) ++offset;
        if (offset == end) break;
        if (count == 3) return false;
        const size_t start = offset;
        while (offset < end && !space(text[offset])) {
            const unsigned char c = static_cast<unsigned char>(text[offset]);
            if (c < 0x21 || c > 0x7e) return false;
            ++offset;
        }
        tokens[count++] = {text + start, offset - start};
    }

    Command command;
    if (count == 1 && equals(tokens[0], "status")) {
        command.kind = Kind::Status;
    } else if (count == 2 && equals(tokens[0], "screen")) {
        // Values: clock=0, pet=1, exchange=2, mario=3, pomodoro=4.
        static const char* const values[] = {"clock", "pet", "exchange", "mario", "pomodoro"};
        command.kind = Kind::Screen;
        if (!choice(tokens[1], values, 5, command.value)) return false;
    } else if (count == 2 && equals(tokens[0], "char")) {
        // Values: next=0, chiikawa=1, hachiware=2, usagi=3, trio=4.
        static const char* const values[] = {"next", "chiikawa", "hachiware", "usagi", "trio"};
        command.kind = Kind::Character;
        if (!choice(tokens[1], values, 5, command.value)) return false;
    } else if (count == 2 && equals(tokens[0], "bright")) {
        command.kind = Kind::Brightness;
        if (!number(tokens[1], 3, 100, command.value)) return false;
    } else if (count == 2 && equals(tokens[0], "pet")) {
        // Values: feed=0, play=1, sleep=2.
        static const char* const values[] = {"feed", "play", "sleep"};
        command.kind = Kind::Pet;
        if (!choice(tokens[1], values, 3, command.value)) return false;
    } else if (count == 2 && equals(tokens[0], "pomo")) {
        // Values: start=0, pause=1, reset=2, skip=3.
        static const char* const values[] = {"start", "pause", "reset", "skip"};
        command.kind = Kind::Pomodoro;
        if (!choice(tokens[1], values, 4, command.value)) return false;
    } else if (count == 3 && equals(tokens[0], "room")) {
        if (equals(tokens[1], "buy")) command.kind = Kind::RoomBuy;
        else if (equals(tokens[1], "equip")) command.kind = Kind::RoomEquip;
        else if (equals(tokens[1], "clear")) command.kind = Kind::RoomClear;
        else return false;
        const bool clear = command.kind == Kind::RoomClear;
        if (!number(tokens[2], clear ? 0 : 1, clear ? 3 : 8, command.value)) return false;
    } else if (count == 2 && equals(tokens[0], "fx") && equals(tokens[1], "refresh")) {
        command.kind = Kind::ExchangeRefresh;
    } else if (count == 2 && equals(tokens[0], "mario") && equals(tokens[1], "jump")) {
        command.kind = Kind::MarioJump;
    } else {
        return false;
    }
    output = command;
    return true;
}

inline bool parse(const char* text, Command& output) {
    if (!text) return false;
    size_t length = 0;
    while (length <= MAX_COMMAND_BYTES && text[length]) ++length;
    return parse(text, length, output);
}

}  // namespace BleCommands
