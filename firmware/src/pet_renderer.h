#pragma once

// Pro-only strip renderer. Uses the same compositing primitives and sprites as
// the clock; no screen clearing or full-frame heap allocation is required.
static void petEllipseStrip(int stripY0, int cx, int cy, int rx, int ry,
                            uint16_t color, uint8_t alpha = 15) {
    for (int y = max(cy - ry, stripY0); y <= min(cy + ry, stripY0 + STRIP_H - 1); ++y) {
        for (int x = cx - rx; x <= cx + rx; ++x) {
            const int dx = x - cx, dy = y - cy;
            if (dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry)
                putStripPx(x, y, stripY0, color, alpha);
        }
    }
}

static void petHeartStrip(int stripY0, int x, int y, uint16_t color) {
    static const uint8_t rows[] = {0x36, 0x7f, 0x7f, 0x3e, 0x1c, 0x08};
    for (int yy = 0; yy < 6; ++yy)
        for (int xx = 0; xx < 7; ++xx)
            if (rows[yy] & (1 << (6 - xx))) putStripPx(x + xx, y + yy, stripY0, color);
}

// A single decoded frame is shared by pet and Pomodoro screens.
static const uint8_t* petSpritePixels(const SpriteFrameRLE* table, uint8_t frame) {
    static uint8_t pixels[CHAR_CHIIKAWA_W * CHAR_CHIIKAWA_H];
    static const SpriteFrameRLE* cachedTable = nullptr;
    static uint8_t cachedFrame = 255;
    if (cachedTable != table || cachedFrame != frame) {
        RleStream stream;
        stream.init(table, frame);
        for (size_t i = 0; i < sizeof(pixels); ++i) pixels[i] = stream.nextPixel();
        cachedTable = table;
        cachedFrame = frame;
    }
    return pixels;
}

static void petCharacterStrip(int stripY0, const uint8_t* pixels, int x, int y,
                              int width, int height, bool flip = false) {
    for (int yy = max(y, stripY0); yy <= min(y + height - 1, stripY0 + STRIP_H - 1); ++yy) {
        const int sourceY = (yy - y) * CHAR_CHIIKAWA_H / height;
        for (int xx = 0; xx < width; ++xx) {
            int sourceX = xx * CHAR_CHIIKAWA_W / width;
            if (flip) sourceX = CHAR_CHIIKAWA_W - 1 - sourceX;
            const uint8_t idx = pixels[sourceY * CHAR_CHIIKAWA_W + sourceX];
            if (idx == 0) continue;
            if (idx <= 48) putStripPx(x + xx, yy, stripY0, pgm_read_word(&SPRITE_PALETTE_565[idx]));
            else putStripPx(x + xx, yy, stripY0, c565(54, 42, 46), idx - 48);
        }
    }
}

