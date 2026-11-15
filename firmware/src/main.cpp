#include <Arduino.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_sntp.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <esp_chip_info.h>
#else
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#include <TZ.h>
#include <coredecls.h>
#endif
#include <FS.h>
#include <LittleFS.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino_GFX_Library.h>
#else
#include <TFT_eSPI.h>
#endif
#include <sys/time.h>
#include <time.h>
#if defined(GEEKCLOCK_SMALLTV_PRO)
#include "touch_button.h"
#include "touch_gestures.h"
#include "pet_game.h"
#include "pomodoro.h"
#include "room_decor.h"
#include "exchange_rate.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32C2) && defined(GEEKCLOCK_XTAL_MHZ)
static_assert(CONFIG_XTAL_FREQ == GEEKCLOCK_XTAL_MHZ,
              "ESP32-C2 SDK crystal frequency does not match this board");
#endif

#include "chiikawa_assets.h"
#if defined(GEEKCLOCK_SMALLTV_PRO)
#include "pet_assets.h"
#include "study_assets.h"
#include "exchange_assets.h"
#include "mario_assets.h"
#include "mario_title_assets.h"
#endif
#if defined(ARDUINO_ARCH_ESP32)
#include "web_ui_gzip.h"
#else
#include "web_ui.h"
#endif

// ============================================================================
// Hardware Configuration (SmallTV-Ultra, ESP32-C2 SDC2, or ESP32 SmallTV-Pro)
// ============================================================================
#ifndef GEEKCLOCK_BACKLIGHT_PIN
#define GEEKCLOCK_BACKLIGHT_PIN 5
#endif
#ifndef GEEKCLOCK_BACKLIGHT_INVERT
#define GEEKCLOCK_BACKLIGHT_INVERT 1
#endif
constexpr uint8_t PIN_BACKLIGHT = GEEKCLOCK_BACKLIGHT_PIN;
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 240;
constexpr int STRIP_H  = 20;          // 240x20x2 = 9,600 bytes static RAM buffer (12 strips, zero flicker!)

#if defined(ARDUINO_ARCH_ESP32)
static Arduino_ESP32SPI displayBus(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
// ips=false makes invertDisplay(true) send INVON, matching TFT_eSPI's semantics.
static Arduino_ST7789 tft(&displayBus, TFT_RST, 0, false, SCREEN_W, SCREEN_H);
#else
static TFT_eSPI tft = TFT_eSPI();
#endif
static uint16_t stripBuf[SCREEN_W * STRIP_H];

#if defined(ARDUINO_ARCH_ESP32)
static WebServer server(80);
static bool gOtaSucceeded = false;
static String gOtaError;
static unsigned long gOtaRebootAtMs = 0;
#else
static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer httpUpdater;
#endif
static bool gFsMounted = false;
#if defined(CONFIG_IDF_TARGET_ESP32C2)
static constexpr const char* FIRMWARE_VERSION = "Chiikawa-Standalone-V1.1-ESP32C2";
static constexpr const char* DEVICE_MODEL = "SDC2 (ESP32-C2)";
#elif defined(GEEKCLOCK_SMALLTV_PRO)
static constexpr const char* FIRMWARE_VERSION = "Chiikawa-Standalone-V1.8-SmallTV-PRO";
static constexpr const char* DEVICE_MODEL = "GeekMagic SmallTV-PRO";
#elif defined(ARDUINO_ARCH_ESP32)
static constexpr const char* FIRMWARE_VERSION = "Chiikawa-Standalone-V1.1-ESP32";
static constexpr const char* DEVICE_MODEL = "ESP32";
#else
static constexpr const char* FIRMWARE_VERSION = "Chiikawa-Standalone-V1.0";
static constexpr const char* DEVICE_MODEL = "SmallTV-Ultra";
#endif

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
#if defined(GEEKCLOCK_SMALLTV_PRO)
static TouchButton gTouchButton;
static TouchGestures gTouchGestures;
static PetGame gPetGame;
static PomodoroTimer gPomodoro;
static RoomDecor gRoom;
static uint16_t gStudyRewardCoins = 0;
static uint32_t gStudyRewardAtMs = 0;
static uint32_t gStudyLastSaveMs = 0;
static bool gStudyStorageOk = true;
static bool gStudyDirty = false;
enum class ScreenMode : uint8_t { Clock, Pet, Exchange, Mario, Pomodoro };
static ScreenMode gScreenMode = ScreenMode::Clock;
static ExchangeRateClient gExchange;
struct ExchangeView {
    bool valid = false;
    bool loading = false;
    bool stale = false;
    double krwPerJpy = 0;
    char rateDate[11] = {};
    char fetchedAt[32] = {};
    char error[72] = {};
};
static char gExchangeDiagnostic[72] = {};
static ExchangeView gExchangeView;
static uint32_t gExchangeRevision = UINT32_MAX;
static uint32_t gPetLastSaveMs = 0;
static const char* gLastTouchGesture = "none";
struct MarioClockView {
    int hour = 0, minute = 0, second = 0;
    int year = 0, month = 0, day = 0;
    bool timeValid = false, use12h = false, showSec = false;
    uint32_t nowMs = 0, jumpStartedMs = 0;
    bool jumpActive = false;
};
static MarioClockView gMarioView;

static void updateMarioView() {
    gMarioView.nowMs = millis();
    gMarioView.use12h = gCfg.use12h;
    gMarioView.showSec = gCfg.showSec;
    const time_t now = time(nullptr);
    gMarioView.timeValid = now > 1700000000;
    if (gMarioView.timeValid) {
        struct tm local;
        localtime_r(&now, &local);
        gMarioView.hour = local.tm_hour;
        gMarioView.minute = local.tm_min;
        gMarioView.second = local.tm_sec;
        gMarioView.year = local.tm_year + 1900;
        gMarioView.month = local.tm_mon + 1;
        gMarioView.day = local.tm_mday;
    }
    if (gMarioView.jumpActive && uint32_t(gMarioView.nowMs - gMarioView.jumpStartedMs) >= 900)
        gMarioView.jumpActive = false;
}

static void triggerMarioJump() {
    gMarioView.jumpStartedMs = millis();
    gMarioView.jumpActive = true;
    gForceRedraw = true;
}
#endif

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
#if defined(ARDUINO_ARCH_ESP32)
        tft.draw16bitRGBBitmap(0, stripY0, stripBuf, SCREEN_W, STRIP_H);
#else
        tft.pushImage(0, stripY0, SCREEN_W, STRIP_H, stripBuf);
#endif
        yield();
    }
}

