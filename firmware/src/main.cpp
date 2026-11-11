#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <TZ.h>
#include <coredecls.h>
#include <sys/time.h>
#include <time.h>

#include "chiikawa_assets.h"
#include "web_ui.h"

// ============================================================================
// Hardware Configuration (GeekMagic SmallTV / SmallTV-Ultra ESP8266)
// ============================================================================
constexpr uint8_t PIN_BACKLIGHT = 5;  // GPIO5 PWM (Active LOW on SmallTV-Ultra)
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 240;
constexpr int STRIP_H  = 20;          // 240x20x2 = 9,600 bytes static RAM buffer (12 strips, zero flicker!)

static TFT_eSPI tft = TFT_eSPI();
static uint16_t stripBuf[SCREEN_W * STRIP_H];

static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer httpUpdater;

// ============================================================================
// Persistent Configuration & Live State
// ============================================================================
struct ClockConfig {
    char character[16];     // "chiikawa", "hachiware", "usagi", "trio"
    char theme[16];         // "pink", "sky", "butter", "lavender", "night"
    uint8_t brightness;     // 3..100 (%)
    bool nightDim;          // Auto dim 00:00..06:59
    bool anim;              // Animate character
    bool use12h;            // 12h vs 24h
    bool showSec;           // Show :SS + blinking colon
    char ntpServer[48];     // Default: "time.google.com"
    char city[24];          // Default: "서울"
    float lat;              // Default: 37.5665
    float lon;              // Default: 126.9780
    char customText[84];    // UTF-8 bottom message
    char ddayTitle[36];     // UTF-8 D-Day title
    char ddayDate[16];      // "YYYY-MM-DD"
    uint8_t rotation;       // 0..3 (default 0)
    bool invertDisp;        // ST7789 IPS inversion (default true)
    bool blInvert;          // Active-LOW backlight on GPIO5 (default true)
};

static ClockConfig gCfg;

struct WeatherState {
    bool valid = false;
    float temp = 18.0f;
    float tMin = 12.0f;
    float tMax = 21.0f;
    uint8_t iconIdx = 0;    // 0:sunny, 1:partly_cloudy, 2:cloudy, 3:rain, 4:snow
    unsigned long lastFetchMs = 0;
};

static WeatherState gWeather;
static volatile bool gNtpSynced = false;
static time_t gLastNtpEpoch = 0;

// Optional client-rasterized 196x18 4-bit alpha bitmap for arbitrary Hangul bottom text
constexpr int MSG_BMP_W = 196;
constexpr int MSG_BMP_H = 18;
constexpr int MSG_BMP_BYTES = (MSG_BMP_W * MSG_BMP_H) / 2; // 1,764 bytes
static uint8_t gMsgBmp[MSG_BMP_BYTES];
static bool gHasMsgBmp = false;

static bool gApFallbackActive = false;
static unsigned long gLastWifiRetryMs = 0;
static unsigned long gAnimTickMs = 0;
static uint8_t gAnimFrame = 0;
static bool gForceRedraw = true;

// ============================================================================
// Color Themes (Matches chiikawa_art.py 100%)
// ============================================================================
struct ThemePalette {
    uint8_t bgTop[3];
    uint8_t bgBot[3];
    uint8_t cardBg[3];
    uint8_t cardAlpha;   // 0..15
    uint16_t cardBorder;
    uint16_t accent;
    uint16_t textMain;
    uint16_t textSub;
    uint16_t badgeBg;
    uint16_t msgCardBg;
    bool isNight;
};

constexpr uint16_t c565(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

static const ThemePalette THEME_PINK = {
    {255, 232, 240}, {255, 248, 250}, {255, 255, 255}, 14,
    c565(255, 188, 210), c565(255, 105, 150), c565(68, 46, 56), c565(140, 105, 118),
    c565(255, 115, 158), c565(255, 255, 255), false
};
static const ThemePalette THEME_SKY = {
    {218, 240, 255}, {246, 252, 255}, {255, 255, 255}, 14,
    c565(168, 216, 250), c565(72, 156, 228), c565(42, 62, 84), c565(102, 128, 154),
    c565(78, 162, 232), c565(255, 255, 255), false
};
static const ThemePalette THEME_BUTTER = {
    {255, 244, 200}, {255, 252, 238}, {255, 255, 255}, 14,
    c565(250, 214, 138), c565(235, 148, 45), c565(78, 56, 34), c565(146, 116, 82),
    c565(242, 156, 52), c565(255, 255, 255), false
};
static const ThemePalette THEME_LAVENDER = {
    {238, 226, 255}, {250, 246, 255}, {255, 255, 255}, 14,
    c565(206, 184, 248), c565(152, 108, 224), c565(58, 44, 82), c565(122, 104, 152),
    c565(156, 112, 228), c565(255, 255, 255), false
};
static const ThemePalette THEME_NIGHT = {
    {24, 28, 56}, {46, 52, 92}, {38, 44, 78}, 14,
    c565(96, 110, 172), c565(255, 198, 92), c565(248, 246, 255), c565(182, 190, 226),
    c565(255, 136, 172), c565(42, 48, 86), true
};

static const ThemePalette& getActiveTheme() {
    if (strcmp(gCfg.theme, "sky") == 0) return THEME_SKY;
    if (strcmp(gCfg.theme, "butter") == 0) return THEME_BUTTER;
    if (strcmp(gCfg.theme, "lavender") == 0) return THEME_LAVENDER;
    if (strcmp(gCfg.theme, "night") == 0) return THEME_NIGHT;
    return THEME_PINK;
}

// ============================================================================
// Fast Pixel & Alpha Blending Utilities
// ============================================================================
static inline uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a15) {
    if (a15 == 0) return bg;
    if (a15 >= 15) return fg;
    uint8_t a = a15 + 1;
    uint8_t inv = 16 - a;
    uint16_t r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * inv) >> 4;
    uint16_t g = (((fg >> 5)  & 0x3F) * a + ((bg >> 5)  & 0x3F) * inv) >> 4;
    uint16_t b = (( fg        & 0x1F) * a + ( bg        & 0x1F) * inv) >> 4;
    return (r << 11) | (g << 5) | b;
}

static inline void putStripPx(int x, int y, int stripY0, uint16_t col, uint8_t a15 = 15) {
    int sy = y - stripY0;
    if ((unsigned)x >= SCREEN_W || (unsigned)sy >= STRIP_H) return;
    int idx = sy * SCREEN_W + x;
    stripBuf[idx] = (a15 >= 15) ? col : blend565(stripBuf[idx], col, a15);
}

