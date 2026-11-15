#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// The owner supplies synchronization. BLE callbacks append fragments; only the
// Arduino loop removes complete lines. A rejected frame is discarded through LF
// so its tail can never be mistaken for another control command.
class BleLineQueue {
public:
    static constexpr size_t MAX_LINE = 95;
    static constexpr size_t CAPACITY = 8;

    void reset() {
        head_ = count_ = length_ = 0;
        dropping_ = false;
        lastByteMs_ = 0;
    }

    void append(const uint8_t* bytes, size_t length, uint32_t nowMs) {
        expire(nowMs);
        for (size_t i = 0; i < length; ++i) {
            const uint8_t byte = bytes[i];
            lastByteMs_ = nowMs;
            if (byte == '\n') {
                if (!dropping_ && length_) {
                    if (count_ < CAPACITY) {
                        const size_t tail = (head_ + count_) % CAPACITY;
                        memcpy(lines_[tail], partial_, length_);
                        lines_[tail][length_] = '\0';
                        ++count_;
                    } else {
                        ++rejected_;
                    }
                }
                length_ = 0;
                dropping_ = false;
                continue;
            }
            if (dropping_) continue;
            if ((byte < 0x20 && byte != '\r' && byte != '\t') || byte > 0x7e || length_ == MAX_LINE) {
                ++rejected_;
                length_ = 0;
                dropping_ = true;
                continue;
            }
            // Retain CR for the command parser, which accepts only one trailing
            // CR before LF; an embedded CR must not turn into a valid command.
            partial_[length_++] = char(byte);
        }
    }

    void expire(uint32_t nowMs) {
        if (length_ && uint32_t(nowMs - lastByteMs_) >= 5000) {
            ++rejected_;
            length_ = 0;
            dropping_ = true;
        }
    }

    bool pop(char* destination, size_t capacity) {
        if (!count_ || !destination) return false;
        const size_t length = strlen(lines_[head_]);
        if (capacity <= length) return false;
        memcpy(destination, lines_[head_], length + 1);
        head_ = (head_ + 1) % CAPACITY;
        --count_;
        return true;
    }

    size_t count() const { return count_; }
    uint32_t rejected() const { return rejected_; }

private:
    char lines_[CAPACITY][MAX_LINE + 1] = {};
    char partial_[MAX_LINE] = {};
    size_t head_ = 0, count_ = 0, length_ = 0;
    uint32_t lastByteMs_ = 0, rejected_ = 0;
    bool dropping_ = false;
};
