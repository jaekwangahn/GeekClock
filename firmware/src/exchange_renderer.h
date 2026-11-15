#pragma once

#include <math.h>

// Snapshot-only Pro renderer: networking and cache updates happen elsewhere.
// The screen distinguishes a source rate date from the device's fetch time.
static void fxEllipseStrip(int stripY0, int cx, int cy, int rx, int ry, uint16_t color) {
    for (int y = max(cy - ry, stripY0); y <= min(cy + ry, stripY0 + STRIP_H - 1); ++y)
        for (int x = cx - rx; x <= cx + rx; ++x) {
            const int dx = x - cx, dy = y - cy;
            if (dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry)
                putStripPx(x, y, stripY0, color);
        }
}

static void renderExchangeScreen() {
    const ThemePalette& th = getActiveTheme();
    const bool hasRate = gExchangeView.valid && isfinite(gExchangeView.krwPerJpy) && gExchangeView.krwPerJpy > 0 && gExchangeView.krwPerJpy <= 1000;
    const bool saved = hasRate && (gExchangeView.stale || gExchangeView.error[0]);
    const uint16_t card = c565(th.cardBg[0], th.cardBg[1], th.cardBg[2]);
    const uint16_t yenRed = c565(224, 82, 103);
    const uint16_t statusColor = gExchangeView.loading ? c565(185, 132, 45) :
                                 saved ? c565(185, 132, 45) : th.accent;
    const char* state = gExchangeView.loading ? "갱신 중..." : saved ? "저장값 / 연결 확인" :
                        hasRate ? "참고환율" : "데이터 없음 / 연결 확인";
    char amount[32];
    if (hasRate) snprintf(amount, sizeof(amount), "%.2f", gExchangeView.krwPerJpy * 100.0);
    else strcpy(amount, "—");
    const Font4Bit* amountFont = &FONT_FX_32;
    int amountWidth = measureText4Bit(*amountFont, amount);
    if (amountWidth > 185) { amountFont = &FONT_FX_21; amountWidth = measureText4Bit(*amountFont, amount); }
    if (amountWidth > 185) { amountFont = &FONT_FX_13; amountWidth = measureText4Bit(*amountFont, amount); }
    const int amountX = hasRate ? max(15, (240 - amountWidth - 22) / 2) : (240 - amountWidth) / 2;
    const int amountY = amountFont == &FONT_FX_32 ? 88 : amountFont == &FONT_FX_21 ? 95 : 99;
    char rateDate[32], fetched[40];
    snprintf(rateDate, sizeof(rateDate), "기준  %s", gExchangeView.rateDate[0] ? gExchangeView.rateDate : "—");
    // A compact KST fetch label, kept separate from the provider's rate date.
    const char* at = gExchangeView.fetchedAt;
    char compact[24] = {};
    if (strlen(at) >= 16 && at[4] == '-' && at[7] == '-') {
        snprintf(compact, sizeof(compact), "%.5s %.5s KST", at + 5, at + 11);
    } else if (at[0]) {
        snprintf(compact, sizeof(compact), "%.20s", at);
    } else strcpy(compact, "—");
    snprintf(fetched, sizeof(fetched), "확인  %s", compact);

    for (int stripY0 = 0; stripY0 < SCREEN_H; stripY0 += STRIP_H) {
        for (int sy = 0; sy < STRIP_H; ++sy) {
            const int y = stripY0 + sy;
            const uint8_t r = th.bgTop[0] + (int(th.bgBot[0]) - th.bgTop[0]) * y / 239;
            const uint8_t g = th.bgTop[1] + (int(th.bgBot[1]) - th.bgTop[1]) * y / 239;
            const uint8_t b = th.bgTop[2] + (int(th.bgBot[2]) - th.bgTop[2]) * y / 239;
            for (int x = 0; x < SCREEN_W; ++x) stripBuf[sy * SCREEN_W + x] = c565(r, g, b);
        }
        drawText4BitStrip(stripY0, FONT_FX_13, "일본 엔화 환율", 16, 16, th.textMain);
        drawText4BitStrip(stripY0, FONT_FX_10, "JPY / KRW", 17, 37, th.textSub);
        drawRoundedCardStrip(stripY0, 195, 14, 224, 36, 4, c565(255, 255, 255), 15, th.cardBorder, true);
        fxEllipseStrip(stripY0, 210, 25, 6, 6, yenRed);

        drawRoundedCardStrip(stripY0, 8, 59, 231, 137, 16, card, 15, th.cardBorder, true);
        drawText4BitStrip(stripY0, FONT_FX_10, "100 JPY =", 20, 69, th.textSub);
        drawText4BitStrip(stripY0, *amountFont, amount, amountX, amountY, th.accent);
        if (hasRate) drawText4BitStrip(stripY0, FONT_FX_13, "원", amountX + amountWidth + 5, 102, th.textMain);
        else drawText4BitStrip(stripY0, FONT_FX_10, "데이터 없음", 147, 72, th.textSub);

        drawRoundedCardStrip(stripY0, 8, 145, 231, 208, 12, card, 13, th.cardBorder, false);
        drawText4BitStrip(stripY0, FONT_FX_10, state, 18, 151, statusColor);
        drawText4BitStrip(stripY0, FONT_FX_10, rateDate, 18, 168, th.textMain);
        drawText4BitStrip(stripY0, FONT_FX_10, fetched, 18, 186, th.textSub);
        drawText4BitStrip(stripY0, FONT_FX_10, "ECB / Frankfurter 참고환율", 18, 212, th.textSub);
        const char* hint = "터치 새로고침 / 더블 다음";
        const int hintX = (240 - measureText4Bit(FONT_FX_10, hint)) / 2;
        drawText4BitStrip(stripY0, FONT_FX_10, hint, hintX, 228, th.textSub);
        tft.draw16bitRGBBitmap(0, stripY0, stripBuf, SCREEN_W, STRIP_H);
        yield();
    }
}
