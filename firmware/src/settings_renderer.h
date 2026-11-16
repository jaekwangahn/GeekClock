#pragma once

static String settingsSsid(const String& ssid) {
    String label;
    const char* cursor = ssid.c_str();
    int width = 0;
    const int ellipsisWidth = measureText4Bit(FONT_UI_13, "...");
    while (*cursor) {
        const char* start = cursor;
        uint16_t codepoint = nextUtf8Codepoint(cursor);
        Glyph4Bit glyph{};
        const uint8_t* bitmap = nullptr;
        const bool supported = findGlyph(FONT_UI_13, codepoint, glyph, bitmap);
        const int advance = supported ? glyph.xAdvance : measureText4Bit(FONT_UI_13, "?");
        if (width + advance + (*cursor ? ellipsisWidth : 0) > 194) {
            label += "...";
            break;
        }
        if (supported) {
            while (start < cursor) label += *start++;
        } else {
            label += '?';
        }
        width += advance;
    }
    return label;
}

static void renderSettingsScreen() {
    const ThemePalette& th = getActiveTheme();
    const bool connected = WiFi.status() == WL_CONNECTED;
    const bool setupAp = gApFallbackActive && !connected;
    const String ssid = settingsSsid(connected ? WiFi.SSID() : (setupAp ? SETUP_SSID : "-"));
    const String ip = connected ? WiFi.localIP().toString() :
                      setupAp ? WiFi.softAPIP().toString() : "-";
    const String ipLine = String("IP: ") + ip;
    const size_t total = gFsMounted ? LittleFS.totalBytes() : 0;
    const size_t used = total ? LittleFS.usedBytes() : 0;
    char storage[64], flash[32];
    snprintf(flash, sizeof(flash), "Flash: %.0f MiB", ESP.getFlashChipSize() / 1048576.0);
    if (total) {
        snprintf(storage, sizeof(storage), "%.2f / %.2f MiB free",
                 (total - min(total, used)) / 1048576.0, total / 1048576.0);
    } else {
        strlcpy(storage, "Unavailable", sizeof(storage));
    }
    const char* action = "Hold: reset Wi-Fi";
    const char* hint = "Double tap: clock";
    if (gWifiResetFailed) action = "Save failed. Hold to retry";
    else if (gWifiResetArmed) {
        action = "Hold again to reset Wi-Fi";
        hint = "Tap to cancel (8 seconds)";
    } else if (setupAp) {
        action = "Connect to setup Wi-Fi";
        hint = "Open the IP above in a browser";
    } else if (proWifiSetupRequested()) {
        action = "Starting setup hotspot...";
    }

    for (int stripY0 = 0; stripY0 < SCREEN_H; stripY0 += STRIP_H) {
        renderRoomBackgroundStrip(stripY0);
        drawRoundedCardStrip(stripY0, 10, 12, 229, 47, 11, th.msgCardBg, 15, th.cardBorder, true);
        studyCenteredText(stripY0, FONT_UI_13, "SmallTV Pro Settings", 22, th.textMain);
        drawRoundedCardStrip(stripY0, 10, 55, 229, 103, 11, th.msgCardBg, 15, th.cardBorder, true);
        drawText4BitStrip(stripY0, FONT_SUB_10, "Storage (LittleFS)", 23, 59, th.accent);
        drawText4BitStrip(stripY0, FONT_UI_13, storage, 23, 72, th.textMain);
        drawText4BitStrip(stripY0, FONT_SUB_10, flash, 23, 88, th.textSub);
        drawRoundedCardStrip(stripY0, 10, 110, 229, 176, 11, th.msgCardBg, 15, th.cardBorder, true);
        drawText4BitStrip(stripY0, FONT_SUB_10, setupAp ? "SSID (setup hotspot)" : "SSID", 23, 117, th.accent);
        drawText4BitStrip(stripY0, FONT_UI_13, ssid.c_str(), 23, 132, th.textMain);
        drawText4BitStrip(stripY0, FONT_UI_13, ipLine.c_str(), 23, 151, th.textMain);
        drawRoundedCardStrip(stripY0, 10, 184, 229, 232, 11, th.msgCardBg, 15, th.cardBorder, true);
        studyCenteredText(stripY0, FONT_UI_13, action, 191, th.textMain);
        studyCenteredText(stripY0, FONT_SUB_10, hint, 212, th.textSub);
        tft.draw16bitRGBBitmap(0, stripY0, stripBuf, SCREEN_W, STRIP_H);
        yield();
    }
}
