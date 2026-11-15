#pragma once

// Shared indoor backdrop for the pet and study timer. Furniture is painted
// behind the character and never touches the HUD or the bottom control strip.
static void roomRectStrip(int stripY0, int x, int y, int w, int h, uint16_t color) {
    for (int yy = max(y, stripY0); yy < min(y + h, stripY0 + STRIP_H); ++yy)
        for (int xx = max(0, x); xx < min(SCREEN_W, x + w); ++xx)
            putStripPx(xx, yy, stripY0, color);
}

static void roomEllipseStrip(int stripY0, int cx, int cy, int rx, int ry, uint16_t color) {
    for (int y = max(cy - ry, stripY0); y <= min(cy + ry, stripY0 + STRIP_H - 1); ++y)
        for (int x = cx - rx; x <= cx + rx; ++x) {
            const int dx = x - cx, dy = y - cy;
            if (dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry)
                putStripPx(x, y, stripY0, color);
        }
}

static void roomStarStrip(int stripY0, int x, int y, uint16_t color) {
    static const uint8_t rows[] = {0x08,0x08,0x3e,0x1c,0x3e,0x14,0x22};
    for (int yy = 0; yy < 7; ++yy)
        for (int xx = 0; xx < 7; ++xx)
            if (rows[yy] & (1 << (6 - xx))) putStripPx(x + xx, y + yy, stripY0, color);
}

static void renderRoomBackgroundStrip(int stripY0) {
    const ThemePalette& th = getActiveTheme();
    const bool night = gRoom.data.wallpaper == 2 || (gRoom.data.wallpaper == 0 && th.isNight);
    const bool soda = gRoom.data.wallpaper == 1;
    const uint16_t cream = c565(255, 250, 241);
    const uint16_t outline = night ? c565(100, 104, 147) : c565(214, 182, 178);
    const uint16_t wall = night ? c565(38, 42, 75) : soda ? c565(221, 243, 255) : c565(255, 235, 242);
    const uint16_t floor = night ? c565(61, 59, 84) : c565(244, 222, 196);
    for (int sy = 0; sy < STRIP_H; ++sy) {
        const int y = stripY0 + sy;
        uint16_t color = y < 144 ? wall : y < 207 ? floor :
                         c565(th.cardBg[0], th.cardBg[1], th.cardBg[2]);
        for (int x = 0; x < SCREEN_W; ++x) stripBuf[sy * SCREEN_W + x] = color;
    }
    // Wallpaper patterns distinguish the two purchased room styles.
    for (int i = 0; i < 10; ++i) {
        const int x = 15 + (i * 29) % 215, y = 64 + (i * 19) % 69;
        if (night) roomStarStrip(stripY0, x, y, c565(239, 211, 130));
        else roomEllipseStrip(stripY0, x, y, 2, 2, soda ? c565(175, 217, 242) : c565(250, 210, 225));
    }
    roomRectStrip(stripY0, 0, 141, 240, 3, outline);
    for (int y = 160; y < 207; y += 20) roomRectStrip(stripY0, 0, y, 240, 1, outline);
    for (int i = 0; i < 7; ++i) roomRectStrip(stripY0, 10 + i * 37, 144, 1, 63, outline);
    // Window and curtain.
    drawRoundedCardStrip(stripY0, 174, 65, 224, 114, 7,
                         night ? c565(24, 31, 63) : c565(183, 226, 251), 15, outline, true);
    if (night) {
        roomEllipseStrip(stripY0, 209, 79, 7, 7, c565(255, 226, 142));
        roomEllipseStrip(stripY0, 212, 76, 6, 6, c565(24, 31, 63));
    } else {
        roomEllipseStrip(stripY0, 188, 84, 8, 4, cream);
        roomEllipseStrip(stripY0, 200, 91, 10, 4, cream);
    }
    roomRectStrip(stripY0, 198, 68, 2, 44, cream);
    roomRectStrip(stripY0, 177, 89, 45, 2, cream);
    roomRectStrip(stripY0, 168, 62, 62, 4, outline);
    roomRectStrip(stripY0, 171, 66, 5, 50, soda ? c565(129, 188, 220) : c565(226, 158, 184));

    if (gRoom.data.rug == 3 || gRoom.data.rug == 4) {
        const bool berry = gRoom.data.rug == 3;
        roomEllipseStrip(stripY0, 119, 184, 61, 17, berry ? c565(219, 111, 142) : c565(91, 115, 177));
        roomEllipseStrip(stripY0, 119, 182, 57, 14, berry ? c565(255, 175, 193) : c565(236, 216, 152));
        if (berry) {
            for (int i = 0; i < 6; ++i) {
                roomEllipseStrip(stripY0, 80 + i * 15, 180 + (i & 1) * 7, 2, 3, cream);
            }
        } else {
            for (int i = 0; i < 5; ++i) roomStarStrip(stripY0, 82 + i * 17, 177 + (i & 1) * 5, c565(116, 132, 181));
        }
    }

    if (gRoom.data.bed == 5 || gRoom.data.bed == 6) {
        const bool moon = gRoom.data.bed == 6;
        const uint16_t frame = moon ? c565(91, 95, 147) : c565(223, 170, 187);
        roomRectStrip(stripY0, 13, 166, 5, 32, frame);
        roomRectStrip(stripY0, 68, 179, 5, 19, frame);
        drawRoundedCardStrip(stripY0, 13, 163, 72, 190, 8, frame, 15, frame, false);
        drawRoundedCardStrip(stripY0, 17, 170, 69, 185, 6, cream, 15, cream, false);
        drawRoundedCardStrip(stripY0, 35, 172, 69, 187, 5,
                             moon ? c565(132, 146, 196) : c565(250, 193, 210), 15, frame, false);
        roomEllipseStrip(stripY0, 25, 175, 9, 5, c565(255, 255, 255));
        if (moon) {
            roomEllipseStrip(stripY0, 54, 178, 5, 5, c565(255, 226, 137));
            roomEllipseStrip(stripY0, 56, 176, 4, 4, c565(132, 146, 196));
        } else {
            roomEllipseStrip(stripY0, 24, 164, 10, 6, cream);
            roomEllipseStrip(stripY0, 36, 161, 10, 7, cream);
            roomEllipseStrip(stripY0, 48, 164, 10, 6, cream);
        }
    }

    if (gRoom.data.decor == 7) {
        roomRectStrip(stripY0, 202, 157, 3, 20, c565(66, 132, 91));
        roomEllipseStrip(stripY0, 197, 156, 8, 4, c565(111, 181, 121));
        roomEllipseStrip(stripY0, 210, 149, 9, 5, c565(87, 160, 105));
        roomEllipseStrip(stripY0, 201, 141, 5, 8, c565(132, 192, 126));
        drawRoundedCardStrip(stripY0, 193, 174, 215, 188, 4, c565(215, 146, 121), 15, outline, true);
        roomRectStrip(stripY0, 191, 172, 26, 4, c565(237, 181, 152));
    } else if (gRoom.data.decor == 8) {
        roomRectStrip(stripY0, 204, 143, 3, 42, c565(166, 133, 96));
        roomEllipseStrip(stripY0, 205, 186, 12, 3, c565(166, 133, 96));
        roomEllipseStrip(stripY0, 205, 140, 13, 13, c565(255, 231, 160));
        roomStarStrip(stripY0, 202, 136, c565(222, 164, 73));
    }
}