// ============================================================================
#if defined(GEEKCLOCK_SMALLTV_PRO)
#include "room_renderer.h"
#include "pet_renderer.h"
#include "study_renderer.h"
#include "exchange_renderer.h"
#include "mario_renderer.h"
#endif

static void renderCurrentScreen() {
#if defined(GEEKCLOCK_SMALLTV_PRO)
    if (gScreenMode == ScreenMode::Pomodoro) { renderPomodoroScreen(); return; }
    if (gScreenMode == ScreenMode::Mario) { updateMarioView(); renderMarioScreen(); return; }
    if (gScreenMode == ScreenMode::Exchange) { renderExchangeScreen(); return; }
    if (gScreenMode == ScreenMode::Pet) { renderPetScreen(); return; }
#endif
    renderClockScreen();
}

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
    gCfg.blInvert   = GEEKCLOCK_BACKLIGHT_INVERT != 0;
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
#if defined(ARDUINO_ARCH_ESP32)
    // A dedicated namespace keeps settings persistent without formatting stock FS.
    if (!gFsMounted) {
        Preferences prefs;
        if (prefs.begin("chiikawa", false)) {
            prefs.putString("config", j);
            prefs.end();
        }
        return;
    }
#endif
    File f = LittleFS.open("/chiikawa_cfg.json", "w");
    if (!f) return;
    f.print(j);
    f.close();
}

#if defined(GEEKCLOCK_SMALLTV_PRO)
static void savePetState() {
    Preferences prefs;
    if (prefs.begin("chiipet", false)) {
        prefs.putUChar("full", gPetGame.data.fullness);
        prefs.putUChar("happy", gPetGame.data.happiness);
        prefs.putUChar("energy", gPetGame.data.energy);
        prefs.putUShort("xp", gPetGame.data.xp);
        prefs.putUInt("care", gPetGame.data.careCount);
        prefs.putBool("active", gPetGame.active);
        prefs.end();
    }
    gPetLastSaveMs = millis();
}

static void loadPetState() {
    Preferences prefs;
    if (prefs.begin("chiipet", true)) {
        gPetGame.data.fullness = min((uint8_t)100, prefs.getUChar("full", 80));
        gPetGame.data.happiness = min((uint8_t)100, prefs.getUChar("happy", 80));
        gPetGame.data.energy = min((uint8_t)100, prefs.getUChar("energy", 80));
        gPetGame.data.xp = prefs.getUShort("xp", 0);
        gPetGame.data.careCount = prefs.getUInt("care", 0);
        gPetGame.active = prefs.getBool("active", false);
        prefs.end();
    }
    gPetGame.sanitize();
    gPetGame.tick(millis());
    gPetLastSaveMs = millis();
}

// Integer fields for the study/shop API must be actual complete JSON numbers.
static bool studyJsonInt(const String& json, const char* key, int& value) {
    const String needle = String("\"") + key + "\"";
    int at = json.indexOf(needle);
    if (at < 0) return false;
    at += needle.length();
    auto whitespace = [&]() {
        while (at < int(json.length()) && (json[at] == ' ' || json[at] == '\t' || json[at] == '\r' || json[at] == '\n')) ++at;
    };
    whitespace();
    if (at >= int(json.length()) || json[at++] != ':') return false;
    whitespace();
    bool negative = at < int(json.length()) && json[at] == '-';
    if (negative) ++at;
    if (at >= int(json.length()) || json[at] < '0' || json[at] > '9') return false;
    if (json[at] == '0' && at + 1 < int(json.length()) && json[at + 1] >= '0' && json[at + 1] <= '9') return false;
    int64_t number = 0;
    while (at < int(json.length()) && json[at] >= '0' && json[at] <= '9') {
        number = number * 10 + json[at++] - '0';
        if (number > 2147483647LL) return false;
    }
    whitespace();
    if (at >= int(json.length()) || (json[at] != ',' && json[at] != '}')) return false;
    value = negative ? -int(number) : int(number);
    return true;
}

static bool saveStudyState() {
    // One NVS record commits the timer transition and coin wallet together.
    String record;
    record.reserve(350);
    record = "{\"schema\":1,\"coins\":" + String(gRoom.data.coins);
    record += ",\"owned\":" + String(gRoom.data.ownedMask);
    record += ",\"wall\":" + String(gRoom.data.wallpaper) + ",\"rug\":" + String(gRoom.data.rug);
    record += ",\"bed\":" + String(gRoom.data.bed) + ",\"decor\":" + String(gRoom.data.decor);
    record += ",\"focus\":" + String(gPomodoro.data.focusMinutes);
    record += ",\"short\":" + String(gPomodoro.data.shortBreakMinutes);
    record += ",\"long\":" + String(gPomodoro.data.longBreakMinutes);
    record += ",\"phase\":" + String(gPomodoro.data.phase) + ",\"cycle\":" + String(gPomodoro.data.cycle);
    record += ",\"remaining\":" + String(gPomodoro.data.remainingMs);
    record += ",\"sessions\":" + String(gPomodoro.data.completedFocus);
    record += ",\"minutes\":" + String(gPomodoro.data.totalFocusMinutes) + "}";
    Preferences prefs;
    bool ok = false;
    if (prefs.begin("chiistudy", false)) {
        ok = prefs.putString("record", record) == record.length();
        prefs.end();
    }
    gStudyStorageOk = ok;
    gStudyDirty = !ok;
    gStudyLastSaveMs = millis();
    return ok;
}

static void loadStudyState() {
    Preferences prefs;
    String record;
    if (prefs.begin("chiistudy", true)) {
        record = prefs.getString("record", "");
        prefs.end();
    }
    RoomData room;
    PomodoroData timer;
    auto read = [&](const char* key, int low, int high, auto& target) {
        int value = 0;
        if (!studyJsonInt(record, key, value) || value < low || value > high) return false;
        target = value;
        return true;
    };
    int schema = 0;
    const bool valid = read("schema", 1, 1, schema) && read("coins", 0, 99999, room.coins) &&
        read("owned", 0, 255, room.ownedMask) && read("wall", 0, 8, room.wallpaper) &&
        read("rug", 0, 8, room.rug) && read("bed", 0, 8, room.bed) && read("decor", 0, 8, room.decor) &&
        read("focus", 1, 90, timer.focusMinutes) && read("short", 1, 30, timer.shortBreakMinutes) &&
        read("long", 1, 60, timer.longBreakMinutes) && read("phase", 0, 2, timer.phase) &&
        read("cycle", 0, 3, timer.cycle) && read("remaining", 1, 5400000, timer.remainingMs) &&
        read("sessions", 0, 1000000000, timer.completedFocus) && read("minutes", 0, 2000000000, timer.totalFocusMinutes);
    if (valid) { gRoom.data = room; gPomodoro.data = timer; }
    gRoom.sanitize();
    gPomodoro.restore();  // Resume paused; powered-off time never earns coins.
    gStudyLastSaveMs = millis();
    if (!valid) saveStudyState();
}

