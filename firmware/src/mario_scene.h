#pragma once

#include <stdint.h>
#include <stddef.h>

// Time is supplied by the device clock; only the scenery uses elapsed uptime.
namespace mario_scene {
inline uint8_t glyphWidth(char value) { return value == '1' || value == ':' ? 1 : 3; }
inline uint8_t glyphRow(char value, unsigned row) {
    static const uint8_t digits[10][5] = {
        {7,5,5,5,7}, {1,1,1,1,1}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
        {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7}
    };
    if (row >= 5) return 0;
    if (value >= '0' && value <= '9') return digits[value-'0'][row];
    if (value == ':') return row == 1 || row == 4 ? 1 : 0;
    return value == '-' && row == 2 ? 7 : 0;
}
inline void formatTime(int hour, int minute, bool valid, bool use12h, char (&out)[6]) {
    if (!valid || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        out[0]='-';out[1]='-';out[2]=':';out[3]='-';out[4]='-';out[5]=0;return;
    }
    if (use12h) { hour %= 12; if (hour == 0) hour = 12; }
    unsigned at=0;
    if (hour >= 10) out[at++]=char('0'+hour/10);
    out[at++]=char('0'+hour%10);
    out[at++]=':';out[at++]=char('0'+minute/10);out[at++]=char('0'+minute%10);out[at]=0;
}
inline unsigned timeUnits(const char* text) {
    unsigned result=0;
    for (const char* p=text;*p;++p) result += glyphWidth(*p)+(p==text?0:1);
    return result;
}
inline int jumpHeight(uint32_t elapsed, uint32_t duration=900, int peak=64) {
    if (duration == 0 || elapsed >= duration) return 0;
    return int((uint64_t(4)*unsigned(peak)*elapsed*(duration-elapsed))/(uint64_t(duration)*duration));
}
inline int walkX(uint32_t nowMs) {
    const unsigned phase=(nowMs/85)%352;
    return phase<=176?int(phase):int(352-phase);
}
inline bool facingLeft(uint32_t nowMs) { return ((nowMs/85)%352)>176; }
inline bool isNight(int hour) { return hour < 5 || hour >= 21; }
inline bool isDusk(int hour) { return (hour >= 18 && hour < 21) || hour == 5; }
} // namespace mario_scene
