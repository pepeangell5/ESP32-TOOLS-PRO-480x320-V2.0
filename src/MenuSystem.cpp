#include "MenuSystem.h"
#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"

#include "WifiScanner.h"
#include "WifiChannelScanner.h"
#include "WifiRadar.h"
#include "WifiDirectionFinder.h"
#include "RadioScanner.h"
#include "RadioJammer.h"
#include "SignalTools.h"
#include "CC1101Tools.h"
#include "PacketMonitor.h"
#include "SettingsMenu.h"
#include "SystemInfo.h"
#include "BLEDeviceRadar.h"
#include "BLEInspector.h"
#include "BLEIPhoneRemote.h"
#include "BLESpam.h"
#include "BTDisruptor.h"
#include "BeaconSpam.h"
#include "Deauther.h"
#include "EvilPortal.h"
#include "Screensaver.h"
#include "ProbeSniffer.h"
#include "Karma.h"
#include "ClockWeather.h"
#include "About.h"

#include <string.h>

static constexpr uint16_t MOD_BG     = TFT_BLACK;
static constexpr uint16_t MOD_TEXT   = TFT_WHITE;
static constexpr uint16_t MOD_LINE   = TFT_WHITE;
static constexpr uint16_t MOD_INVERT = TFT_BLACK;
static constexpr uint16_t MOD_GLOW   = 0x07FF;
static constexpr uint16_t MOD_GLOW_2 = 0x03EF;

static constexpr int MENU_TOP = 42;
static constexpr int ROW_H    = 31;
static constexpr int ROW_X    = 8;
static constexpr int ROW_W    = 152;
static constexpr int ROW_BOX_H = 27;
static constexpr int PREVIEW_X = 168;
static constexpr int PREVIEW_Y = 42;
static constexpr int PREVIEW_W = 144;
static constexpr int PREVIEW_H = 156;

static void handlerWifi();
static void handlerRadio();
static void handlerBT();
static void handlerMonitor();
static void handlerSystem();

struct SubMenuCursorMemory {
    const char* title;
    int cursor;
};

static SubMenuCursorMemory submenuCursorMemory[16];

static int findSubMenuCursorSlot(const char* title, bool create) {
    if (!title) return -1;

    int emptySlot = -1;
    for (int i = 0; i < 16; i++) {
        if (!submenuCursorMemory[i].title) {
            if (emptySlot < 0) emptySlot = i;
            continue;
        }
        if (strcmp(submenuCursorMemory[i].title, title) == 0) {
            return i;
        }
    }

    if (create && emptySlot >= 0) {
        submenuCursorMemory[emptySlot].title = title;
        submenuCursorMemory[emptySlot].cursor = 0;
        return emptySlot;
    }

    return -1;
}

static const MainMenuEntry MAIN_ENTRIES[] = {
    { "WIFI",      "Scanner / portal", ICON_WIFI,      handlerWifi    },
    { "RADIO",     "Dual NRF tools",   ICON_RADIO,     handlerRadio   },
    { "BLUETOOTH", "BLE lab modes",    ICON_BLUETOOTH, handlerBT      },
    { "MONITOR",   "Live packets",     ICON_MONITOR,   handlerMonitor },
    { "SYSTEM",    "Device config",    ICON_SYSTEM,    handlerSystem  },
};

static const int MAIN_COUNT = sizeof(MAIN_ENTRIES) / sizeof(MainMenuEntry);
static int currentEntry = 0;

static void drawBitmapCentered(int y, const String& text, uint16_t color,
                               int size, FontType font) {
    int w = getTextWidth(text, size, font);
    int x = (320 - w) / 2;
    if (x < 0) x = 0;

    if (font == FONT_BIG) drawStringBig(x, y, text, color, size);
    else drawStringCustom(x, y, text, color, size);
}

static void drawBitmapCenteredIn(int x0, int width, int y, const String& text,
                                 uint16_t color, int size, FontType font) {
    int w = getTextWidth(text, size, font);
    int x = x0 + (width - w) / 2;
    if (x < x0) x = x0;

    if (font == FONT_BIG) drawStringBig(x, y, text, color, size);
    else drawStringCustom(x, y, text, color, size);
}

static void drawBitmapRight(int xRight, int y, const String& text,
                            uint16_t color, int size, FontType font) {
    int w = getTextWidth(text, size, font);
    int x = xRight - w;

    if (font == FONT_BIG) drawStringBig(x, y, text, color, size);
    else drawStringCustom(x, y, text, color, size);
}