static void settleStudy(uint32_t nowMs) {
    const PomodoroEvent event = gPomodoro.tick(nowMs);
    if (event == PomodoroEvent::None) return;
    if (event == PomodoroEvent::FocusComplete) {
        gStudyRewardCoins = uint16_t(focusRewardCoins(gPomodoro.lastCompletedFocusMinutes()));
        gStudyRewardAtMs = nowMs;
        gRoom.credit(gStudyRewardCoins);
    }
    saveStudyState();
    gForceRedraw = true;
}

static void togglePomodoro(uint32_t nowMs) {
    settleStudy(nowMs);
    if (gPomodoro.data.running) gPomodoro.pause(nowMs);
    else gPomodoro.start(nowMs);
    gStudyRewardCoins = 0;
    settleStudy(nowMs);
    saveStudyState();
    gForceRedraw = true;
}

static String pomodoroStatusJson() {
    settleStudy(millis());
    String j = "{\"phase\":\"" + String(gPomodoro.phaseName()) + "\"";
    j += ",\"running\":" + String(gPomodoro.data.running ? "true" : "false");
    j += ",\"remainingSeconds\":" + String((gPomodoro.data.remainingMs + 999) / 1000);
    j += ",\"focusMinutes\":" + String(gPomodoro.data.focusMinutes);
    j += ",\"shortBreakMinutes\":" + String(gPomodoro.data.shortBreakMinutes);
    j += ",\"longBreakMinutes\":" + String(gPomodoro.data.longBreakMinutes);
    j += ",\"completedFocus\":" + String(gPomodoro.data.completedFocus);
    j += ",\"totalFocusMinutes\":" + String(gPomodoro.data.totalFocusMinutes);
    j += ",\"cycle\":" + String(gPomodoro.data.cycle);
    j += ",\"rewardCoins\":" + String(focusRewardCoins(gPomodoro.data.focusMinutes));
    j += ",\"lastRewardCoins\":" + String(gStudyRewardCoins && uint32_t(millis() - gStudyRewardAtMs) < 8000 ? gStudyRewardCoins : 0);
    j += ",\"storageReady\":" + String(gStudyStorageOk ? "true" : "false") + "}";
    return j;
}

static String roomStatusJson() {
    settleStudy(millis());
    String j;
    j.reserve(1500);
    j = "{\"coins\":" + String(gRoom.data.coins);
    j += ",\"wallpaper\":" + String(gRoom.data.wallpaper) + ",\"rug\":" + String(gRoom.data.rug);
    j += ",\"bed\":" + String(gRoom.data.bed) + ",\"decor\":" + String(gRoom.data.decor);
    j += ",\"items\":[";
    static const char* slots[] = {"wallpaper", "rug", "bed", "decor"};
    const uint8_t equipped[] = {gRoom.data.wallpaper, gRoom.data.rug, gRoom.data.bed, gRoom.data.decor};
    for (size_t i = 0; i < 8; ++i) {
        const RoomItem& item = ROOM_ITEMS[i];
        if (i) j += ',';
        j += "{\"id\":" + String(item.id) + ",\"slot\":\"" + slots[item.slot] + "\"";
        j += ",\"key\":\"" + String(item.key) + "\",\"name\":\"" + escapeJson(item.name) + "\"";
        j += ",\"price\":" + String(item.price);
        j += ",\"owned\":" + String(gRoom.owns(item.id) ? "true" : "false");
        j += ",\"equipped\":" + String(equipped[item.slot] == item.id ? "true" : "false") + '}';
    }
    j += "]}";
    return j;
}

static const char* screenModeName() {
    return gScreenMode == ScreenMode::Pet ? "pet" :
           gScreenMode == ScreenMode::Exchange ? "exchange" :
           gScreenMode == ScreenMode::Mario ? "mario" :
           gScreenMode == ScreenMode::Pomodoro ? "pomodoro" : "clock";
}

static void updateExchangeView() {
    const ExchangeSnapshot state = gExchange.snapshot();
    if (gExchangeRevision == state.revision && gExchangeView.stale == state.stale) return;
    gExchangeRevision = state.revision;
    gExchangeView.valid = state.valid;
    gExchangeView.loading = state.loading;
    gExchangeView.stale = state.stale;
    gExchangeView.krwPerJpy = state.krwPerJpy;
    strlcpy(gExchangeView.rateDate, state.rateDate, sizeof(gExchangeView.rateDate));
    strlcpy(gExchangeDiagnostic, state.error, sizeof(gExchangeDiagnostic));
    const char* error = state.error[0] ? "환율 조회 실패 · 연결을 확인해 주세요." : "";
    if (strcmp(state.error, "Waiting for Wi-Fi") == 0) error = "Wi-Fi 연결을 기다리고 있어요.";
    else if (strcmp(state.error, "Waiting for clock synchronization") == 0) error = "시간 동기화를 기다리고 있어요.";
    strlcpy(gExchangeView.error, error, sizeof(gExchangeView.error));
    gExchangeView.fetchedAt[0] = '\0';
    if (state.fetchedEpoch > 1700000000UL) {
        // Format KST independently of SNTP/TZ initialization during boot.
        time_t kstEpoch = time_t(state.fetchedEpoch) + 9 * 3600;
        struct tm kst;
        gmtime_r(&kstEpoch, &kst);
        strftime(gExchangeView.fetchedAt, sizeof(gExchangeView.fetchedAt), "%m/%d %H:%M KST", &kst);
    }
    if (gScreenMode == ScreenMode::Exchange) gForceRedraw = true;
}

static void loadScreenState() {
    gScreenMode = gPetGame.active ? ScreenMode::Pet : ScreenMode::Clock;
    Preferences prefs;
    if (prefs.begin("chiiscreen", true)) {
        uint8_t mode = prefs.getUChar("mode", uint8_t(gScreenMode));
        if (mode <= uint8_t(ScreenMode::Pomodoro)) gScreenMode = ScreenMode(mode);
        prefs.end();
    }
    gPetGame.active = gScreenMode == ScreenMode::Pet;
    gExchange.begin();
    updateExchangeView();
}