// Draws an anti-aliased rounded rectangle into the current strip [stripY0 .. stripY0 + STRIP_H - 1]
static void drawRoundedCardStrip(
    int stripY0, int x1, int y1, int x2, int y2, int r,
    uint16_t fillCol, uint8_t fillAlpha15, uint16_t borderCol, bool hasBorder
) {
    int syStart = max(y1, stripY0);
    int syEnd   = min(y2, stripY0 + STRIP_H - 1);
    if (syStart > syEnd) return;

    int rSqIn  = (r - 1) * (r - 1);
    int rSqBorderIn = max(0, (r - 2) * (r - 2));
    int rSqOut = r * r;

    for (int y = syStart; y <= syEnd; ++y) {
        uint16_t* row = &stripBuf[(y - stripY0) * SCREEN_W];
        bool inTopBotCorner = (y < y1 + r) || (y > y2 - r);
        int cy = (y < y1 + r) ? (y1 + r) : (y2 - r);
        int dy = y - cy;
        int dy2 = dy * dy;

        for (int x = max(0, x1); x <= min(SCREEN_W - 1, x2); ++x) {
            if (!inTopBotCorner && x >= x1 + r && x <= x2 - r) {
                if (hasBorder && (y <= y1 + 1 || y >= y2 - 1)) {
                    row[x] = borderCol;
                } else {
                    row[x] = (fillAlpha15 >= 15) ? fillCol : blend565(row[x], fillCol, fillAlpha15);
                }
                continue;
            }
            if (!inTopBotCorner) {
                if (hasBorder && (x <= x1 + 1 || x >= x2 - 1)) {
                    row[x] = borderCol;
                } else {
                    row[x] = (fillAlpha15 >= 15) ? fillCol : blend565(row[x], fillCol, fillAlpha15);
                }
                continue;
            }
            if (x >= x1 + r && x <= x2 - r) {
                if (hasBorder && (y <= y1 + 1 || y >= y2 - 1)) {
                    row[x] = borderCol;
                } else {
                    row[x] = (fillAlpha15 >= 15) ? fillCol : blend565(row[x], fillCol, fillAlpha15);
                }
                continue;
            }

            int cx = (x < x1 + r) ? (x1 + r) : (x2 - r);
            int dx = x - cx;
            int d2 = dx * dx + dy2;
            if (d2 > rSqOut + r) continue;
            if (d2 > rSqOut) {
                // Soft outer edge anti-alias
                row[x] = blend565(row[x], hasBorder ? borderCol : fillCol, 6);
            } else if (hasBorder && d2 >= rSqBorderIn) {
                row[x] = borderCol;
            } else if (d2 <= rSqIn) {
                row[x] = (fillAlpha15 >= 15) ? fillCol : blend565(row[x], fillCol, fillAlpha15);
            } else {
                row[x] = (fillAlpha15 >= 15) ? fillCol : blend565(row[x], fillCol, fillAlpha15);
            }
        }
    }
}

// ============================================================================
// UTF-8 + 4-Bit Anti-Aliased PROGMEM Font Renderer
// ============================================================================
static uint16_t nextUtf8Codepoint(const char*& p) {
    uint8_t c = (uint8_t)*p;
    if (!c) return 0;
    ++p;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) {
        uint8_t c2 = (uint8_t)*p;
        if ((c2 & 0xC0) == 0x80) { ++p; return ((uint16_t)(c & 0x1F) << 6) | (c2 & 0x3F); }
        return '?';
    }
    if ((c & 0xF0) == 0xE0) {
        uint8_t c2 = (uint8_t)p[0];
        uint8_t c3 = (uint8_t)p[1];
        if (((c2 & 0xC0) == 0x80) && ((c3 & 0xC0) == 0x80)) {
            p += 2;
            return ((uint16_t)(c & 0x0F) << 12) | ((uint16_t)(c2 & 0x3F) << 6) | (c3 & 0x3F);
        }
        return '?';
    }
    while (((uint8_t)*p & 0xC0) == 0x80) ++p;
    return '?';
}