static void drawFrame() {
    tft.fillScreen(MOD_BG);
    tft.drawRect(0, 0, 320, 240, MOD_LINE);
    tft.drawFastHLine(0, 34, 320, MOD_LINE);
    tft.drawFastHLine(0, 206, 320, MOD_LINE);
}

static void drawMainHeader() {
    tft.fillRect(1, 1, 318, 32, MOD_BG);
    drawStringBig(10, 8, "ESP32 TOOLS", MOD_TEXT, 1);
    drawStringCustom(128, 13, "MAIN MENU", MOD_GLOW, 1);

    String count = String(currentEntry + 1) + "/" + String(MAIN_COUNT);
    drawBitmapRight(306, 12, count, MOD_TEXT, 1, FONT_SMALL);

    tft.drawRect(236, 24, 70, 5, MOD_GLOW);
    int fillW = ((currentEntry + 1) * 68) / MAIN_COUNT;
    if (fillW > 0) tft.fillRect(237, 25, fillW, 3, MOD_TEXT);
}

static void drawMainFooter() {
    tft.fillRect(1, 207, 318, 32, MOD_BG);
    drawStringCustom(10, 218, "UP/DN: SELECT", MOD_TEXT, 1);
    drawBitmapRight(310, 218, "OK: OPEN", MOD_TEXT, 1, FONT_SMALL);
}

static void clearMainArea() {
    tft.fillRect(1, 35, 318, 171, MOD_BG);
}

static void drawGlowBox(int x, int y, int w, int h) {
    tft.drawRect(x - 2, y - 2, w + 4, h + 4, MOD_GLOW_2);
    tft.drawRect(x - 1, y - 1, w + 2, h + 2, MOD_GLOW);
    tft.drawRect(x, y, w, h, MOD_TEXT);
}

static void drawWifiIcon(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawRect(cx - 10, cy + 5, 20, 7, color);
    tft.fillCircle(cx - 5, cy + 8, 1, accent);
    tft.fillCircle(cx + 5, cy + 8, 1, accent);

    tft.drawLine(cx - 11, cy - 1, cx - 6, cy - 6, color);
    tft.drawLine(cx - 6, cy - 6, cx + 6, cy - 6, color);
    tft.drawLine(cx + 6, cy - 6, cx + 11, cy - 1, color);
    tft.drawLine(cx - 6, cy + 2, cx - 3, cy - 1, accent);
    tft.drawLine(cx - 3, cy - 1, cx + 3, cy - 1, accent);
    tft.drawLine(cx + 3, cy - 1, cx + 6, cy + 2, accent);
}

static void drawRadioIcon(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawFastHLine(cx - 14, cy + 13, 28, color);
    for (int i = 0; i < 5; i++) {
        int h = 6 + i * 3;
        int x = cx - 13 + i * 6;
        tft.fillRect(x, cy + 12 - h, 3, h, (i == 3) ? accent : color);
    }
    tft.drawCircle(cx + 12, cy - 7, 5, color);
    tft.fillCircle(cx + 12, cy - 7, 2, accent);
}

static void drawBluetoothIcon(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawLine(cx, cy - 12, cx, cy + 12, color);
    tft.drawLine(cx, cy - 12, cx + 9, cy - 4, color);
    tft.drawLine(cx + 9, cy - 4, cx - 7, cy + 9, color);
    tft.drawLine(cx, cy + 12, cx + 9, cy + 4, color);
    tft.drawLine(cx + 9, cy + 4, cx - 7, cy - 9, color);
    tft.fillCircle(cx, cy, 2, accent);
}

static void drawMonitorIcon(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawRect(cx - 15, cy - 12, 30, 20, color);
    tft.drawLine(cx - 11, cy, cx - 5, cy, color);
    tft.drawLine(cx - 5, cy, cx - 2, cy - 6, accent);
    tft.drawLine(cx - 2, cy - 6, cx + 3, cy + 5, accent);
    tft.drawLine(cx + 3, cy + 5, cx + 7, cy - 2, color);
    tft.drawLine(cx + 7, cy - 2, cx + 12, cy - 2, color);
    tft.drawFastVLine(cx, cy + 8, 5, color);
    tft.drawFastHLine(cx - 8, cy + 13, 16, color);
}