static void setScreenMode(ScreenMode mode) {
    if (gScreenMode == mode) return;
    gScreenMode = mode;
    gPetGame.active = mode == ScreenMode::Pet;
    savePetState();
    Preferences prefs;
    if (prefs.begin("chiiscreen", false)) {
        prefs.putUChar("mode", uint8_t(mode));
        prefs.end();
    }
    if (mode == ScreenMode::Exchange) gExchange.request(false);
    updateExchangeView();
    gForceRedraw = true;
}

static void setPetMode(bool active) {
    setScreenMode(active ? ScreenMode::Pet : ScreenMode::Clock);
}

static String exchangeStatusJson() {
    updateExchangeView();
    String j = "{\"valid\":" + String(gExchangeView.valid ? "true" : "false");
    j += ",\"loading\":" + String(gExchangeView.loading ? "true" : "false");
    j += ",\"stale\":" + String(gExchangeView.stale ? "true" : "false");
    j += ",\"krwPerJpy\":" + (gExchangeView.valid ? String(gExchangeView.krwPerJpy, 6) : String("null"));
    j += ",\"krwPer100Jpy\":" + (gExchangeView.valid ? String(gExchangeView.krwPerJpy * 100.0, 2) : String("null"));
    j += ",\"rateDate\":\"" + String(gExchangeView.rateDate) + "\"";
    j += ",\"fetchedAt\":\"" + String(gExchangeView.fetchedAt) + "\"";
    j += ",\"error\":\"" + escapeJson(gExchangeView.error) + "\"";
    j += ",\"diagnostic\":\"" + escapeJson(gExchangeDiagnostic) + "\"";
    j += ",\"source\":\"ECB via Frankfurter\",\"referenceOnly\":true}";
    return j;
}

static void advanceClockCharacter() {
    static const char* const characters[] = {"chiikawa", "hachiware", "usagi", "trio"};
    size_t next = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (strcmp(gCfg.character, characters[i]) == 0) {
            next = (i + 1) % 4;
            break;
        }
    }
    strcpy(gCfg.character, characters[next]);
    gAnimFrame = 0;
    gForceRedraw = true;
    saveConfigToFS();
}

static void serviceTouchButton() {
    static uint32_t lastSampleMs = 0;
    const uint32_t nowMs = millis();
    if (uint32_t(nowMs - lastSampleMs) < 20) return;
    lastSampleMs = nowMs;

    const uint16_t a = touchRead(GEEKCLOCK_TOUCH_PIN);
    const uint16_t b = touchRead(GEEKCLOCK_TOUCH_PIN);
    const uint16_t c = touchRead(GEEKCLOCK_TOUCH_PIN);
    const uint16_t raw = max(min(a, b), min(max(a, b), c));
    gTouchButton.update(raw, nowMs);
    const Gesture gesture = gTouchGestures.update(gTouchButton.pressed(), nowMs);
    if (gesture == Gesture::Double) {
        gLastTouchGesture = "double";
        setScreenMode(ScreenMode((uint8_t(gScreenMode) + 1) % 5));
    } else if (gesture == Gesture::Single) {
        gLastTouchGesture = "single";
        if (gScreenMode == ScreenMode::Pomodoro) {
            togglePomodoro(nowMs);
        } else if (gPetGame.active) {
            if (gPetGame.play(nowMs)) { savePetState(); gForceRedraw = true; }
        } else if (gScreenMode == ScreenMode::Mario) {
            triggerMarioJump();
        } else if (gScreenMode == ScreenMode::Exchange) {
            gExchange.request(true);
            updateExchangeView();
        } else {
            advanceClockCharacter();
        }
    } else if (gesture == Gesture::Long) {
        gLastTouchGesture = "long";
        if (gScreenMode == ScreenMode::Pomodoro) {
            settleStudy(nowMs);
            gPomodoro.reset(nowMs);
            gStudyRewardCoins = 0;
            saveStudyState();
            gForceRedraw = true;
        } else if (gScreenMode == ScreenMode::Exchange || gScreenMode == ScreenMode::Mario) {
            setScreenMode(ScreenMode::Clock);
        } else if (gPetGame.active && gPetGame.feed(nowMs)) {
            savePetState();
            gForceRedraw = true;
        }
    }
}

static String marioStatusJson() {
    updateMarioView();
    String j = "{\"timeValid\":" + String(gMarioView.timeValid ? "true" : "false");
    j += ",\"hour\":" + String(gMarioView.hour) + ",\"minute\":" + String(gMarioView.minute);
    j += ",\"second\":" + String(gMarioView.second);
    j += ",\"year\":" + String(gMarioView.year) + ",\"month\":" + String(gMarioView.month);
    j += ",\"day\":" + String(gMarioView.day);
    j += ",\"showSec\":" + String(gMarioView.showSec ? "true" : "false");
    j += ",\"jumping\":" + String(gMarioView.jumpActive ? "true" : "false") + "}";
    return j;
}

static String petStatusJson() {
    String j = "{\"active\":" + String(gPetGame.active ? "true" : "false");
    j += ",\"fullness\":" + String(gPetGame.data.fullness);
    j += ",\"happiness\":" + String(gPetGame.data.happiness);
    j += ",\"energy\":" + String(gPetGame.data.energy);
    j += ",\"level\":" + String(gPetGame.level());
    j += ",\"xp\":" + String(gPetGame.data.xp);
    j += ",\"careCount\":" + String(gPetGame.data.careCount);
    j += ",\"activity\":\"" + String(gPetGame.activityName()) + "\"";
    j += ",\"x\":" + String(gPetGame.x) + ",\"y\":" + String(gPetGame.y) + "}";
    return j;
}