static bool findGlyph(const Font4Bit& fontProg, uint16_t cp, Glyph4Bit& outG, const uint8_t*& outBmpBase) {
    uint16_t count = pgm_read_word(&fontProg.glyphCount);
    const Glyph4Bit* glyphs = (const Glyph4Bit*)pgm_read_dword(&fontProg.glyphs);
    outBmpBase = (const uint8_t*)pgm_read_dword(&fontProg.bitmap);

    int lo = 0, hi = (int)count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint16_t mcp = pgm_read_word(&glyphs[mid].codepoint);
        if (mcp == cp) {
            outG.codepoint    = mcp;
            outG.width        = pgm_read_byte(&glyphs[mid].width);
            outG.height       = pgm_read_byte(&glyphs[mid].height);
            outG.xOffset      = (int8_t)pgm_read_byte(&glyphs[mid].xOffset);
            outG.yOffset      = (int8_t)pgm_read_byte(&glyphs[mid].yOffset);
            outG.xAdvance     = pgm_read_byte(&glyphs[mid].xAdvance);
            outG.bitmapOffset = pgm_read_word(&glyphs[mid].bitmapOffset);
            return true;
        }
        if (mcp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return false;
}

static int measureText4Bit(const Font4Bit& fontProg, const char* utf8Str) {
    int w = 0;
    const char* p = utf8Str;
    Glyph4Bit g;
    const uint8_t* bmpBase = nullptr;
    while (*p) {
        uint16_t cp = nextUtf8Codepoint(p);
        if (!cp) break;
        if (findGlyph(fontProg, cp, g, bmpBase)) {
            w += g.xAdvance;
        } else {
            w += pgm_read_byte(&fontProg.lineHeight) / 2;
        }
    }
    return w;
}

static void drawText4BitStrip(
    int stripY0, const Font4Bit& fontProg, const char* utf8Str,
    int x, int y, uint16_t color, uint8_t alphaScale15 = 15
) {
    int lineH = pgm_read_byte(&fontProg.lineHeight);
    if (y + lineH + 4 < stripY0 || y - 4 >= stripY0 + STRIP_H) return;

    const char* p = utf8Str;
    int penX = x;
    Glyph4Bit g;
    const uint8_t* bmpBase = nullptr;

    while (*p) {
        uint16_t cp = nextUtf8Codepoint(p);
        if (!cp) break;
        if (!findGlyph(fontProg, cp, g, bmpBase)) {
            penX += lineH / 2;
            continue;
        }
        if (g.width > 0 && g.height > 0) {
            int gx0 = penX + g.xOffset;
            int gy0 = y + g.yOffset;
            if (gy0 + g.height > stripY0 && gy0 < stripY0 + STRIP_H) {
                int rStart = max(0, stripY0 - gy0);
                int rEnd   = min((int)g.height - 1, stripY0 + STRIP_H - 1 - gy0);
                for (int row = rStart; row <= rEnd; ++row) {
                    uint16_t* dstRow = &stripBuf[(gy0 + row - stripY0) * SCREEN_W];
                    int pixBase = row * g.width;
                    for (int col = 0; col < g.width; ++col) {
                        int dx = gx0 + col;
                        if ((unsigned)dx >= SCREEN_W) continue;
                        int pIdx = pixBase + col;
                        uint8_t packed = pgm_read_byte(&bmpBase[g.bitmapOffset + (pIdx >> 1)]);
                        uint8_t nib = (pIdx & 1) ? (packed & 0x0F) : (packed >> 4);
                        if (nib == 0) continue;
                        if (alphaScale15 < 15) nib = (nib * alphaScale15) / 15;
                        dstRow[dx] = (nib >= 15) ? color : blend565(dstRow[dx], color, nib);
                    }
                }
            }
        }
        penX += g.xAdvance;
    }
}

// ============================================================================
// Streaming O(1)-RAM RLE6 Sprite Decoder across Strips
// ============================================================================
struct RleStream {
    const uint8_t* ptr = nullptr;
    const uint8_t* end = nullptr;
    uint16_t remRun = 0;
    uint8_t curIdx = 0;

    void init(const SpriteFrameRLE* tableProg, uint8_t index) {
        uint16_t len = pgm_read_word(&tableProg[index].len);
        ptr = (const uint8_t*)pgm_read_dword(&tableProg[index].data);
        end = ptr + len;
        remRun = 0;
        curIdx = 0;
    }

    inline uint8_t nextPixel() {
        if (remRun == 0) {
            if (ptr >= end) return 0;
            uint8_t b = pgm_read_byte(ptr++);
            uint8_t top = b >> 6;
            curIdx = b & 0x3F;
            if (top < 3) {
                remRun = top + 1;
            } else {
                uint8_t extra = (ptr < end) ? pgm_read_byte(ptr++) : 0;
                remRun = (uint16_t)extra + 4;
            }
        }
        --remRun;
        return curIdx;
    }

    void skipPixels(uint32_t count) {
        while (count > 0) {
            if (remRun == 0) {
                if (ptr >= end) return;
                uint8_t b = pgm_read_byte(ptr++);
                uint8_t top = b >> 6;
                curIdx = b & 0x3F;
                if (top < 3) remRun = top + 1;
                else {
                    uint8_t extra = (ptr < end) ? pgm_read_byte(ptr++) : 0;
                    remRun = (uint16_t)extra + 4;
                }
            }
            uint32_t step = (count < remRun) ? count : remRun;
            remRun -= step;
            count  -= step;
        }
    }
};

static void blitRleStreamStrip(
    RleStream& stream, int stripY0, int sprX, int sprY, int sprW, int sprH
) {
    constexpr uint16_t OUTLINE_565 = c565(54, 42, 46);
    int yStart = max(sprY, stripY0);
    int yEnd   = min(sprY + sprH - 1, stripY0 + STRIP_H - 1);
    if (yStart > yEnd) return;

    for (int y = yStart; y <= yEnd; ++y) {
        uint16_t* dstRow = &stripBuf[(y - stripY0) * SCREEN_W];
        for (int col = 0; col < sprW; ++col) {
            uint8_t idx = stream.nextPixel();
            if (idx == 0) continue;
            int dx = sprX + col;
            if ((unsigned)dx >= SCREEN_W) continue;
            if (idx <= 48) {
                dstRow[dx] = pgm_read_word(&SPRITE_PALETTE_565[idx]);
            } else {
                // Indices 49..63 = 15 alpha steps (1..15) of Chiikawa outline (54, 42, 46)
                uint8_t a15 = idx - 48;
                dstRow[dx] = blend565(dstRow[dx], OUTLINE_565, a15);
            }
        }
    }
}

// ============================================================================
// D-Day Calculation Helper
// ============================================================================
static void formatDdayBadge(const struct tm& nowTm, char* outBuf, size_t outSize) {
    outBuf[0] = '\0';
    if (strlen(gCfg.ddayDate) != 10) return;
    int y = 0, m = 0, d = 0;
    if (sscanf(gCfg.ddayDate, "%d-%d-%d", &y, &m, &d) != 3) return;

    struct tm targetTm = {};
    targetTm.tm_year = y - 1900;
    targetTm.tm_mon  = m - 1;
    targetTm.tm_mday = d;
    targetTm.tm_hour = 12;

    struct tm baseTm = nowTm;
    baseTm.tm_hour = 12;
    baseTm.tm_min  = 0;
    baseTm.tm_sec  = 0;

    time_t tTarget = mktime(&targetTm);
    time_t tBase   = mktime(&baseTm);
    long diffDays  = (long)((tTarget - tBase) / 86400L);

    char dPart[16];
    if (diffDays == 0) strcpy(dPart, "D-DAY");
    else if (diffDays > 0) snprintf(dPart, sizeof(dPart), "D-%ld", diffDays);
    else snprintf(dPart, sizeof(dPart), "D+%ld", -diffDays);

    if (gCfg.ddayTitle[0]) {
        snprintf(outBuf, outSize, "%s %s", gCfg.ddayTitle, dPart);
    } else {
        snprintf(outBuf, outSize, "%s", dPart);
    }
}

// ============================================================================
// Full 240x240 Flicker-Free Strip Renderer
// ============================================================================
static void renderClockScreen() {
    const ThemePalette& th = getActiveTheme();

    time_t nowEpoch = time(nullptr);
    struct tm timeinfo;
    localtime_r(&nowEpoch, &timeinfo);
    bool timeValid = (nowEpoch > 1700000000);

    static const char* WEEKDAYS_KO[7] = {"일", "월", "화", "수", "목", "금", "토"};
    char dateStr[32];
    char timeStr[12];
    char secStr[8];
    char ampmStr[6] = "";

    if (timeValid) {
        snprintf(dateStr, sizeof(dateStr), "%d월 %d일 (%s)",
                 timeinfo.tm_mon + 1, timeinfo.tm_mday, WEEKDAYS_KO[timeinfo.tm_wday % 7]);
        int hr = timeinfo.tm_hour;
        if (gCfg.use12h) {
            strcpy(ampmStr, (hr < 12) ? "AM" : "PM");
            hr = hr % 12;
            if (hr == 0) hr = 12;
        }
        bool colonOn = ((millis() / 500) % 2 == 0) || !gCfg.anim;
        snprintf(timeStr, sizeof(timeStr), "%02d%c%02d", hr, colonOn ? ':' : ' ', timeinfo.tm_min);
        snprintf(secStr, sizeof(secStr), ":%02d", timeinfo.tm_sec);
    } else {
        strcpy(dateStr, WiFi.status() == WL_CONNECTED ? "시간 동기화 중..." : "Wi-Fi 연결 중...");
        strcpy(timeStr, "--:--");
        strcpy(secStr, "");
    }

    char tempStr[16];
    char minMaxStr[24];
    if (gWeather.valid) {
        int tRound = (int)roundf(gWeather.temp);
        snprintf(tempStr, sizeof(tempStr), "%d°", tRound);
        snprintf(minMaxStr, sizeof(minMaxStr), "%d°/%d°",
                 (int)roundf(gWeather.tMin), (int)roundf(gWeather.tMax));
    } else {
        strcpy(tempStr, "--°");
        strcpy(minMaxStr, "날씨 대기");
    }

    char ddayBadge[56];
    if (timeValid) formatDdayBadge(timeinfo, ddayBadge, sizeof(ddayBadge));
    else ddayBadge[0] = '\0';

    bool showWifiHint = gApFallbackActive && (WiFi.status() != WL_CONNECTED);
    bool hasBottomBar = showWifiHint || (ddayBadge[0] != '\0') || (gCfg.customText[0] != '\0') || gHasMsgBmp;

    // Prepare streaming RLE cursors (sequential top-to-bottom across the 8 strips!)
    const SpriteFrameRLE* charTable = CHAR_FRAMES_CHIIKAWA;
    int charW = CHAR_CHIIKAWA_W, charH = CHAR_CHIIKAWA_H;
    if (strcmp(gCfg.character, "hachiware") == 0) {
        charTable = CHAR_FRAMES_HACHIWARE; charW = CHAR_HACHIWARE_W; charH = CHAR_HACHIWARE_H;
    } else if (strcmp(gCfg.character, "usagi") == 0) {
        charTable = CHAR_FRAMES_USAGI; charW = CHAR_USAGI_W; charH = CHAR_USAGI_H;
    } else if (strcmp(gCfg.character, "trio") == 0) {
        charTable = CHAR_FRAMES_TRIO; charW = CHAR_TRIO_W; charH = CHAR_TRIO_H;
    }

    uint8_t frameIdx = gCfg.anim ? (gAnimFrame & 0x03) : 0;
    int charX = (SCREEN_W - charW) / 2;
    int charY = hasBottomBar ? 98 : 108;

    RleStream charStream;
    charStream.init(charTable, frameIdx);

    RleStream weatherStream;
    weatherStream.init(WEATHER_SPRITES, min((uint8_t)4, gWeather.iconIdx));
    const int wxIconX = 152, wxIconY = 16;

    // Pre-measure centered weather labels
    int tempW   = measureText4Bit(FONT_TEMP_21, tempStr);
    int tempX   = 190 - tempW / 2;
    int minMaxW = measureText4Bit(FONT_SUB_10, minMaxStr);
    int minMaxX = 190 - minMaxW / 2;

    // Render 12 strips of 240x20
    for (int stripY0 = 0; stripY0 < SCREEN_H; stripY0 += STRIP_H) {
        // 1. Pastel vertical gradient + polka dots / twinkling night stars
        for (int sy = 0; sy < STRIP_H; ++sy) {
            int y = stripY0 + sy;
            uint8_t r = th.bgTop[0] + ((int)(th.bgBot[0] - th.bgTop[0]) * y) / (SCREEN_H - 1);
            uint8_t g = th.bgTop[1] + ((int)(th.bgBot[1] - th.bgTop[1]) * y) / (SCREEN_H - 1);
            uint8_t b = th.bgTop[2] + ((int)(th.bgBot[2] - th.bgTop[2]) * y) / (SCREEN_H - 1);
            uint16_t rowCol = c565(r, g, b);
            uint16_t* dstRow = &stripBuf[sy * SCREEN_W];
            for (int x = 0; x < SCREEN_W; ++x) dstRow[x] = rowCol;

            if (!th.isNight) {
                int modY = y % 36;
                if (modY >= 16 && modY <= 19) {
                    int rowShift = ((y / 36) & 1) ? 9 : 0;
                    for (int gx = 18 + rowShift; gx < SCREEN_W - 4; gx += 36) {
                        dstRow[gx] = blend565(rowCol, th.cardBorder, 6);
                        dstRow[gx + 1] = blend565(rowCol, th.cardBorder, 6);
                    }
                }
            }
        }

        if (th.isNight) {
            static const uint8_t STARS[8][2] = {
                {24, 21}, {65, 14}, {170, 18}, {216, 34},
                {28, 120}, {213, 112}, {19, 180}, {221, 172}
            };
            for (int i = 0; i < 8; ++i) {
                int sx = STARS[i][0], sy = STARS[i][1];
                bool tw = ((frameIdx + i) & 1) == 0;
                uint16_t scol = tw ? c565(255, 242, 165) : c565(180, 195, 240);
                putStripPx(sx, sy, stripY0, scol);
                if (tw) {
                    putStripPx(sx - 1, sy, stripY0, scol, 10);
                    putStripPx(sx + 1, sy, stripY0, scol, 10);
                    putStripPx(sx, sy - 1, stripY0, scol, 10);
                    putStripPx(sx, sy + 1, stripY0, scol, 10);
                }
            }
        }

        // 2. Top Dashboard Rounded Card [8, 7, 231, 91]
        uint16_t cardFill = c565(th.cardBg[0], th.cardBg[1], th.cardBg[2]);
        drawRoundedCardStrip(stripY0, 8, 7, 231, 91, 14, cardFill, th.cardAlpha, th.cardBorder, true);

        // Vertical divider at x = 147, y = 17..82
        if (82 >= stripY0 && 17 < stripY0 + STRIP_H) {
            for (int y = max(17, stripY0); y <= min(82, stripY0 + STRIP_H - 1); ++y) {
                putStripPx(147, y, stripY0, th.cardBorder, 13);
            }
        }

        // Date + AM/PM + Big Clock + Optional Seconds
        drawText4BitStrip(stripY0, FONT_UI_13, dateStr, 18, 14, th.textSub);
        if (ampmStr[0]) {
            drawText4BitStrip(stripY0, FONT_SUB_10, ampmStr, 118, 16, th.accent);
        }
        // Subtle green/pink NTP indicator dot at top-left of card
        if (gNtpSynced) {
            putStripPx(138, 20, stripY0, c565(64, 204, 128));
            putStripPx(139, 20, stripY0, c565(64, 204, 128));
            putStripPx(138, 21, stripY0, c565(64, 204, 128));
            putStripPx(139, 21, stripY0, c565(64, 204, 128));
        }

        // Always draw crisp HH and MM, and fade/blink ':' smoothly so digits never shift width!
        char hhStr[3] = { timeStr[0], timeStr[1], 0 };
        char mmStr[3] = { timeStr[3], timeStr[4], 0 };
        drawText4BitStrip(stripY0, FONT_TIME_40, hhStr, 16, 33, th.textMain);
        uint8_t colonAlpha = (timeStr[2] == ':') ? 15 : 4;
        drawText4BitStrip(stripY0, FONT_TIME_40, ":", 66, 31, th.accent, colonAlpha);
        drawText4BitStrip(stripY0, FONT_TIME_40, mmStr, 78, 33, th.textMain);

        if (gCfg.showSec && secStr[0]) {
            drawText4BitStrip(stripY0, FONT_SUB_10, secStr, 125, 62, th.textSub);
        }

        // Weather icon + City + Temp + Min/Max
        blitRleStreamStrip(weatherStream, stripY0, wxIconX, wxIconY, WEATHER_ICON_W, WEATHER_ICON_H);
        drawText4BitStrip(stripY0, FONT_UI_13, gCfg.city, 188, 22, th.textSub);
        drawText4BitStrip(stripY0, FONT_TEMP_21, tempStr, tempX, 47, th.accent);
        drawText4BitStrip(stripY0, FONT_SUB_10, minMaxStr, minMaxX, 74, th.textSub);

        // 3. Floating Sparkles + Animated Chiikawa Character
        static const uint8_t SPARKLES[5][2] = {
            {26, 114}, {214, 112}, {22, 178}, {218, 176}, {120, 100}
        };
        for (int i = 0; i < 5; ++i) {
            int bob = (((frameIdx + i) & 1) == 0) ? -3 : 3;
            int sx = SPARKLES[i][0];
            int sy = SPARKLES[i][1] + bob;
            if (((frameIdx + i) & 1) == 0) {
                for (int d = -4; d <= 4; ++d) {
                    putStripPx(sx + d, sy, stripY0, th.accent);
                    putStripPx(sx, sy + d, stripY0, th.accent);
                }
            } else {
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        putStripPx(sx + dx, sy + dy, stripY0, th.cardBorder);
            }
        }

        blitRleStreamStrip(charStream, stripY0, charX, charY, charW, charH);

        // 4. Bottom Speech Bubble / D-Day / Wi-Fi Setup Hint [10, 209, 229, 234]
        if (hasBottomBar) {
            drawRoundedCardStrip(stripY0, 10, 209, 229, 234, 11, th.msgCardBg, 15, th.cardBorder, true);

            if (showWifiHint) {
                const char* hint = "설정 Wi-Fi: Chiikawa-Clock";
                int hw = measureText4Bit(FONT_UI_13, hint);
                drawText4BitStrip(stripY0, FONT_UI_13, hint, (SCREEN_W - hw) / 2, 214, th.accent);
            } else if (gHasMsgBmp) {
                // Blit client-rasterized 196x18 4-bit alpha Hangul message bitmap
                int bx0 = (SCREEN_W - MSG_BMP_W) / 2;
                int by0 = 213;
                if (by0 + MSG_BMP_H > stripY0 && by0 < stripY0 + STRIP_H) {
                    int rStart = max(0, stripY0 - by0);
                    int rEnd   = min(MSG_BMP_H - 1, stripY0 + STRIP_H - 1 - by0);
                    for (int row = rStart; row <= rEnd; ++row) {
                        uint16_t* dstRow = &stripBuf[(by0 + row - stripY0) * SCREEN_W];
                        int pBase = row * MSG_BMP_W;
                        for (int col = 0; col < MSG_BMP_W; ++col) {
                            int pIdx = pBase + col;
                            uint8_t pk = gMsgBmp[pIdx >> 1];
                            uint8_t nib = (pIdx & 1) ? (pk & 0x0F) : (pk >> 4);
                            if (nib) dstRow[bx0 + col] = blend565(dstRow[bx0 + col], th.textMain, nib);
                        }
                    }
                }
            } else {
                int textLeft = 20;
                if (ddayBadge[0]) {
                    int bw = measureText4Bit(FONT_SUB_10, ddayBadge) + 14;
                    drawRoundedCardStrip(stripY0, 15, 213, 15 + bw, 230, 8, th.badgeBg, 15, th.badgeBg, false);
                    drawText4BitStrip(stripY0, FONT_SUB_10, ddayBadge, 22, 216, 0xFFFF);
                    textLeft = 15 + bw + 7;
                }
                if (gCfg.customText[0]) {
                    int cw = measureText4Bit(FONT_UI_13, gCfg.customText);
                    int tx = ddayBadge[0] ? textLeft : max(18, (SCREEN_W - cw) / 2);
                    drawText4BitStrip(stripY0, FONT_UI_13, gCfg.customText, tx, 214, th.textMain);
                }
            }
        }

        // Push 240x30 strip over 40MHz SPI!
        tft.pushImage(0, stripY0, SCREEN_W, STRIP_H, stripBuf);
        yield();
    }
}

// ============================================================================
// Backlight PWM Control (GPIO 5)
// ============================================================================
static void applyBacklight() {
    uint8_t targetPct = constrain(gCfg.brightness, (uint8_t)1, (uint8_t)100);
    if (gCfg.nightDim) {
        time_t nowEpoch = time(nullptr);
        if (nowEpoch > 1700000000) {
            struct tm ti;
            localtime_r(&nowEpoch, &ti);
            if (ti.tm_hour >= 0 && ti.tm_hour < 7) {
                targetPct = min(targetPct, (uint8_t)15);
            }
        }
    }
    int pwm = (targetPct * 255) / 100;
    if (gCfg.blInvert) pwm = 255 - pwm;
    analogWrite(PIN_BACKLIGHT, constrain(pwm, 0, 255));
}

static void applyDisplayHardwareSettings() {
    tft.setRotation(gCfg.rotation & 0x03);
    tft.invertDisplay(gCfg.invertDisp);
    applyBacklight();
    gForceRedraw = true;
}

// ============================================================================
// LittleFS Configuration Load / Save (Lightweight Zero-Dependency JSON Parser)
// ============================================================================
static void setDefaultConfig() {
    memset(&gCfg, 0, sizeof(gCfg));
    strcpy(gCfg.character, "chiikawa");
    strcpy(gCfg.theme, "pink");
    gCfg.brightness = 85;
    gCfg.nightDim   = false;
    gCfg.anim       = true;
    gCfg.use12h     = false;
    gCfg.showSec    = true;
    strcpy(gCfg.ntpServer, "time.google.com");
    strcpy(gCfg.city, "서울");
    gCfg.lat = 37.5665f;
    gCfg.lon = 126.9780f;
    strcpy(gCfg.customText, "오늘도 화이팅! ♡");
    gCfg.ddayTitle[0] = '\0';
    gCfg.ddayDate[0]  = '\0';
    gCfg.rotation   = 0;
    gCfg.invertDisp = true;
    gCfg.blInvert   = true;
}

static bool extractJsonStr(const String& json, const char* key, char* dst, size_t maxLen) {
    String pat = String("\"") + key + "\":";
    int idx = json.indexOf(pat);
    if (idx < 0) return false;
    int q1 = json.indexOf('"', idx + pat.length());
    if (q1 < 0) return false;
    String val;
    for (int i = q1 + 1; i < (int)json.length(); ++i) {
        char c = json[i];
        if (c == '\\' && i + 1 < (int)json.length()) {
            val += json[++i];
        } else if (c == '"') {
            break;
        } else {
            val += c;
        }
    }
    strncpy(dst, val.c_str(), maxLen - 1);
    dst[maxLen - 1] = '\0';
    return true;
}

static bool extractJsonFloat(const String& json, const char* key, float& outVal) {
    String pat = String("\"") + key + "\":";
    int idx = json.indexOf(pat);
    if (idx < 0) return false;
    int p = idx + pat.length();
    while (p < (int)json.length() && (json[p] == ' ' || json[p] == '"')) ++p;
    outVal = json.substring(p).toFloat();
    return true;
}

static bool extractJsonInt(const String& json, const char* key, int& outVal) {
    String pat = String("\"") + key + "\":";
    int idx = json.indexOf(pat);
    if (idx < 0) return false;
    int p = idx + pat.length();
    while (p < (int)json.length() && (json[p] == ' ' || json[p] == '"')) ++p;
    if (json.substring(p).startsWith("true")) { outVal = 1; return true; }
    if (json.substring(p).startsWith("false")) { outVal = 0; return true; }
    outVal = json.substring(p).toInt();
    return true;
}

static String escapeJson(const char* s) {
    String out;
    while (*s) {
        if (*s == '"' || *s == '\\') out += '\\';
        out += *s++;
    }
    return out;
}

static void saveConfigToFS() {
    File f = LittleFS.open("/chiikawa_cfg.json", "w");
    if (!f) return;
    String j = "{";
    j += "\"character\":\"" + escapeJson(gCfg.character) + "\",";
    j += "\"theme\":\"" + escapeJson(gCfg.theme) + "\",";
    j += "\"brightness\":" + String(gCfg.brightness) + ",";
    j += "\"nightDim\":" + String(gCfg.nightDim ? 1 : 0) + ",";
    j += "\"anim\":" + String(gCfg.anim ? 1 : 0) + ",";
    j += "\"use12h\":" + String(gCfg.use12h ? 1 : 0) + ",";
    j += "\"showSec\":" + String(gCfg.showSec ? 1 : 0) + ",";
    j += "\"ntpServer\":\"" + escapeJson(gCfg.ntpServer) + "\",";
    j += "\"city\":\"" + escapeJson(gCfg.city) + "\",";
    j += "\"lat\":" + String(gCfg.lat, 4) + ",";
    j += "\"lon\":" + String(gCfg.lon, 4) + ",";
    j += "\"customText\":\"" + escapeJson(gCfg.customText) + "\",";
    j += "\"ddayTitle\":\"" + escapeJson(gCfg.ddayTitle) + "\",";
    j += "\"ddayDate\":\"" + escapeJson(gCfg.ddayDate) + "\",";
    j += "\"rotation\":" + String(gCfg.rotation) + ",";
    j += "\"invertDisp\":" + String(gCfg.invertDisp ? 1 : 0) + ",";
    j += "\"blInvert\":" + String(gCfg.blInvert ? 1 : 0);
    j += "}";
    f.print(j);
    f.close();
}

static void applyJsonToConfig(const String& json) {
    extractJsonStr(json, "character", gCfg.character, sizeof(gCfg.character));
    extractJsonStr(json, "theme", gCfg.theme, sizeof(gCfg.theme));
    int iv = 0;
    if (extractJsonInt(json, "brightness", iv)) gCfg.brightness = constrain(iv, 1, 100);
    if (extractJsonInt(json, "nightDim", iv))   gCfg.nightDim = (iv != 0);
    if (extractJsonInt(json, "anim", iv))       gCfg.anim = (iv != 0);
    if (extractJsonInt(json, "use12h", iv))     gCfg.use12h = (iv != 0);
    if (extractJsonInt(json, "showSec", iv))    gCfg.showSec = (iv != 0);
    extractJsonStr(json, "ntpServer", gCfg.ntpServer, sizeof(gCfg.ntpServer));
    extractJsonStr(json, "city", gCfg.city, sizeof(gCfg.city));
    extractJsonFloat(json, "lat", gCfg.lat);
    extractJsonFloat(json, "lon", gCfg.lon);
    extractJsonStr(json, "customText", gCfg.customText, sizeof(gCfg.customText));
    extractJsonStr(json, "ddayTitle", gCfg.ddayTitle, sizeof(gCfg.ddayTitle));
    extractJsonStr(json, "ddayDate", gCfg.ddayDate, sizeof(gCfg.ddayDate));
    if (extractJsonInt(json, "rotation", iv))   gCfg.rotation = iv & 0x03;
    if (extractJsonInt(json, "invertDisp", iv)) gCfg.invertDisp = (iv != 0);
    if (extractJsonInt(json, "blInvert", iv))   gCfg.blInvert = (iv != 0);
}

static void loadConfigFromFS() {
    setDefaultConfig();
    if (LittleFS.exists("/chiikawa_cfg.json")) {
        File f = LittleFS.open("/chiikawa_cfg.json", "r");
        if (f) {
            String json = f.readString();
            f.close();
            applyJsonToConfig(json);
        }
    }

    if (LittleFS.exists("/chiikawa_msg.bin")) {
        File mf = LittleFS.open("/chiikawa_msg.bin", "r");
        if (mf && mf.size() == MSG_BMP_BYTES) {
            mf.read(gMsgBmp, MSG_BMP_BYTES);
            gHasMsgBmp = true;
        }
        if (mf) mf.close();
    }
}

// ============================================================================
// NTP Time Sync (time.google.com + HTTP Date Fallback) & Open-Meteo Weather
// ============================================================================
static void configureNtpSync() {
    const char* primary = (gCfg.ntpServer[0] != '\0') ? gCfg.ntpServer : "time.google.com";
    configTime(TZ_Asia_Seoul, primary, "time.apple.com", "kr.pool.ntp.org");
}

// Fallback parser for HTTP "Date: Wed, 11 Nov 2026 04:45:12 GMT" if router blocks UDP 123
static void syncTimeFromHttpDateHeader(const String& dateHdr) {
    if (gNtpSynced || dateHdr.length() < 20) return;
    char wk[8] = {}, monStr[8] = {};
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
    if (sscanf(dateHdr.c_str(), "%7s %d %7s %d %d:%d:%d", wk, &day, monStr, &year, &hh, &mm, &ss) != 7) return;
    static const char* MONTHS = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char* mp = strstr(MONTHS, monStr);
    if (!mp || year < 2025) return;
    int mon = (int)(mp - MONTHS) / 3;

    // Convert UTC broken-down date to Unix epoch (days since 1970-01-01)
    auto daysFromCivil = [](int y, unsigned m, unsigned d) -> long {
        y -= m <= 2;
        const int era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = (unsigned)(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097L + (long)doe - 719468L;
    };
    long days = daysFromCivil(year, (unsigned)(mon + 1), (unsigned)day);
    time_t utcEpoch = (time_t)(days * 86400L + hh * 3600L + mm * 60L + ss);
    struct timeval tv = { utcEpoch, 0 };
    settimeofday(&tv, nullptr);
    gNtpSynced = true;
    gLastNtpEpoch = utcEpoch;
}

static uint8_t wmoToIconIdx(int wmo) {
    if (wmo == 0) return 0;                        // sunny
    if (wmo == 1 || wmo == 2) return 1;            // partly_cloudy
    if (wmo == 3 || wmo == 45 || wmo == 48) return 2; // cloudy
    if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) return 4; // snow
    if (wmo >= 51) return 3;                       // drizzle / rain / storm
    return 1;
}

static void fetchLiveWeather() {
    if (WiFi.status() != WL_CONNECTED) return;
    gWeather.lastFetchMs = millis();

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(4500);
    http.useHTTP10(true);

    String url = String("http://api.open-meteo.com/v1/forecast?latitude=") +
                 String(gCfg.lat, 4) + "&longitude=" + String(gCfg.lon, 4) +
                 "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min"
                 "&timezone=Asia%2FSeoul&forecast_days=1";

    const char* headerKeys[] = { "Date" };
    http.collectHeaders(headerKeys, 1);

    if (http.begin(client, url)) {
        int code = http.GET();
        if (http.hasHeader("Date")) {
            syncTimeFromHttpDateHeader(http.header("Date"));
        }
        if (code == 200) {
            String body = http.getString();
            int curPos = body.indexOf("\"current\":{");
            if (curPos >= 0) {
                String curSub = body.substring(curPos);
                float t = gWeather.temp;
                int wmo = 0;
                if (extractJsonFloat(curSub, "temperature_2m", t)) gWeather.temp = t;
                if (extractJsonInt(curSub, "weather_code", wmo)) gWeather.iconIdx = wmoToIconIdx(wmo);
                gWeather.valid = true;
            }
            int dailyPos = body.indexOf("\"daily\":{");
            if (dailyPos >= 0) {
                int maxIdx = body.indexOf("\"temperature_2m_max\":[", dailyPos);
                if (maxIdx >= 0) gWeather.tMax = body.substring(maxIdx + 22).toFloat();
                int minIdx = body.indexOf("\"temperature_2m_min\":[", dailyPos);
                if (minIdx >= 0) gWeather.tMin = body.substring(minIdx + 22).toFloat();
            }
            gForceRedraw = true;
        }
        http.end();
    }
}

// ============================================================================
// Embedded Web Server + OTA Recovery + GeekMagic Compatible API
// ============================================================================
static void addCors() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

static void setupWebServer() {
    // Ensure GPIO0 (TFT_DC) & GPIO2 (TFT_RST) are driven HIGH right before any OTA soft-reboot
    // so ESP8266 ROM bootloader always boots cleanly into flash mode without needing USB replug!
    Update.onProgress([](size_t progress, size_t total) {
        if (total > 0 && progress >= total) {
            digitalWrite(0, HIGH);
            digitalWrite(2, HIGH);
        }
    });

    // 1. Wireless OTA Firmware Update (/update) — 100% compatible with stock SmallTV-Ultra!
    httpUpdater.setup(&server, "/update");

    // 2. Built-in Korean Web Dashboard (http://<ip>/)
    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html; charset=utf-8", WEB_INDEX_HTML);
    });

    // 3. Live JSON Status
    server.on("/api/status", HTTP_GET, []() {
        addCors();
        time_t nowEpoch = time(nullptr);
        struct tm ti;
        localtime_r(&nowEpoch, &ti);
        char tbuf[48];
        if (nowEpoch > 1700000000) {
            snprintf(tbuf, sizeof(tbuf), "%04d-%02d-%02d %02d:%02d:%02d KST",
                     (ti.tm_year + 1900) % 10000, (ti.tm_mon + 1) % 100, ti.tm_mday % 100,
                     ti.tm_hour % 100, ti.tm_min % 100, ti.tm_sec % 100);
        } else {
            strcpy(tbuf, "동기화 대기 중");
        }
        String ipStr = (WiFi.status() == WL_CONNECTED)
                       ? WiFi.localIP().toString()
                       : WiFi.softAPIP().toString();

        String j = "{";
        j += "\"time\":\"" + String(tbuf) + "\",";
        j += "\"ntpSynced\":" + String(gNtpSynced ? "true" : "false") + ",";
        j += "\"ntpServer\":\"" + escapeJson(gCfg.ntpServer) + "\",";
        j += "\"ip\":\"" + ipStr + "\",";
        j += "\"character\":\"" + escapeJson(gCfg.character) + "\",";
        j += "\"theme\":\"" + escapeJson(gCfg.theme) + "\",";
        j += "\"brightness\":" + String(gCfg.brightness) + ",";
        j += "\"nightDim\":" + String(gCfg.nightDim ? "true" : "false") + ",";
        j += "\"anim\":" + String(gCfg.anim ? "true" : "false") + ",";
        j += "\"use12h\":" + String(gCfg.use12h ? "true" : "false") + ",";
        j += "\"showSec\":" + String(gCfg.showSec ? "true" : "false") + ",";
        j += "\"city\":\"" + escapeJson(gCfg.city) + "\",";
        j += "\"lat\":" + String(gCfg.lat, 4) + ",";
        j += "\"lon\":" + String(gCfg.lon, 4) + ",";
        j += "\"temp\":" + String(gWeather.temp, 1) + ",";
        j += "\"customText\":\"" + escapeJson(gCfg.customText) + "\",";
        j += "\"ddayTitle\":\"" + escapeJson(gCfg.ddayTitle) + "\",";
        j += "\"ddayDate\":\"" + escapeJson(gCfg.ddayDate) + "\",";
        j += "\"rotation\":" + String(gCfg.rotation) + ",";
        j += "\"invertDisp\":" + String(gCfg.invertDisp ? "true" : "false") + ",";
        j += "\"freeHeap\":" + String(ESP.getFreeHeap());
        j += "}";
        server.send(200, "application/json; charset=utf-8", j);
    });

    // 4. Save Full Config
    server.on("/api/config", HTTP_POST, []() {
        addCors();
        float oldLat = gCfg.lat, oldLon = gCfg.lon;
        String oldNtp = String(gCfg.ntpServer);

        applyJsonToConfig(server.arg("plain"));
        saveConfigToFS();
        applyDisplayHardwareSettings();

        if (oldNtp != String(gCfg.ntpServer)) {
            configureNtpSync();
        }
        if (fabsf(oldLat - gCfg.lat) > 0.01f || fabsf(oldLon - gCfg.lon) > 0.01f) {
            gWeather.lastFetchMs = 0; // Trigger immediate weather refresh in loop()
        }
        gForceRedraw = true;
        server.send(200, "application/json", "{\"ok\":true}");
    });

    // 5. Save Client-Rasterized 4-Bit Hangul Bottom Text Bitmap
    server.on("/api/msg_bmp", HTTP_POST, []() {
        addCors();
        String hex = server.arg("plain");
        hex.trim();
        if (hex.length() == MSG_BMP_BYTES * 2) {
            auto hexNib = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            for (int i = 0; i < MSG_BMP_BYTES; ++i) {
                gMsgBmp[i] = (hexNib(hex[i * 2]) << 4) | hexNib(hex[i * 2 + 1]);
            }
            gHasMsgBmp = true;
            File mf = LittleFS.open("/chiikawa_msg.bin", "w");
            if (mf) { mf.write(gMsgBmp, MSG_BMP_BYTES); mf.close(); }
        } else {
            gHasMsgBmp = false;
            LittleFS.remove("/chiikawa_msg.bin");
        }
        gForceRedraw = true;
        server.send(200, "application/json", "{\"ok\":true}");
    });

    // 6. Immediate NTP & Weather Sync Triggers
    server.on("/api/sync_ntp", HTTP_POST, []() {
        addCors();
        configureNtpSync();
        server.send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/sync_weather", HTTP_POST, []() {
        addCors();
        gWeather.lastFetchMs = 0;
        server.send(200, "application/json", "{\"ok\":true}");
    });

    // 7. Save New Wi-Fi Credentials
    server.on("/api/wifi", HTTP_POST, []() {
        addCors();
        String body = server.arg("plain");
        char ssid[64] = {}, pass[64] = {};
        extractJsonStr(body, "ssid", ssid, sizeof(ssid));
        extractJsonStr(body, "pass", pass, sizeof(pass));
        if (ssid[0] == '\0') {
            server.send(400, "application/json", "{\"ok\":false}");
            return;
        }
        server.send(200, "application/json", "{\"ok\":true}");
        delay(250);
        // Save to /wifi.json too so stock SmallTV-Ultra & Chiikawa firmware share Wi-Fi seamlessly!
        File wf = LittleFS.open("/wifi.json", "w");
        if (wf) {
            wf.print(String("{\"a\":\"") + escapeJson(ssid) + "\", \"p\":\"" + escapeJson(pass) + "\"}");
            wf.close();
        }
        WiFi.persistent(true);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pass);
        WiFi.persistent(false);
    });

    // 8. Stock GeekMagic SmallTV-Ultra Compatible Endpoints (for Studio / Auto-Discovery)
    server.on("/v.json", HTTP_GET, []() {
        addCors();
        server.send(200, "application/json",
                    "{\"m\":\"SmallTV-Ultra\",\"v\":\"Chiikawa-Standalone-V1.0\",\"standalone\":true}");
    });

    server.on("/space.json", HTTP_GET, []() {
        addCors();
        FSInfo info;
        LittleFS.info(info);
        String j = "{\"total\":" + String(info.totalBytes) +
                   ",\"free\":" + String(info.totalBytes - info.usedBytes) + "}";
        server.send(200, "application/json", j);
    });

    server.on("/brt.json", HTTP_GET, []() {
        addCors();
        server.send(200, "application/json", "{\"brt\":\"" + String(gCfg.brightness) + "\"}");
    });

    server.on("/set", HTTP_GET, []() {
        addCors();
        if (server.hasArg("brt")) {
            int b = constrain(server.arg("brt").toInt(), 1, 100);
            gCfg.brightness = b;
            applyBacklight();
            saveConfigToFS();
        }
        if (server.hasArg("char")) {
            strncpy(gCfg.character, server.arg("char").c_str(), sizeof(gCfg.character) - 1);
            saveConfigToFS();
            gForceRedraw = true;
        }
        if (server.hasArg("theme")) {
            strncpy(gCfg.theme, server.arg("theme").c_str(), sizeof(gCfg.theme) - 1);
            saveConfigToFS();
            gForceRedraw = true;
        }
        server.send(200, "text/plain", "OK");
    });

    server.onNotFound([]() {
        if (server.method() == HTTP_OPTIONS) {
            addCors();
            server.send(204);
            return;
        }
        // Captive portal redirect when in AP setup mode
        if (gApFallbackActive && WiFi.status() != WL_CONNECTED) {
            server.sendHeader("Location", "http://192.168.4.1/", true);
            server.send(302, "text/plain", "");
            return;
        }
        server.send(404, "text/plain", "Not Found");
    });

    server.begin();
}