static void drawSystemIcon(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawCircle(cx, cy, 8, color);
    tft.drawCircle(cx, cy, 4, accent);
    tft.drawFastHLine(cx - 13, cy, 5, color);
    tft.drawFastHLine(cx + 8, cy, 5, color);
    tft.drawFastVLine(cx, cy - 13, 5, color);
    tft.drawFastVLine(cx, cy + 8, 5, color);
    tft.drawLine(cx - 8, cy - 8, cx - 11, cy - 11, color);
    tft.drawLine(cx + 8, cy - 8, cx + 11, cy - 11, color);
    tft.drawLine(cx - 8, cy + 8, cx - 11, cy + 11, color);
    tft.drawLine(cx + 8, cy + 8, cx + 11, cy + 11, color);
}

static void drawVectorIcon(IconID id, int cx, int cy, uint16_t color,
                           uint16_t accent) {
    tft.drawCircle(cx, cy, 13, color);
    tft.drawCircle(cx, cy, 11, accent);

    switch (id) {
        case ICON_WIFI:      drawWifiIcon(cx, cy, color, accent);      break;
        case ICON_RADIO:     drawRadioIcon(cx, cy, color, accent);     break;
        case ICON_BLUETOOTH: drawBluetoothIcon(cx, cy, color, accent); break;
        case ICON_MONITOR:   drawMonitorIcon(cx, cy, color, accent);   break;
        case ICON_SYSTEM:
        default:             drawSystemIcon(cx, cy, color, accent);    break;
    }
}

static void drawHeroWifi(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawRect(cx - 25, cy + 18, 50, 14, color);
    tft.drawFastHLine(cx - 20, cy + 24, 40, accent);
    tft.fillCircle(cx - 13, cy + 25, 2, accent);
    tft.fillCircle(cx + 13, cy + 25, 2, accent);

    tft.drawLine(cx - 34, cy + 8, cx - 20, cy - 6, color);
    tft.drawLine(cx - 20, cy - 6, cx + 20, cy - 6, color);
    tft.drawLine(cx + 20, cy - 6, cx + 34, cy + 8, color);
    tft.drawLine(cx - 24, cy + 9, cx - 13, cy - 1, accent);
    tft.drawLine(cx - 13, cy - 1, cx + 13, cy - 1, accent);
    tft.drawLine(cx + 13, cy - 1, cx + 24, cy + 9, accent);
}

static void drawHeroRadio(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawFastHLine(cx - 28, cy + 26, 56, color);
    for (int i = 0; i < 7; i++) {
        int h = 10 + (i % 4) * 8;
        int x = cx - 25 + i * 8;
        tft.fillRect(x, cy + 25 - h, 4, h, (i == 4) ? accent : color);
    }
    tft.drawCircle(cx + 27, cy - 18, 10, color);
    tft.drawCircle(cx + 27, cy - 18, 16, accent);
    tft.fillCircle(cx + 27, cy - 18, 3, accent);
}

static void drawHeroBluetooth(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawLine(cx, cy - 34, cx, cy + 34, color);
    tft.drawLine(cx, cy - 34, cx + 26, cy - 12, color);
    tft.drawLine(cx + 26, cy - 12, cx - 20, cy + 24, color);
    tft.drawLine(cx, cy + 34, cx + 26, cy + 12, color);
    tft.drawLine(cx + 26, cy + 12, cx - 20, cy - 24, color);
    tft.fillCircle(cx, cy, 5, accent);
    tft.drawCircle(cx, cy, 10, accent);
}

static void drawHeroMonitor(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawRect(cx - 36, cy - 26, 72, 48, color);
    tft.drawRect(cx - 31, cy - 21, 62, 36, color);
    tft.drawLine(cx - 26, cy, cx - 14, cy, color);
    tft.drawLine(cx - 14, cy, cx - 8, cy - 14, accent);
    tft.drawLine(cx - 8, cy - 14, cx + 4, cy + 12, accent);
    tft.drawLine(cx + 4, cy + 12, cx + 13, cy - 7, color);
    tft.drawLine(cx + 13, cy - 7, cx + 26, cy - 7, color);
    tft.drawFastVLine(cx, cy + 22, 10, color);
    tft.drawFastHLine(cx - 18, cy + 32, 36, color);
}

