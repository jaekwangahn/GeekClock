#pragma once

// Uses the pet renderer's shared decoded sprite frame and room backdrop.
static void studyCenteredText(int stripY0, const Font4Bit& font, const char* text,
                              int y, uint16_t color) {
    drawText4BitStrip(stripY0, font, text, (240 - measureText4Bit(font, text)) / 2, y, color);
}

static void renderPomodoroScreen() {
    const ThemePalette& th = getActiveTheme();
    const uint32_t nowMs = millis();
    const bool focus = gPomodoro.data.phase == 0;
    const bool running = gPomodoro.data.running;
    const uint32_t seconds = (gPomodoro.data.remainingMs + 999UL) / 1000UL;
    const uint32_t duration = gPomodoro.phaseDurationMs();
    const uint32_t remaining = min(gPomodoro.data.remainingMs, duration);
    const int progress = duration ? int((uint64_t(duration - remaining) * 190) / duration) : 0;
    const uint16_t accent = focus ? c565(239, 109, 151) : c565(89, 163, 151);
    const uint16_t card = c565(255, 252, 248);
    const uint16_t ink = c565(82, 59, 73);
    const uint16_t muted = c565(149, 117, 128);
    const char* phase = focus ? "집중" : gPomodoro.data.phase == 1 ? "짧은 휴식" : "긴 휴식";
    const char* state = running ? "진행 중" :
                        gPomodoro.data.remainingMs == duration ? "준비" : "일시정지";
    const bool reward = gStudyRewardCoins > 0 && uint32_t(nowMs - gStudyRewardAtMs) < 8000;
    char timer[12], coins[18], rewardLine[60], countLine[24], minutesLine[24];
    snprintf(timer, sizeof(timer), "%02lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
    snprintf(coins, sizeof(coins), "C %lu", (unsigned long)gRoom.data.coins);
    const unsigned nextCoins = max(1, int(gPomodoro.data.focusMinutes) * 2 / 5);
    if (reward) snprintf(rewardLine, sizeof(rewardLine), "집중 완료! +%u 코인", unsigned(gStudyRewardCoins));
    else snprintf(rewardLine, sizeof(rewardLine), "다음 보상 +%u 코인", nextCoins);
    if (gPomodoro.data.completedFocus < 100000)
        snprintf(countLine, sizeof(countLine), "%lu회", (unsigned long)gPomodoro.data.completedFocus);
    else strcpy(countLine, "99999+회");
    if (gPomodoro.data.totalFocusMinutes < 100000)
        snprintf(minutesLine, sizeof(minutesLine), "총 %lu분", (unsigned long)gPomodoro.data.totalFocusMinutes);
    else strcpy(minutesLine, "총 99999+분");

    const uint8_t frame = (nowMs / (focus ? 400 : 700)) & 3;
    const uint8_t* pixels = petSpritePixels(focus ? CHAR_FRAMES_CHIIKAWA : CHAR_FRAMES_PET_SLEEP, frame);
    const int bob = running && (frame & 1) ? -1 : 0;

    for (int stripY0 = 0; stripY0 < SCREEN_H; stripY0 += STRIP_H) {
        renderRoomBackgroundStrip(stripY0);
        drawRoundedCardStrip(stripY0, 7, 6, 232, 44, 12, card, 15, th.cardBorder, true);
        drawText4BitStrip(stripY0, FONT_STUDY_13, "치이카와 뽀모도로", 15, 14, ink);
        drawText4BitStrip(stripY0, FONT_STUDY_10, coins,
                         224 - measureText4Bit(FONT_STUDY_10, coins), 18, accent);

        drawRoundedCardStrip(stripY0, 10, 51, 229, 143, 14, card, 15, th.cardBorder, true);
        drawText4BitStrip(stripY0, FONT_STUDY_13, phase, 21, 59, accent);
        drawText4BitStrip(stripY0, FONT_STUDY_10, state,
                         219 - measureText4Bit(FONT_STUDY_10, state), 62, muted);
        studyCenteredText(stripY0, FONT_TIME_40, timer, 77, ink);
        drawRoundedCardStrip(stripY0, 24, 120, 215, 125, 3, c565(244, 223, 230), 15,
                             c565(244, 223, 230), false);
        if (progress > 0) drawRoundedCardStrip(stripY0, 24, 120, 24 + max(progress, 3), 125, 3,
                                              accent, 15, accent, false);
        studyCenteredText(stripY0, FONT_STUDY_10, rewardLine, 130, reward ? c565(184, 115, 37) : muted);

        // Small Chiikawa sits at a desk while focusing, on a pillow at rest.
        petCharacterStrip(stripY0, pixels, 30, 148 + bob, 61, 57);
        if (focus) {
            drawRoundedCardStrip(stripY0, 17, 194, 111, 201, 3,
                                 c565(191, 140, 111), 15, c565(159, 113, 94), true);
            roomRectStrip(stripY0, 22, 200, 4, 7, c565(159, 113, 94));
            roomRectStrip(stripY0, 102, 200, 4, 7, c565(159, 113, 94));
            drawRoundedCardStrip(stripY0, 64, 187, 92, 194, 2,
                                 c565(255, 251, 224), 15, c565(207, 176, 147), true);
            roomRectStrip(stripY0, 77, 188, 1, 5, c565(207, 176, 147));
            drawRoundedCardStrip(stripY0, 95, 181, 104, 193, 3,
                                 c565(244, 188, 203), 15, c565(214, 154, 174), true);
        } else {
            petEllipseStrip(stripY0, 60, 200, 39, 6, c565(178, 196, 226));
            drawText4BitStrip(stripY0, FONT_STUDY_13, "Zzz", 90, 156, c565(115, 147, 185));
        }

        drawRoundedCardStrip(stripY0, 129, 151, 230, 205, 10, card, 15, th.cardBorder, true);
        drawText4BitStrip(stripY0, FONT_STUDY_10, "집중 완료", 140, 158, muted);
        drawText4BitStrip(stripY0, FONT_STUDY_13, countLine, 140, 171, ink);
        drawText4BitStrip(stripY0, FONT_STUDY_10, minutesLine, 140, 190, muted);
        const int completed = gPomodoro.data.phase == 2 ? 4 : int(gPomodoro.data.cycle);
        for (int i = 0; i < 4; ++i)
            roomEllipseStrip(stripY0, 202 + (i & 1) * 12, 163 + (i / 2) * 12,
                             3, 3, i < completed ? accent : c565(231, 215, 219));

        roomRectStrip(stripY0, 0, 211, 240, 29, card);
        const char* hint = "터치 시작/정지 · 꾹 초기화";
        studyCenteredText(stripY0, FONT_STUDY_10, hint, 220, muted);
        tft.draw16bitRGBBitmap(0, stripY0, stripBuf, SCREEN_W, STRIP_H);
        yield();
    }
}