// ============================================================================
// Setup & Main Loop
// ============================================================================
void setup() {
    // 1. Backlight PWM setup (avoid full flash during boot)
    analogWriteRange(255);
    analogWriteFreq(1000);
    pinMode(PIN_BACKLIGHT, OUTPUT);
    analogWrite(PIN_BACKLIGHT, 40); // ~84% bright on active-LOW

    // 2. Mount LittleFS (auto-migrating /wifi.json from stock GeekMagic SPIFFS on first boot!)
    if (!LittleFS.begin()) {
        char migSsid[64] = {}, migPass[64] = {};
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        if (SPIFFS.begin()) {
            if (SPIFFS.exists("/wifi.json")) {
                File swf = SPIFFS.open("/wifi.json", "r");
                if (swf) {
                    String wj = swf.readString();
                    swf.close();
                    extractJsonStr(wj, "a", migSsid, sizeof(migSsid));
                    extractJsonStr(wj, "p", migPass, sizeof(migPass));
                }
            }
            SPIFFS.end();
        }
        #pragma GCC diagnostic pop

        LittleFS.format();
        LittleFS.begin();
        if (migSsid[0] != '\0') {
            File lwf = LittleFS.open("/wifi.json", "w");
            if (lwf) {
                lwf.print(String("{\"a\":\"") + escapeJson(migSsid) + "\", \"p\":\"" + escapeJson(migPass) + "\"}");
                lwf.close();
            }
        }
    }
    loadConfigFromFS();

    // 3. Initialize 240x240 ST7789 IPS Display (SPI_MODE3 for CS tied to GND!)
    tft.init();
    tft.setSwapBytes(true);
    applyDisplayHardwareSettings();

    // 4. Register SNTP time-sync callback
    settimeofday_cb([]() {
        gNtpSynced = true;
        gLastNtpEpoch = time(nullptr);
        gForceRedraw = true;
    });

    // 5. Render immediate boot frame so the user sees cute Chiikawa right away!
    renderClockScreen();

    // 6. Connect to Stored Wi-Fi (supports BOTH ESP8266 SDK sector AND stock SmallTV-Ultra /wifi.json!)
    WiFi.persistent(false);
    WiFi.hostname("Chiikawa-Clock");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    char savedSsid[64] = {}, savedPass[64] = {};
    if (LittleFS.exists("/wifi.json")) {
        File wf = LittleFS.open("/wifi.json", "r");
        if (wf) {
            String wj = wf.readString();
            wf.close();
            extractJsonStr(wj, "a", savedSsid, sizeof(savedSsid));
            extractJsonStr(wj, "p", savedPass, sizeof(savedPass));
            if (savedSsid[0] == '\0') {
                extractJsonStr(wj, "ssid", savedSsid, sizeof(savedSsid));
                extractJsonStr(wj, "pass", savedPass, sizeof(savedPass));
            }
        }
    }

    if (savedSsid[0] != '\0') {
        WiFi.begin(savedSsid, savedPass);
    } else {
        WiFi.begin();
    }

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 14000) {
        delay(180);
        if (millis() - gAnimTickMs >= 360) {
            gAnimTickMs = millis();
            gAnimFrame = (gAnimFrame + 1) & 0x03;
            renderClockScreen();
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        configureNtpSync();
    } else {
        // Fallback Recovery AP so user can ALWAYS configure Wi-Fi or /update wirelessly!
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("Chiikawa-Clock");
        gApFallbackActive = true;
        gLastWifiRetryMs = millis();
    }

    setupWebServer();

    if (WiFi.status() == WL_CONNECTED) {
        fetchLiveWeather();
    }
    renderClockScreen();
}