static void drawHeroSystem(int cx, int cy, uint16_t color, uint16_t accent) {
    tft.drawCircle(cx, cy, 25, color);
    tft.drawCircle(cx, cy, 12, accent);
    tft.drawFastHLine(cx - 42, cy, 17, color);
    tft.drawFastHLine(cx + 25, cy, 17, color);
    tft.drawFastVLine(cx, cy - 42, 17, color);
    tft.drawFastVLine(cx, cy + 25, 17, color);
    tft.drawLine(cx - 26, cy - 26, cx - 36, cy - 36, color);
    tft.drawLine(cx + 26, cy - 26, cx + 36, cy - 36, color);
    tft.drawLine(cx - 26, cy + 26, cx - 36, cy + 36, color);
    tft.drawLine(cx + 26, cy + 26, cx + 36, cy + 36, color);
    tft.fillCircle(cx, cy, 4, accent);
}

static void drawHeroIcon(IconID id, int cx, int cy, uint16_t color,
                         uint16_t accent) {
    tft.drawCircle(cx, cy, 43, MOD_GLOW_2);
    tft.drawCircle(cx, cy, 40, MOD_GLOW);
    tft.drawCircle(cx, cy, 36, MOD_TEXT);

    switch (id) {
        case ICON_WIFI:      drawHeroWifi(cx, cy, color, accent);      break;
        case ICON_RADIO:     drawHeroRadio(cx, cy, color, accent);     break;
        case ICON_BLUETOOTH: drawHeroBluetooth(cx, cy, color, accent); break;
        case ICON_MONITOR:   drawHeroMonitor(cx, cy, color, accent);   break;
        case ICON_SYSTEM:
        default:             drawHeroSystem(cx, cy, color, accent);    break;
    }
}

static void drawMainRow(int idx, bool selected, bool pressed) {
    if (idx < 0 || idx >= MAIN_COUNT) return;

    const MainMenuEntry& e = MAIN_ENTRIES[idx];
    int y = MENU_TOP + idx * ROW_H;
    uint16_t bg = pressed ? MOD_TEXT : MOD_BG;
    uint16_t fg = pressed ? MOD_INVERT : MOD_TEXT;
    uint16_t accent = selected ? MOD_GLOW : MOD_TEXT;

    tft.fillRect(ROW_X - 3, y - 3, ROW_W + 6, ROW_BOX_H + 6, MOD_BG);
    tft.fillRect(ROW_X, y, ROW_W, ROW_BOX_H, bg);

    if (selected) {
        drawGlowBox(ROW_X, y, ROW_W, ROW_BOX_H);
    } else {
        tft.drawRect(ROW_X, y, ROW_W, ROW_BOX_H, MOD_LINE);
    }

    drawVectorIcon(e.icon, 26, y + 14, fg, accent);
    drawStringBig(48, y + 4, e.title, fg, 1);
    drawStringCustom(48, y + 19, e.subtitle, fg, 1);

    String idxText = "0" + String(idx + 1);
    drawBitmapRight(151, y + 10, idxText, fg, 1, FONT_SMALL);
}

static void drawPreviewPanel(bool pressed) {
    if (currentEntry < 0 || currentEntry >= MAIN_COUNT) return;
    const MainMenuEntry& e = MAIN_ENTRIES[currentEntry];
    uint16_t panelBg = pressed ? MOD_TEXT : MOD_BG;
    uint16_t fg = pressed ? MOD_INVERT : MOD_TEXT;
    uint16_t accent = pressed ? MOD_INVERT : MOD_GLOW;

    tft.fillRect(PREVIEW_X - 3, PREVIEW_Y - 3,
                 PREVIEW_W + 6, PREVIEW_H + 6, MOD_BG);
    tft.fillRect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, panelBg);
    drawGlowBox(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H);

    drawHeroIcon(e.icon, PREVIEW_X + PREVIEW_W / 2, PREVIEW_Y + 56, fg, accent);
    drawBitmapCenteredIn(PREVIEW_X, PREVIEW_W, PREVIEW_Y + 112,
                         e.title, fg, 1, FONT_BIG);
    drawBitmapCenteredIn(PREVIEW_X, PREVIEW_W, PREVIEW_Y + 132,
                         e.subtitle, fg, 1, FONT_SMALL);
    drawBitmapCenteredIn(PREVIEW_X, PREVIEW_W, PREVIEW_Y + 146,
                         "OK TO OPEN", fg, 1, FONT_SMALL);
}