static String hardwareStatusJson() {
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    const char* flashMode = "Unknown";
    switch (ESP.getFlashChipMode()) {
        case FM_QIO: flashMode = "QIO"; break;
        case FM_QOUT: flashMode = "QOUT"; break;
        case FM_DIO: flashMode = "DIO"; break;
        case FM_DOUT: flashMode = "DOUT"; break;
        case FM_FAST_READ: flashMode = "FAST_READ"; break;
        case FM_SLOW_READ: flashMode = "SLOW_READ"; break;
        default: break;
    }
    String j;
    j.reserve(1200);
    j = "{\"model\":\"" + String(DEVICE_MODEL) + "\"";
    j += ",\"chipModel\":\"" + String(ESP.getChipModel()) + "\"";
    j += ",\"chipRevision\":" + String(chip.revision);
    j += ",\"chipCores\":" + String(chip.cores);
    j += ",\"cpuMHz\":" + String(ESP.getCpuFreqMHz());
    j += ",\"xtalMHz\":" + String(getXtalFrequencyMhz());
    j += ",\"flashBytes\":" + String(ESP.getFlashChipSize());
    // Read runtime clock registers rather than the stock bootloader's header.
    j += ",\"flashSpeedHz\":" + String(ESP.getFlashFrequencyMHz() * 1000000UL);
    j += ",\"flashMode\":\"" + String(flashMode) + "\"";
    j += ",\"heapTotalBytes\":" + String(ESP.getHeapSize());
    j += ",\"heapFreeBytes\":" + String(ESP.getFreeHeap());
    j += ",\"heapMinFreeBytes\":" + String(ESP.getMinFreeHeap());
    j += ",\"heapMaxAllocBytes\":" + String(ESP.getMaxAllocHeap());
    j += ",\"psramBytes\":" + String(ESP.getPsramSize());
    j += ",\"psramFreeBytes\":" + String(ESP.getFreePsram());
    j += ",\"sketchBytes\":" + String(ESP.getSketchSize());
    j += ",\"otaSlotBytes\":" + String(next ? next->size : 0);
    j += ",\"runningPartition\":\"" + String(running ? running->label : "") + "\"";
    j += ",\"otaPartition\":\"" + String(next ? next->label : "") + "\"";
    j += ",\"chipWifiBGN\":" + String(chip.features & CHIP_FEATURE_WIFI_BGN ? "true" : "false");
    j += ",\"chipBluetoothClassic\":" + String(chip.features & CHIP_FEATURE_BT ? "true" : "false");
    j += ",\"chipBLE\":" + String(chip.features & CHIP_FEATURE_BLE ? "true" : "false");
    // This firmware never starts a Bluetooth controller or profile.
    j += ",\"bluetoothFirmwareEnabled\":false";
    // These are the selected board profile, not values probed from the panel.
    j += ",\"board\":{\"displayController\":\"ST7789\",\"displayWidth\":" + String(SCREEN_W);
    j += ",\"displayHeight\":" + String(SCREEN_H) + ",\"displayDiagonalInches\":1.54";
    j += ",\"displaySpiHz\":" + String(SPI_FREQUENCY);
    j += ",\"pins\":{\"mosi\":" + String(TFT_MOSI) + ",\"sclk\":" + String(TFT_SCLK);
    j += ",\"dc\":" + String(TFT_DC) + ",\"rst\":" + String(TFT_RST) + ",\"cs\":" + String(TFT_CS);
    j += ",\"backlight\":" + String(PIN_BACKLIGHT) + ",\"touch\":" + String(GEEKCLOCK_TOUCH_PIN) + "}";
    j += ",\"backlightActiveLow\":" + String(GEEKCLOCK_BACKLIGHT_INVERT ? "true" : "false");
    j += "}}";
    return j;
}

#endif

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
#if defined(ARDUINO_ARCH_ESP32)
    if (!gFsMounted) {
        Preferences prefs;
        if (prefs.begin("chiikawa", true)) {
            applyJsonToConfig(prefs.getString("config", "{}"));
            if (prefs.getBytesLength("message") == MSG_BMP_BYTES) {
                gHasMsgBmp = prefs.getBytes("message", gMsgBmp, MSG_BMP_BYTES) == MSG_BMP_BYTES;
            }
            prefs.end();
        }
        return;
    }
#endif
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
#if defined(ARDUINO_ARCH_ESP32)
    configTzTime("KST-9", primary, "time.apple.com", "kr.pool.ntp.org");
#else
    configTime(TZ_Asia_Seoul, primary, "time.apple.com", "kr.pool.ntp.org");
#endif
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

#if defined(ARDUINO_ARCH_ESP32)
static void handleOtaUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        gOtaSucceeded = false;
        gOtaError = "";
        if (gOtaRebootAtMs != 0) {
            gOtaError = "Firmware restart already pending";
            return;
        }
        // Update writes only the inactive OTA app partition, preserving NVS and files.
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            gOtaError = Update.errorString();
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (gOtaError.length() == 0 &&
            Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            gOtaError = Update.errorString();
            Update.abort();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (gOtaError.length() != 0) return;
        if (upload.totalSize == 0) {
            gOtaError = "Empty firmware upload";
            Update.abort();
        } else if (!Update.end(true)) {
            gOtaError = Update.errorString();
        } else {
            gOtaSucceeded = true;
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        gOtaSucceeded = false;
        gOtaError = "Firmware upload aborted";
    }
}

static void finishOtaUpload() {
    addCors();
    server.sendHeader("Connection", "close");
    if (!gOtaSucceeded || gOtaError.length() != 0) {
        String error = gOtaError.length() ? gOtaError : String("No firmware file received");
        server.send(400, "text/plain; charset=utf-8", "Update failed: " + error);
        return;
    }
    server.send(200, "text/plain; charset=utf-8", "OK. Restarting...");
    // Let WebServer finish its response before restarting, including stock AJAX clients.
    gOtaRebootAtMs = millis() + 1000;
}

static void setupEsp32Ota() {
    auto page = []() {
        server.send(200, "text/html; charset=utf-8",
            "<!doctype html><html lang='ko'><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Chiikawa Firmware Update</title><h1>펌웨어 업데이트</h1>"
            "<p>이 기기 모델에 맞는 앱 펌웨어(.bin)를 선택하세요.</p>"
            "<form method='POST' action='/update_ota' enctype='multipart/form-data'>"
            "<input type='file' name='update' accept='.bin' required>"
            "<button type='submit'>업데이트</button></form></html>");
    };
    server.on("/update", HTTP_GET, page);
    server.on("/update_ota", HTTP_GET, page);
    server.on("/update", HTTP_POST, finishOtaUpload, handleOtaUpload);
    server.on("/update_ota", HTTP_POST, finishOtaUpload, handleOtaUpload);
}
#endif