static void renderPetScreen() {
    const ThemePalette& th = getActiveTheme();
    const uint32_t nowMs = millis();
    const PetActivity activity = gPetGame.activity();
    const bool sleeping = activity == PetActivity::Sleeping;
    const bool eating = activity == PetActivity::Eating;
    const bool happy = activity == PetActivity::Happy;
    const uint8_t frame = (nowMs / (sleeping ? 600 : 180)) & 3;
    const SpriteFrameRLE* table = sleeping ? CHAR_FRAMES_PET_SLEEP :
                                  eating ? CHAR_FRAMES_PET_EAT :
                                  gPetGame.lowNeeds() ? CHAR_FRAMES_PET_SAD : CHAR_FRAMES_CHIIKAWA;
    const uint8_t* pixels = petSpritePixels(table, frame);

    const int petX = gPetGame.x;
    const int bob = sleeping || eating ? 0 : (frame & 1) ? -2 : 0;
    const int petY = gPetGame.y + bob - (happy && frame == 2 ? 5 : 0);
    const uint16_t ink = th.textMain;
    const uint16_t pink = c565(245, 116, 158);
    const uint16_t blue = c565(115, 157, 218);
    const uint16_t cream = c565(255, 252, 246);
    const char* speech = sleeping ? "쿨쿨..." : eating ? "냠냠! 맛있다" :
                         happy ? "좋아요!" : gPetGame.data.fullness < 25 ? "배고파요" :
                         gPetGame.data.energy < 25 ? "졸려요" :
                         gPetGame.data.happiness < 25 ? "놀아줘요" : "산책 중";
    const int speechW = measureText4Bit(FONT_PET_10, speech) + 16;
    const int speechX = constrain(petX + 44 - speechW / 2, 7, 233 - speechW);
    const int speechY = max(59, petY - 22);
    char levelStr[20];
    snprintf(levelStr, sizeof(levelStr), "L%u C%lu", unsigned(gPetGame.level()), (unsigned long)gRoom.data.coins);

    for (int stripY0 = 0; stripY0 < SCREEN_H; stripY0 += STRIP_H) {
        renderRoomBackgroundStrip(stripY0);

        petEllipseStrip(stripY0, petX + 43, gPetGame.y + 78, 26, 4,
                        th.isNight ? c565(20, 43, 43) : c565(116, 157, 112), 5);

        petCharacterStrip(stripY0, pixels, petX, petY, 87, 81, gPetGame.facingLeft());

        drawRoundedCardStrip(stripY0, speechX, speechY, speechX + speechW, speechY + 18,
                             7, cream, 15, th.cardBorder, true);
        drawText4BitStrip(stripY0, FONT_PET_10, speech, speechX + 8, speechY + 3,
                         c565(96, 67, 82));
        if (happy) {
            petHeartStrip(stripY0, petX + 8, petY + 12 - frame * 2, pink);
            petHeartStrip(stripY0, petX + 73, petY + 5 + frame * 2, pink);
        }
        if (sleeping) drawText4BitStrip(stripY0, FONT_PET_13, "Zzz", petX + 61, petY + 8, blue);
        if (eating) {
            petEllipseStrip(stripY0, petX + 44, petY + 61, 11, 6, cream);
            drawRoundedCardStrip(stripY0, petX + 32, petY + 62, petX + 56, petY + 70,
                                 4, pink, 15, pink, false);
        }

        // The HUD is drawn last so motion never paints over the controls.
        drawRoundedCardStrip(stripY0, 6, 5, 233, 56, 12,
                             th.msgCardBg, 15, th.cardBorder, true);
        drawText4BitStrip(stripY0, FONT_STUDY_13, "치이카와의 방", 14, 12, ink);
        drawText4BitStrip(stripY0, FONT_STUDY_10, levelStr, 225 - measureText4Bit(FONT_STUDY_10, levelStr), 15, th.accent);
        const uint8_t stats[] = {gPetGame.data.fullness, gPetGame.data.happiness, gPetGame.data.energy};
        const char* labels[] = {"밥", "행복", "기력"};
        const uint16_t colors[] = {c565(90, 174, 122), pink, blue};
        const int starts[] = {14, 86, 158};
        for (int i = 0; i < 3; ++i) {
            drawText4BitStrip(stripY0, FONT_PET_10, labels[i], starts[i], 36, th.textSub);
            const int x = starts[i] + 24;
            drawRoundedCardStrip(stripY0, x, 39, x + 40, 45, 3,
                                 th.cardBorder, 8, th.cardBorder, false);
            if (stats[i]) drawRoundedCardStrip(stripY0, x, 39,
                                               x + max(3, int(stats[i]) * 40 / 100), 45, 3,
                                               colors[i], 15, colors[i], false);
        }
        const int xpWidth = int(gPetGame.data.xp % 100) * 220 / 100;
        for (int x = 10; x < 230; ++x) putStripPx(x, 210, stripY0, th.cardBorder);
        for (int x = 10; x < 10 + xpWidth; ++x) putStripPx(x, 210, stripY0, pink);
        const char* hint = "터치 놀이 / 꾹 밥 / 더블 환율";
        const int hintX = (240 - measureText4Bit(FONT_PET_10, hint)) / 2;
        drawText4BitStrip(stripY0, FONT_PET_10, hint, hintX, 222, th.textSub);

        tft.draw16bitRGBBitmap(0, stripY0, stripBuf, SCREEN_W, STRIP_H);
        yield();
    }
}