static void drawAllMainRows() {
    tft.fillRect(1, 35, 164, 171, MOD_BG);
    for (int i = 0; i < MAIN_COUNT; i++) {
        drawMainRow(i, i == currentEntry, false);
    }
}

static void redrawMainMenu(bool pressed = false) {
    drawFrame();
    drawMainHeader();
    drawMainFooter();
    if (pressed) {
        drawAllMainRows();
        drawMainRow(currentEntry, true, true);
        drawPreviewPanel(true);
    } else {
        drawAllMainRows();
        drawPreviewPanel(false);
    }
}

static void changeMainEntry(int nextEntry) {
    int oldEntry = currentEntry;
    currentEntry = nextEntry;
    tft.startWrite();
    drawMainRow(oldEntry, false, false);
    drawMainRow(currentEntry, true, false);
    drawPreviewPanel(false);
    drawMainHeader();
    tft.endWrite();
}

static void handlerWifi() {
    static const char* wifiItems[] = {
        "WiFi Scanner",
        "Channel Scan",
        "WiFi Radar",
        "Direction Finder",
        "Beacon Spam",
        "Deauther",
        "Evil Portal",
        "Probe Sniffer",
        "KARMA Attack"
    };

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenu("WIFI TOOLS", wifiItems,
                                sizeof(wifiItems) / sizeof(char*));
        switch (choice) {
            case -1: exitSub = true;    break;
            case  0: runWifiScan();     break;
            case  1: runWifiChannelScanner(); break;
            case  2: runWifiRadar();    break;
            case  3: runWifiDirectionFinder(); break;
            case  4: runBeaconSpam();   break;
            case  5: runDeauther();     break;
            case  6: runEvilPortal();   break;
            case  7: runProbeSniffer(); break;
            case  8: runKarma();        break;
        }
    }
}

static void handlerRadio() {
    static const char* radioItems[] = {
        "Jammer",
        "Spectrum",
        "Signal Tools",
        "CC1101"
    };

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenu("RADIO TOOLS", radioItems,
                                sizeof(radioItems) / sizeof(char*));
        switch (choice) {
            case -1: exitSub = true;       break;
            case  0: runRadioJammer();     break;
            case  1: runRadioScanner();    break;
            case  2: runSignalTools();      break;
            case  3: runCC1101Tools();      break;
        }
    }
}

static void handlerBT() {
    static const char* btItems[] = {
        "BLE Device Radar",
        "BLE Inspector",
        "iPhone Remote",
        "BLE Spam",
        "BT Disruptor"
    };

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenu("BLUETOOTH", btItems,
                                sizeof(btItems) / sizeof(char*));
        switch (choice) {
            case -1: exitSub = true;   break;
            case  0: runBLEDeviceRadar(); break;
            case  1: runBLEInspector();   break;
            case  2: runBLEIPhoneRemote(); break;
            case  3: runBLESpam();        break;
            case  4: runBTDisruptor();    break;
        }
    }
}

static void handlerMonitor() {
    runPacketMonitor();
}

static void handlerSystem() {
    static const char* systemItems[] = {
        "Settings",
        "System Info",
        "Clock & Weather",
        "About"
    };

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenu("SYSTEM", systemItems,
                                sizeof(systemItems) / sizeof(char*));
        switch (choice) {
            case -1: exitSub = true;      break;
            case  0: runSettings();       break;
            case  1: runSystemInfo();     break;
            case  2: runClockWeather();   break;
            case  3: runAbout();          break;
        }
    }
}

void runMainMenu() {
    redrawMainMenu(false);

    unsigned long lastPress = 0;
    unsigned long lastActivity = millis();

    while (true) {
        if (digitalRead(BTN_UP) == LOW && (millis() - lastPress > 200)) {
            int prev = (currentEntry - 1 + MAIN_COUNT) % MAIN_COUNT;
            beep(2200, 25);
            changeMainEntry(prev);
            lastPress = millis();
            lastActivity = millis();
        }

        if (digitalRead(BTN_DOWN) == LOW && (millis() - lastPress > 200)) {
            int next = (currentEntry + 1) % MAIN_COUNT;
            beep(2200, 25);
            changeMainEntry(next);
            lastPress = millis();
            lastActivity = millis();
        }

        if (digitalRead(BTN_OK) == LOW && (millis() - lastPress > 350)) {
            tft.startWrite();
            drawMainRow(currentEntry, true, true);
            tft.endWrite();

            beep(1800, 40);
            delay(80);
            while (digitalRead(BTN_OK) == LOW) delay(5);

            MAIN_ENTRIES[currentEntry].handler();

            redrawMainMenu(false);
            lastPress = millis();
            lastActivity = millis();
        }

        if (millis() - lastActivity > SCREENSAVER_IDLE_MS) {
            runScreensaver();
            redrawMainMenu(false);
            lastActivity = millis();
            lastPress = millis();
        }

        delay(10);
    }
}