static void setupWebServer() {
#if defined(ARDUINO_ARCH_ESP32)
    setupEsp32Ota();
#else
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
#endif

    // 2. Built-in Korean Web Dashboard (http://<ip>/)
    server.on("/", HTTP_GET, []() {
#if defined(ARDUINO_ARCH_ESP32)
        server.sendHeader("Content-Encoding", "gzip");
        server.send_P(200, "text/html; charset=utf-8",
                      reinterpret_cast<const char*>(WEB_INDEX_GZIP), sizeof(WEB_INDEX_GZIP));
#else
        server.send_P(200, "text/html; charset=utf-8", WEB_INDEX_HTML);
#endif
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
        j += "\"filesystemReady\":" + String(gFsMounted ? "true" : "false") + ",";
#if defined(GEEKCLOCK_SMALLTV_PRO)
        j += "\"touchReady\":" + String(gTouchButton.ready() ? "true" : "false") + ",";
        j += "\"touchPressed\":" + String(gTouchButton.pressed() ? "true" : "false") + ",";
        j += "\"touchRaw\":" + String(gTouchButton.raw()) + ",";
        j += "\"touchBaseline\":" + String(gTouchButton.baseline()) + ",";
        j += "\"touchPressCount\":" + String(gTouchButton.pressCount()) + ",";
        j += "\"lastTouchGesture\":\"" + String(gLastTouchGesture) + "\",";
        j += "\"pet\":" + petStatusJson() + ",";
        j += "\"hardware\":" + hardwareStatusJson() + ",";
        j += "\"screenMode\":\"" + String(screenModeName()) + "\",";
        j += "\"exchange\":" + exchangeStatusJson() + ",";
        j += "\"mario\":" + marioStatusJson() + ",";
        j += "\"pomodoro\":" + pomodoroStatusJson() + ",";
        j += "\"room\":" + roomStatusJson() + ",";
#endif
#if defined(ARDUINO_ARCH_ESP32)
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
        j += "\"version\":\"" + String(FIRMWARE_VERSION) + "\",";
        j += "\"chipModel\":\"" + String(ESP.getChipModel()) + "\",";
        j += "\"chipRevision\":" + String(ESP.getChipRevision()) + ",";
        j += "\"flashSize\":" + String(ESP.getFlashChipSize()) + ",";
        j += "\"sketchSize\":" + String(ESP.getSketchSize()) + ",";
        j += "\"runningPartition\":\"" + String(running ? running->label : "") + "\",";
        j += "\"otaPartition\":\"" + String(next ? next->label : "") + "\",";
        j += "\"otaSlotBytes\":" + String(next ? next->size : 0) + ",";
#endif
        j += "\"freeHeap\":" + String(ESP.getFreeHeap());
        j += "}";
        server.send(200, "application/json; charset=utf-8", j);
    });

#if defined(GEEKCLOCK_SMALLTV_PRO)
    server.on("/api/pomodoro", HTTP_GET, []() {
        addCors();
        server.send(200, "application/json", pomodoroStatusJson());
    });
    server.on("/api/pomodoro", HTTP_POST, []() {
        addCors();
        const uint32_t nowMs = millis();
        settleStudy(nowMs);
        const PomodoroData before = gPomodoro.data;
        const String body = server.arg("plain");
        char action[20] = {};
        extractJsonStr(body, "action", action, sizeof(action));
        bool accepted = false;
        if (strcmp(action, "start") == 0) accepted = gPomodoro.start(nowMs);
        else if (strcmp(action, "pause") == 0) accepted = gPomodoro.pause(nowMs);
        else if (strcmp(action, "reset") == 0) accepted = gPomodoro.reset(nowMs);
        else if (strcmp(action, "skip") == 0) accepted = gPomodoro.skip(nowMs);
        else if (strcmp(action, "configure") == 0) {
            int focus = 0, shortBreak = 0, longBreak = 0;
            if (!studyJsonInt(body, "focusMinutes", focus) || !studyJsonInt(body, "shortBreakMinutes", shortBreak) ||
                !studyJsonInt(body, "longBreakMinutes", longBreak) || focus < 1 || focus > 90 ||
                shortBreak < 1 || shortBreak > 30 || longBreak < 1 || longBreak > 60) {
                server.send(400, "application/json", "{\"error\":\"집중 1~90분, 짧은 휴식 1~30분, 긴 휴식 1~60분으로 입력해 주세요.\"}");
                return;
            }
            accepted = gPomodoro.configure(focus, shortBreak, longBreak, nowMs);
        } else {
            server.send(400, "application/json", "{\"error\":\"알 수 없는 타이머 동작입니다.\"}");
            return;
        }
        if (!accepted) {
            server.send(409, "application/json", "{\"error\":\"현재 상태에서는 변경할 수 없어요. 진행 중이라면 먼저 일시정지해 주세요.\"}");
            return;
        }
        gStudyRewardCoins = 0;
        settleStudy(nowMs);
        if (!saveStudyState()) {
            gPomodoro.data = before;
            gPomodoro.restore();
            if (before.running) gPomodoro.start(nowMs);
            server.send(503, "application/json", "{\"error\":\"저장하지 못해 타이머 변경을 취소했어요. 다시 시도해 주세요.\"}");
            return;
        }
        gForceRedraw = true;
        server.send(200, "application/json", pomodoroStatusJson());
    });
    server.on("/api/room", HTTP_GET, []() {
        addCors();
        server.send(200, "application/json", roomStatusJson());
    });
    server.on("/api/room", HTTP_POST, []() {
        addCors();
        settleStudy(millis());
        const RoomData before = gRoom.data;
        const String body = server.arg("plain");
        char action[20] = {};
        extractJsonStr(body, "action", action, sizeof(action));
        RoomResult result = RoomResult::InvalidItem;
        int value = 0;
        if (strcmp(action, "buy") == 0 || strcmp(action, "equip") == 0) {
            if (studyJsonInt(body, "id", value) && value >= 1 && value <= 8)
                result = strcmp(action, "buy") == 0 ? gRoom.buy(uint8_t(value)) : gRoom.equip(uint8_t(value));
        } else if (strcmp(action, "unequip") == 0) {
            if (studyJsonInt(body, "slot", value) && value >= 0 && value <= 3)
                result = gRoom.unequip(uint8_t(value));
        }
        if (result != RoomResult::Ok) {
            const char* error = result == RoomResult::AlreadyOwned ? "이미 보유한 아이템이에요. 적용 버튼을 눌러 주세요." :
                                result == RoomResult::InsufficientCoins ? "꾸미기 코인이 부족해요. 집중 시간을 완료해 모아 주세요." :
                                result == RoomResult::NotOwned ? "먼저 구매해야 적용할 수 있어요." : "아이템 또는 슬롯을 확인해 주세요.";
            server.send(result == RoomResult::InvalidItem ? 400 : 409, "application/json",
                        "{\"error\":\"" + escapeJson(error) + "\"}");
            return;
        }
        if (!saveStudyState()) {
            gRoom.data = before;
            server.send(503, "application/json", "{\"error\":\"저장하지 못해 꾸미기 변경을 취소했어요. 다시 시도해 주세요.\"}");
            return;
        }
        gForceRedraw = true;
        server.send(200, "application/json", roomStatusJson());
    });
    server.on("/api/screen", HTTP_POST, []() {
        addCors();
        char mode[16] = {};
        extractJsonStr(server.arg("plain"), "mode", mode, sizeof(mode));
        if (strcmp(mode, "clock") == 0) setScreenMode(ScreenMode::Clock);
        else if (strcmp(mode, "pet") == 0) setScreenMode(ScreenMode::Pet);
        else if (strcmp(mode, "exchange") == 0) setScreenMode(ScreenMode::Exchange);
        else if (strcmp(mode, "mario") == 0) setScreenMode(ScreenMode::Mario);
        else if (strcmp(mode, "pomodoro") == 0) setScreenMode(ScreenMode::Pomodoro);
        else {
            server.send(400, "application/json", "{\"error\":\"Unknown screen mode\"}");
            return;
        }
        server.send(200, "application/json", "{\"mode\":\"" + String(screenModeName()) + "\"}");
    });
    server.on("/api/mario/jump", HTTP_POST, []() {
        addCors();
        if (gScreenMode != ScreenMode::Mario) {
            server.send(409, "application/json", "{\"error\":\"먼저 마리오 시계 화면을 선택해 주세요.\"}");
            return;
        }
        triggerMarioJump();
        server.send(200, "application/json", "{\"ok\":true}");
    });
    server.on("/api/exchange", HTTP_GET, []() {
        addCors();
        server.send(200, "application/json", exchangeStatusJson());
    });
    server.on("/api/exchange/refresh", HTTP_POST, []() {
        addCors();
        if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) {
            server.send(409, "application/json", "{\"error\":\"Wi-Fi 연결과 시간 동기화를 기다려 주세요.\"}");
            return;
        }
        if (!gExchange.snapshot().loading && !gExchange.request(true)) {
            server.send(429, "application/json", "{\"error\":\"환율 갱신은 30초 간격으로 할 수 있어요.\"}");
            return;
        }
        server.send(202, "application/json", exchangeStatusJson());
    });

    server.on("/api/pet", HTTP_GET, []() {
        addCors();
        server.send(200, "application/json", petStatusJson());
    });
    server.on("/api/pet", HTTP_POST, []() {
        addCors();
        char action[16] = {};
        extractJsonStr(server.arg("plain"), "action", action, sizeof(action));
        if (strcmp(action, "enter") == 0 || strcmp(action, "exit") == 0) {
            setPetMode(strcmp(action, "enter") == 0);
        } else if (strcmp(action, "feed") == 0 || strcmp(action, "play") == 0 ||
                   strcmp(action, "sleep") == 0) {
            if (!gPetGame.active) {
                server.send(409, "application/json", "{\"error\":\"먼저 다마고치 모드로 들어가 주세요.\"}");
                return;
            }
            if (strcmp(action, "play") == 0 &&
                (gPetGame.data.energy < 8 || gPetGame.data.fullness < 4)) {
                server.send(409, "application/json", "{\"error\":\"밥을 먹고 쉬면 다시 놀 수 있어요.\"}");
                return;
            }
            if (strcmp(action, "sleep") == 0 && gPetGame.activity() == PetActivity::Sleeping) {
                server.send(409, "application/json", "{\"error\":\"지금은 쿨쿨 쉬고 있어요.\"}");
                return;
            }
            const uint32_t nowMs = millis();
            bool accepted = strcmp(action, "feed") == 0 ? gPetGame.feed(nowMs) :
                            strcmp(action, "play") == 0 ? gPetGame.play(nowMs) :
                                                         gPetGame.sleep(nowMs);
            if (!accepted) {
                server.send(429, "application/json", "{\"error\":\"Wait a moment before the next action\"}");
                return;
            }
            savePetState();
            gForceRedraw = true;
        } else {
            server.send(400, "application/json", "{\"error\":\"Unknown pet action\"}");
            return;
        }
        server.send(200, "application/json", petStatusJson());
    });
