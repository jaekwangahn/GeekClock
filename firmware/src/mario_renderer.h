#pragma once

#include "mario_scene.h"

// Render the uploaded NES title artwork directly into the 20-row display strips.
// Each background row is a sequence of (run length, palette index) byte pairs.
static void marioTitleBackgroundStrip(int stripY0) {
    for (int sy = 0; sy < STRIP_H; ++sy) {
        const int y = stripY0 + sy;
        uint32_t at = pgm_read_dword(&MARIO_TITLE_ROW_OFFSETS[y]);
        const uint32_t end = pgm_read_dword(&MARIO_TITLE_ROW_OFFSETS[y + 1]);
        int x = 0;
        while (at + 1 < end && x < SCREEN_W) {
            const uint8_t run = pgm_read_byte(&MARIO_TITLE_BACKGROUND_RLE[at++]);
            const uint8_t index = pgm_read_byte(&MARIO_TITLE_BACKGROUND_RLE[at++]);
            const uint16_t color = pgm_read_word(&MARIO_TITLE_PALETTE_565[index]);
            const int stop = min(SCREEN_W, x + int(run));
            for (; x < stop; ++x) stripBuf[sy * SCREEN_W + x] = color;
        }
    }
}

static void marioTitleTextStrip(int stripY0, const char* text, int x, int y,
                                int scale, uint16_t color) {
    for (const char* p = text; *p; ++p, x += 8 * scale) {
        for (unsigned row = 0; row < 8; ++row) {
            const uint8_t bits = titleGlyphRow(*p, row);
            for (int col = 0; col < 8; ++col) {
                if (!(bits & (0x80 >> col))) continue;
                for (int yy = 0; yy < scale; ++yy)
                    for (int xx = 0; xx < scale; ++xx)
                        putStripPx(x + col * scale + xx, y + int(row) * scale + yy,
                                   stripY0, color);
            }
        }
    }
}

static void marioTitleLabelStrip(int stripY0, const char* text, int y, int scale) {
    const int x = (SCREEN_W - int(strlen(text)) * 8 * scale) / 2;
    marioTitleTextStrip(stripY0, text, x + 1, y + 1, scale, c565(0, 0, 0));
    marioTitleTextStrip(stripY0, text, x, y, scale, c565(255, 255, 255));
}

static void marioTitleSpriteStrip(int stripY0, const MarioSprite& sprite,
                                  int x, int y, bool flip) {
    const int w = pgm_read_byte(&sprite.w), h = pgm_read_byte(&sprite.h);
    const uint8_t* pixels = (const uint8_t*)pgm_read_dword(&sprite.pixels);
    for (int yy = max(y, stripY0); yy < min(y + h, stripY0 + STRIP_H); ++yy) {
        for (int xx = max(x, 0); xx < min(x + w, SCREEN_W); ++xx) {
            const int sourceX = flip ? w - 1 - (xx - x) : xx - x;
            const uint8_t index = pgm_read_byte(&pixels[(yy - y) * w + sourceX]);
            if (index) putStripPx(xx, yy, stripY0, pgm_read_word(&MARIO_PALETTE_565[index]));
        }
    }
}

static void renderMarioScreen() {
    const MarioClockView view = gMarioView;
    int hour = view.hour;
    if (view.use12h) { hour %= 12; if (hour == 0) hour = 12; }
    char clock[9];
    if (view.timeValid && view.hour >= 0 && view.hour <= 23 &&
        view.minute >= 0 && view.minute <= 59 && view.second >= 0 && view.second <= 59) {
        if (view.showSec) snprintf(clock, sizeof(clock), "%02d:%02d:%02d", hour, view.minute, view.second);
        else snprintf(clock, sizeof(clock), "%02d:%02d", hour, view.minute);
    } else strcpy(clock, view.showSec ? "--:--:--" : "--:--");

    char date[20];
    if (view.timeValid && view.year >= 2000 && view.year <= 9999 &&
        view.month >= 1 && view.month <= 12 && view.day >= 1 && view.day <= 31)
        snprintf(date, sizeof(date), "%04d.%02d.%02d KST", view.year, view.month, view.day);
    else strcpy(date, "NTP SYNC");

    // Small title-screen patrol around the original character's hill position.
    // Staying left of x64 prevents the character from covering the date label.
    const unsigned phase = (view.nowMs / 140) % 40;
    const int heroX = 28 + int(phase <= 20 ? phase : 40 - phase);
    const bool flip = phase > 20;
    const bool manual = view.jumpActive && uint32_t(view.nowMs - view.jumpStartedMs) < 900;
    const uint32_t age = manual ? uint32_t(view.nowMs - view.jumpStartedMs) : view.nowMs % 6400;
    const bool jumping = manual || age < 800;
    const int jump = jumping ? mario_scene::jumpHeight(age, manual ? 900 : 800, manual ? 28 : 8) : 0;
    const int heroY = 215 - 16 - jump;
    const uint8_t frame = (view.nowMs / 160) % 3;

    for (int stripY0 = 0; stripY0 < SCREEN_H; stripY0 += STRIP_H) {
        marioTitleBackgroundStrip(stripY0);
        marioTitleLabelStrip(stripY0, clock, 149, 2);
        if ((view.nowMs / 400) % 4 != 3) marioTitleLabelStrip(stripY0, "INSERT COIN", 173, 1);
        marioTitleLabelStrip(stripY0, date, 188, 1);
        marioTitleSpriteStrip(stripY0, jumping ? MARIO_JUMP : MARIO_RUN[frame], heroX, heroY, flip);
        tft.draw16bitRGBBitmap(0, stripY0, stripBuf, SCREEN_W, STRIP_H);
        yield();
    }
}