int runSubMenu(const char* title, const char* items[], int count,
               bool rememberCursor) {
    const int VISIBLE = 5;
    const int LINE_H = 30;
    const int LIST_Y = 50;

    if (count <= 0) return -1;

    int totalItems = count;
    int memorySlot = rememberCursor ? findSubMenuCursorSlot(title, true) : -1;
    int cursor = (memorySlot >= 0) ? submenuCursorMemory[memorySlot].cursor : 0;
    if (cursor < 0) cursor = 0;
    if (cursor >= totalItems) cursor = totalItems - 1;
    int scrollOffset = 0;
    if (cursor >= VISIBLE) scrollOffset = cursor - VISIBLE + 1;
    int result = -2;
    unsigned long lastPress = 0;

    auto drawHeaderFooter = [&]() {
        drawFrame();
        drawStringBig(12, 8, title, MOD_TEXT, 1);
        drawBitmapRight(306, 13, String(count) + " ITEMS", MOD_TEXT, 1, FONT_SMALL);
        drawStringCustom(10, 218, "UP/DN: MOVE", MOD_TEXT, 1);
        drawBitmapRight(310, 218, "OK:HOLD BACK", MOD_TEXT, 1, FONT_SMALL);
    };

    auto drawItem = [&](int idx, int row, bool selected) {
        if (row < 0 || row >= VISIBLE) return;

        int y = LIST_Y + row * LINE_H;
        uint16_t bg = selected ? MOD_TEXT : MOD_BG;
        uint16_t fg = selected ? MOD_INVERT : MOD_TEXT;
        String label = String(items[idx]);

        tft.fillRect(10, y - 5, 300, LINE_H - 3, bg);
        tft.drawRect(10, y - 5, 300, LINE_H - 3, MOD_LINE);
        drawStringCustom(22, y + 2, label, fg, 2);
    };

    auto drawScrollBar = [&]() {
        tft.fillRect(313, 42, 3, 162, MOD_BG);
        if (totalItems <= VISIBLE) return;

        int barH = (VISIBLE * 162) / totalItems;
        int barY = 42 + (scrollOffset * (162 - barH)) / (totalItems - VISIBLE);
        tft.fillRect(313, barY, 3, barH, MOD_TEXT);
    };

    auto drawVisible = [&]() {
        tft.fillRect(1, 35, 318, 171, MOD_BG);
        for (int row = 0; row < VISIBLE; row++) {
            int idx = scrollOffset + row;
            if (idx < totalItems) {
                drawItem(idx, row, idx == cursor);
            }
        }
        drawScrollBar();
    };

    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(80);

    tft.startWrite();
    drawHeaderFooter();
    drawVisible();
    tft.endWrite();

    while (result == -2) {
        if (digitalRead(BTN_UP) == LOW && (millis() - lastPress > 120)) {
            cursor = (cursor - 1 + totalItems) % totalItems;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE) scrollOffset = cursor - VISIBLE + 1;

            beep(2200, 15);
            tft.startWrite();
            drawVisible();
            tft.endWrite();
            lastPress = millis();
        }

        if (digitalRead(BTN_DOWN) == LOW && (millis() - lastPress > 120)) {
            cursor = (cursor + 1) % totalItems;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + VISIBLE) scrollOffset = cursor - VISIBLE + 1;

            beep(2200, 15);
            tft.startWrite();
            drawVisible();
            tft.endWrite();
            lastPress = millis();
        }

        if (digitalRead(BTN_OK) == LOW && (millis() - lastPress > 280)) {
            bool held = waitOkReleaseWasLong();
            beep(held ? 1000 : 1500, 40);
            result = held ? -1 : cursor;
            lastPress = millis();
        }

        delay(4);
    }

    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(60);

    if (memorySlot >= 0) {
        submenuCursorMemory[memorySlot].cursor = cursor;
    }

    return result;
}