#endif

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
#if defined(ARDUINO_ARCH_ESP32)
            if (!gFsMounted) {
                Preferences prefs;
                if (prefs.begin("chiikawa", false)) {
                    prefs.putBytes("message", gMsgBmp, MSG_BMP_BYTES);
                    prefs.end();
                }
            } else
#endif
            {
                File mf = LittleFS.open("/chiikawa_msg.bin", "w");
                if (mf) { mf.write(gMsgBmp, MSG_BMP_BYTES); mf.close(); }
            }
        } else {
            gHasMsgBmp = false;
#if defined(ARDUINO_ARCH_ESP32)
            if (!gFsMounted) {
                Preferences prefs;
                if (prefs.begin("chiikawa", false)) {
                    prefs.remove("message");
                    prefs.end();
                }
            } else
#endif
            {
                LittleFS.remove("/chiikawa_msg.bin");
            }
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
        if (gFsMounted) {
            File wf = LittleFS.open("/wifi.json", "w");
            if (wf) {
                wf.print(String("{\"a\":\"") + escapeJson(ssid) + "\", \"p\":\"" + escapeJson(pass) + "\"}");
                wf.close();
            }
        }
#if defined(ARDUINO_ARCH_ESP32)
        WiFi.mode(WIFI_STA);
        // persistent() only affects Wi-Fi initialization on ESP32. Select FLASH
        // explicitly so recovery credentials survive when stock FS cannot mount.
        esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        WiFi.begin(ssid, pass);
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
#else
        WiFi.persistent(true);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pass);
        WiFi.persistent(false);
#endif
    });

    // 8. Stock GeekMagic SmallTV-Ultra Compatible Endpoints (for Studio / Auto-Discovery)
    server.on("/v.json", HTTP_GET, []() {
        addCors();
        server.send(200, "application/json",
                    String("{\"m\":\"") + DEVICE_MODEL + "\",\"v\":\"" +
                    FIRMWARE_VERSION + "\",\"standalone\":true}");
    });
#if defined(ARDUINO_ARCH_ESP32)
    server.on("/version", HTTP_GET, []() {
        addCors();
        server.send(200, "text/plain", FIRMWARE_VERSION);
    });