void loop() {
    server.handleClient();
    if (WiFi.status() == WL_CONNECTED) {
        if (gApFallbackActive) {
            // Connected to home Wi-Fi! Turn off fallback AP cleanly
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            gApFallbackActive = false;
            configureNtpSync();
            gWeather.lastFetchMs = 0;
        }
    } else if (millis() - gLastWifiRetryMs > 60000UL) {
        gLastWifiRetryMs = millis();
        if (!gApFallbackActive) {
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP("Chiikawa-Clock");
            gApFallbackActive = true;
        }
        // Only scan/reconnect if an SSID actually exists; otherwise keep AP channel rock-solid!
        if (WiFi.SSID().length() > 0 || LittleFS.exists("/wifi.json")) {
            WiFi.reconnect();
        }
    }

    // Check if system clock became valid via lwIP SNTP
    if (!gNtpSynced && time(nullptr) > 1700000000) {
        gNtpSynced = true;
        gForceRedraw = true;
    }

    // Periodic Open-Meteo Weather Fetch (every 15 minutes, or immediately when invalidated)
    if (WiFi.status() == WL_CONNECTED) {
        unsigned long interval = gWeather.valid ? 900000UL : 20000UL;
        if (gWeather.lastFetchMs == 0 || (millis() - gWeather.lastFetchMs >= interval)) {
            fetchLiveWeather();
        }
    }

    // Smooth 360ms Animation + Exact Second Rollover Redraw
    static time_t lastDrawnSec = 0;
    time_t curSec = time(nullptr);
    unsigned long nowMs = millis();

    bool needDraw = gForceRedraw;
    if (gCfg.anim && (nowMs - gAnimTickMs >= 360)) {
        gAnimTickMs = nowMs;
        gAnimFrame = (gAnimFrame + 1) & 0x03;
        needDraw = true;
    } else if (!gCfg.anim && curSec != lastDrawnSec) {
        needDraw = true;
    }

    if (needDraw) {
        gForceRedraw = false;
        lastDrawnSec = curSec;
        applyBacklight();
        renderClockScreen();
    }
}