#endif

    server.on("/space.json", HTTP_GET, []() {
        addCors();
        size_t totalBytes = 0, usedBytes = 0;
        if (gFsMounted) {
#if defined(ARDUINO_ARCH_ESP32)
            totalBytes = LittleFS.totalBytes();
            usedBytes = LittleFS.usedBytes();
#else
            FSInfo info;
            if (LittleFS.info(info)) {
                totalBytes = info.totalBytes;
                usedBytes = info.usedBytes;
            }
#endif
        }
        String j = "{\"total\":" + String(totalBytes) +
                   ",\"free\":" + String(totalBytes - min(totalBytes, usedBytes)) + "}";
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
#if defined(GEEKCLOCK_SMALLTV_PRO)
    // Keep restored RTC time in KST even before Wi-Fi/NTP initialization.
    setenv("TZ", "KST-9", 1);
    tzset();
#endif
    // 1. Backlight PWM setup (avoid full flash during boot)
#if !defined(ARDUINO_ARCH_ESP32)
    analogWriteRange(255);
    analogWriteFreq(1000);
#endif
    pinMode(PIN_BACKLIGHT, OUTPUT);
    analogWrite(PIN_BACKLIGHT, GEEKCLOCK_BACKLIGHT_INVERT ? 40 : 215);

#if defined(ARDUINO_ARCH_ESP32)
    // Preserve stock photo/config files if their filesystem is not LittleFS or
    // the stock partition layout differs. Network recovery still works via NVS/AP.
    gFsMounted = LittleFS.begin(false);
#else
    // 2. Mount LittleFS (auto-migrating /wifi.json from stock GeekMagic SPIFFS on first boot!)
    gFsMounted = LittleFS.begin();
    if (!gFsMounted) {
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
        gFsMounted = LittleFS.begin();
        if (migSsid[0] != '\0') {
            File lwf = LittleFS.open("/wifi.json", "w");
            if (lwf) {
                lwf.print(String("{\"a\":\"") + escapeJson(migSsid) + "\", \"p\":\"" + escapeJson(migPass) + "\"}");
                lwf.close();
            }
        }
    }
#endif
    loadConfigFromFS();
#if defined(GEEKCLOCK_SMALLTV_PRO)
    loadPetState();
    loadStudyState();
    loadScreenState();
#endif

    // 3. Initialize 240x240 ST7789 IPS Display (SPI_MODE3 for CS tied to GND!)
#if defined(ARDUINO_ARCH_ESP32)
    // Arduino_ST7789 selects SPI_MODE3 on ESP32, including ESP32-C2.
    tft.begin(SPI_FREQUENCY);
#else
    tft.init();
    tft.setSwapBytes(true);
#endif
    applyDisplayHardwareSettings();

    // 4. Register SNTP time-sync callback
#if defined(ARDUINO_ARCH_ESP32)
    sntp_set_time_sync_notification_cb([](struct timeval*) {
#else
    settimeofday_cb([]() {
#endif
        gNtpSynced = true;
        gLastNtpEpoch = time(nullptr);
        gForceRedraw = true;
    });

    // 5. Render immediate boot frame so the user sees cute Chiikawa right away!
    renderCurrentScreen();

    // 6. Connect to Stored Wi-Fi (supports BOTH ESP8266 SDK sector AND stock SmallTV-Ultra /wifi.json!)
    WiFi.persistent(false);
#if defined(ARDUINO_ARCH_ESP32)
    WiFi.setHostname("Chiikawa-Clock");
#else
    WiFi.hostname("Chiikawa-Clock");
#endif
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    char savedSsid[64] = {}, savedPass[64] = {};
    const char* wifiPaths[] = {"/wifi.json"
#if defined(ARDUINO_ARCH_ESP32)
        , "/config.json", "/.sys/config.json"
#endif
    };
    for (const char* path : wifiPaths) {
        if (!gFsMounted || !LittleFS.exists(path)) continue;
        File wf = LittleFS.open(path, "r");
        if (wf) {
            String wj = wf.readString();
            wf.close();
            extractJsonStr(wj, "a", savedSsid, sizeof(savedSsid));
            extractJsonStr(wj, "p", savedPass, sizeof(savedPass));
            if (savedSsid[0] == '\0') {
                extractJsonStr(wj, "ssid", savedSsid, sizeof(savedSsid));
                extractJsonStr(wj, "pass", savedPass, sizeof(savedPass));
                if (savedPass[0] == '\0') extractJsonStr(wj, "password", savedPass, sizeof(savedPass));
            }
        }
        if (savedSsid[0] != '\0') break;
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
            renderCurrentScreen();
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

    if (WiFi.status() == WL_CONNECTED
#if defined(GEEKCLOCK_SMALLTV_PRO)
        && gScreenMode == ScreenMode::Clock
#endif
    ) {
        fetchLiveWeather();
    }
    renderCurrentScreen();
}

void loop() {
    server.handleClient();
#if defined(ARDUINO_ARCH_ESP32)
    if (gOtaRebootAtMs != 0) {
        if ((int32_t)(millis() - gOtaRebootAtMs) >= 0) ESP.restart();
        delay(1);
        return;
    }
#endif
#if defined(GEEKCLOCK_SMALLTV_PRO)
    settleStudy(millis());
    serviceTouchButton();
    static uint32_t studyFrameMs = 0;
    if (gScreenMode == ScreenMode::Pomodoro && uint32_t(millis() - studyFrameMs) >= 250) {
        studyFrameMs = millis();
        gForceRedraw = true;
    }
    if ((gPomodoro.data.running && uint32_t(millis() - gStudyLastSaveMs) >= 30000) ||
        (gStudyDirty && uint32_t(millis() - gStudyLastSaveMs) >= 5000)) saveStudyState();
    static uint32_t marioFrameMs = 0;
    if (gScreenMode == ScreenMode::Mario && uint32_t(millis() - marioFrameMs) >= 100) {
        marioFrameMs = millis();
        gForceRedraw = true;
    }
    gExchange.service();
    updateExchangeView();
    if (gPetGame.tick(millis()) && gPetGame.active) gForceRedraw = true;
    if (uint32_t(millis() - gPetLastSaveMs) >= 60000UL) savePetState();
#endif
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
        if (WiFi.SSID().length() > 0 || (gFsMounted && LittleFS.exists("/wifi.json"))) {
            WiFi.reconnect();
        }
    }

    // Check if system clock became valid via lwIP SNTP
    if (!gNtpSynced && time(nullptr) > 1700000000) {
        gNtpSynced = true;
        gForceRedraw = true;
    }

    // Periodic Open-Meteo Weather Fetch (every 15 minutes, or immediately when invalidated)
    if (WiFi.status() == WL_CONNECTED
#if defined(GEEKCLOCK_SMALLTV_PRO)
        && gScreenMode == ScreenMode::Clock
#endif
    ) {
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
        renderCurrentScreen();
    }
}
