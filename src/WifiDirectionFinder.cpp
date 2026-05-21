#include "WifiDirectionFinder.h"

#include <Arduino.h>
#include <WiFi.h>

#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"

static constexpr uint8_t WIFI_DIR_MAX_APS = 36;
static constexpr uint8_t WIFI_DIR_VISIBLE = 6;
static constexpr uint8_t WIFI_DIR_SAMPLES = 3;
static constexpr int WIFI_DIR_LIST_X = 8;
static constexpr int WIFI_DIR_LIST_Y = 56;
static constexpr int WIFI_DIR_LIST_W = 304;
static constexpr int WIFI_DIR_ROW_H = 24;

struct WifiDirAp {
    String ssid;
    String bssid;
    int channel = 0;
    int rssi = -127;
    uint8_t auth = WIFI_AUTH_OPEN;
    bool hidden = false;
};

static WifiDirAp wifiDirAps[WIFI_DIR_MAX_APS];
static uint8_t wifiDirCount = 0;
static uint8_t wifiDirSelected = 0;
static uint8_t wifiDirScroll = 0;
static WifiDirAp wifiDirTarget;

static String authShort(uint8_t type) {
    switch (type) {
        case WIFI_AUTH_OPEN: return "OP";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "W2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "W2";
        case WIFI_AUTH_WPA3_PSK: return "W3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "W3";
        default: return "?";
    }
}

static String ssidLabel(const WifiDirAp& ap) {
    if (ap.ssid.length() == 0 || ap.hidden) return "<HIDDEN>";
    return ap.ssid;
}

static String shortBssid(const String& bssid) {
    if (bssid.length() <= 8) return bssid;
    return bssid.substring(bssid.length() - 8);
}

static uint8_t rssiPct(int rssi) {
    if (rssi <= -100) return 0;
    if (rssi >= -35) return 100;
    return (uint8_t)(((rssi + 100) * 100) / 65);
}

static uint16_t rssiColor(int rssi) {
    if (rssi >= -58) return TFT_GREEN;
    if (rssi >= -74) return TFT_YELLOW;
    if (rssi >= -88) return TFT_ORANGE;
    return TFT_RED;
}

static String confidenceText(int diffDb) {
    if (diffDb >= 9) return "HIGH";
    if (diffDb >= 5) return "MEDIUM";
    if (diffDb >= 3) return "LOW";
    return "UNCLEAR";
}

static uint16_t confidenceColor(int diffDb) {
    if (diffDb >= 9) return TFT_GREEN;
    if (diffDb >= 5) return TFT_CYAN;
    if (diffDb >= 3) return TFT_YELLOW;
    return TFT_RED;
}

static void wifiPrepare() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    delay(70);
}

static void drawFrame(const char* title, const char* footer) {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    tft.drawFastHLine(0, 32, 320, TFT_WHITE);
    tft.drawFastHLine(0, 216, 320, TFT_WHITE);
    drawStringBig(10, 8, title, TFT_WHITE, 1);
    drawStringCustom(10, 224, footer, TFT_WHITE, 1);
}

static void drawBar(int x, int y, int w, int h, uint8_t pct, uint16_t color) {
    pct = constrain(pct, 0, 100);
    tft.drawRect(x, y, w, h, TFT_DARKGREY);
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
    int fillW = ((w - 2) * pct) / 100;
    if (fillW > 0) tft.fillRect(x + 1, y + 1, fillW, h - 2, color);
}

static void drawScanning() {
    drawFrame("WIFI DIRECTION", "Scanning...");
    tft.drawRect(18, 58, 284, 96, TFT_CYAN);
    drawStringFit(32, 78, "Escaneando redes WiFi 2.4GHz.",
                  TFT_CYAN, 256, 1);
    drawStringFit(32, 104, "Elige un AP para comparar direccion.",
                  TFT_WHITE, 256, 1);
    drawStringFit(32, 128, "No conecta ni autentica.",
                  TFT_YELLOW, 256, 1);
}

static void sortAps() {
    for (uint8_t i = 0; i < wifiDirCount; i++) {
        for (uint8_t j = i + 1; j < wifiDirCount; j++) {
            if (wifiDirAps[j].rssi > wifiDirAps[i].rssi) {
                WifiDirAp temp = wifiDirAps[i];
                wifiDirAps[i] = wifiDirAps[j];
                wifiDirAps[j] = temp;
            }
        }
    }
}

static void scanAps() {
    wifiPrepare();
    drawScanning();
    wifiDirCount = 0;
    wifiDirSelected = 0;
    wifiDirScroll = 0;

    int n = WiFi.scanNetworks(false, true, false, 160);
    if (n < 0) n = 0;
    for (int i = 0; i < n && wifiDirCount < WIFI_DIR_MAX_APS; i++) {
        WifiDirAp& ap = wifiDirAps[wifiDirCount++];
        ap.ssid = WiFi.SSID(i);
        ap.bssid = WiFi.BSSIDstr(i);
        ap.channel = WiFi.channel(i);
        ap.rssi = WiFi.RSSI(i);
        ap.auth = WiFi.encryptionType(i);
        ap.hidden = ap.ssid.length() == 0;
    }
    WiFi.scanDelete();
    sortAps();
}

static void drawApRow(uint8_t row) {
    int y = WIFI_DIR_LIST_Y + row * WIFI_DIR_ROW_H;
    tft.fillRect(WIFI_DIR_LIST_X, y - 2, WIFI_DIR_LIST_W,
                 WIFI_DIR_ROW_H - 2, TFT_BLACK);

    uint8_t idx = wifiDirScroll + row;
    if (idx >= wifiDirCount) return;

    const WifiDirAp& ap = wifiDirAps[idx];
    bool selected = idx == wifiDirSelected;
    uint16_t bg = selected ? TFT_CYAN : TFT_BLACK;
    uint16_t fg = selected ? TFT_BLACK : TFT_WHITE;
    uint16_t sub = selected ? TFT_BLACK : TFT_DARKGREY;
    uint16_t rssiCol = selected ? TFT_BLACK : rssiColor(ap.rssi);

    tft.fillRect(WIFI_DIR_LIST_X, y - 2, WIFI_DIR_LIST_W,
                 WIFI_DIR_ROW_H - 2, bg);
    tft.drawRect(WIFI_DIR_LIST_X, y - 2, WIFI_DIR_LIST_W,
                 WIFI_DIR_ROW_H - 2, selected ? TFT_WHITE : TFT_DARKGREY);
    drawStringFit(16, y, ssidLabel(ap), fg, 170, 1);
    drawStringRight(304, y, String(ap.rssi) + "dBm CH" + ap.channel,
                    rssiCol, 1);
    drawStringFit(16, y + 12, shortBssid(ap.bssid), sub, 210, 1);
    drawStringRight(304, y + 12, authShort(ap.auth), sub, 1);
}

static void drawRows() {
    for (uint8_t row = 0; row < WIFI_DIR_VISIBLE; row++) drawApRow(row);
}

static void drawSelectedFooter() {
    tft.fillRect(12, 200, 296, 12, TFT_BLACK);
    if (wifiDirCount == 0) return;
    const WifiDirAp& ap = wifiDirAps[wifiDirSelected];
    drawStringFit(12, 200, ap.bssid + "  CH" + ap.channel,
                  TFT_DARKGREY, 296, 1);
}

static void drawList() {
    drawFrame("WIFI DIRECTION", "UP/DN MOVE  OK:START  HOLD:BACK");
    drawStringCustom(12, 40, "Networks: " + String(wifiDirCount),
                     TFT_CYAN, 1);

    if (wifiDirCount == 0) {
        drawStringCustom(44, 104, "NO WIFI NETWORKS", TFT_YELLOW, 2);
        drawStringFit(44, 134, "Tap OK to scan again.",
                      TFT_WHITE, 240, 1);
        return;
    }

    drawRows();
    drawSelectedFooter();
}

static void updateSelection(uint8_t oldSelected, uint8_t oldScroll) {
    if (oldScroll != wifiDirScroll) {
        drawRows();
    } else {
        if (oldSelected >= wifiDirScroll &&
            oldSelected < wifiDirScroll + WIFI_DIR_VISIBLE) {
            drawApRow(oldSelected - wifiDirScroll);
        }
        if (wifiDirSelected >= wifiDirScroll &&
            wifiDirSelected < wifiDirScroll + WIFI_DIR_VISIBLE) {
            drawApRow(wifiDirSelected - wifiDirScroll);
        }
    }
    drawSelectedFooter();
}

static bool scanTargetOnce(int& rssiOut) {
    wifiPrepare();
    int n = WiFi.scanNetworks(false, true, false, 170, wifiDirTarget.channel);
    bool found = false;
    int best = -127;
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            if (WiFi.BSSIDstr(i).equalsIgnoreCase(wifiDirTarget.bssid)) {
                best = WiFi.RSSI(i);
                found = true;
                break;
            }
        }
    }
    WiFi.scanDelete();
    rssiOut = best;
    return found;
}

static bool waitForOkOnPrompt(const char* sector) {
    drawFrame("WIFI DIRECTION", "OK:MEASURE  HOLD:CANCEL");
    tft.drawRect(18, 44, 284, 136, TFT_CYAN);
    drawStringCustom(34, 62, "Apunta hacia:", TFT_DARKGREY, 1);
    drawStringCustom(34, 86, sector, TFT_CYAN, 3);
    drawStringFit(34, 128, "Manten el equipo quieto y presiona OK.",
                  TFT_WHITE, 250, 1);
    drawStringFit(34, 150, ssidLabel(wifiDirTarget),
                  TFT_YELLOW, 250, 1);

    while (true) {
        if (digitalRead(BTN_OK) == LOW) {
            bool held = waitOkReleaseWasLong();
            delay(80);
            return !held;
        }
        delay(10);
    }
}

static void drawMeasureScreen(const char* sector, uint8_t sample,
                              uint8_t total) {
    drawFrame("WIFI DIRECTION", "Measuring...");
    tft.drawRect(18, 56, 284, 104, TFT_GREEN);
    drawStringFit(34, 78, String("Midiendo ") + sector,
                  TFT_CYAN, 250, 1);
    drawStringCustom(34, 104, "Sample " + String(sample) + "/" +
                     String(total), TFT_WHITE, 1);
    drawBar(34, 132, 252, 12, (sample * 100) / total, TFT_GREEN);
}

static bool measureSector(const char* sector, int& averageOut) {
    int sum = 0;
    uint8_t hits = 0;

    for (uint8_t i = 0; i < WIFI_DIR_SAMPLES; i++) {
        drawMeasureScreen(sector, i + 1, WIFI_DIR_SAMPLES);
        int rssi = -127;
        if (scanTargetOnce(rssi)) {
            sum += rssi;
            hits++;
        }
        delay(90);
    }

    if (hits == 0) {
        averageOut = -127;
        return false;
    }

    averageOut = sum / hits;
    return true;
}

static uint8_t bestIndex(const int* values, const bool* valid) {
    uint8_t best = 255;
    for (uint8_t i = 0; i < 4; i++) {
        if (!valid[i]) continue;
        if (best == 255 || values[i] > values[best]) best = i;
    }
    return best;
}

static int secondBest(const int* values, const bool* valid, uint8_t best) {
    int second = -127;
    for (uint8_t i = 0; i < 4; i++) {
        if (!valid[i] || i == best) continue;
        if (values[i] > second) second = values[i];
    }
    return second;
}

static void drawResult(const int* values, const bool* valid) {
    static const char* labels[] = {"FRENTE", "DERECHA", "ATRAS", "IZQUIERDA"};
    uint8_t best = bestIndex(values, valid);
    int second = best == 255 ? -127 : secondBest(values, valid, best);
    int diff = best == 255 ? 0 : values[best] - second;
    uint16_t mainColor = best == 255 ? TFT_RED : confidenceColor(diff);

    drawFrame("WIFI DIRECTION", "OK:REPEAT  HOLD:LIST");
    tft.drawRect(12, 42, 296, 70, mainColor);
    if (best == 255) {
        drawStringFit(24, 62, "No se encontro el BSSID objetivo.",
                      TFT_RED, 270, 1);
        drawStringFit(24, 84, "Acercate o vuelve a escanear.",
                      TFT_YELLOW, 270, 1);
    } else {
        drawStringFit(24, 56, String("Mayor senal: ") + labels[best],
                      mainColor, 270, 1);
        drawStringFit(24, 78, "Confianza " + confidenceText(diff) +
                      "  +" + String(diff) + " dB",
                      TFT_CYAN, 270, 1);
        drawStringRight(296, 94, String(values[best]) + " dBm",
                        TFT_GREEN, 1);
    }

    int y0 = 126;
    for (uint8_t i = 0; i < 4; i++) {
        int y = y0 + i * 21;
        uint16_t color = (i == best) ? TFT_GREEN :
                         (valid[i] ? TFT_CYAN : TFT_RED);
        drawStringCustom(18, y, labels[i], color, 1);
        drawBar(92, y + 3, 142, 9, valid[i] ? rssiPct(values[i]) : 0, color);
        drawStringRight(304, y, valid[i] ? String(values[i]) + "dBm" : "--",
                        color, 1);
    }

    drawStringFit(18, 204, "Si sale incierto, repite girando mas lento.",
                  TFT_DARKGREY, 286, 1);
}

static void runDirectionForTarget() {
    static const char* sectors[] = {"FRENTE", "DERECHA", "ATRAS", "IZQUIERDA"};
    int values[4] = {-127, -127, -127, -127};
    bool valid[4] = {false, false, false, false};

    while (true) {
        for (uint8_t i = 0; i < 4; i++) {
            if (!waitForOkOnPrompt(sectors[i])) return;
            valid[i] = measureSector(sectors[i], values[i]);
        }

        drawResult(values, valid);
        while (true) {
            if (digitalRead(BTN_OK) == LOW) {
                bool held = waitOkReleaseWasLong();
                delay(80);
                if (held) return;
                break;
            }
            delay(10);
        }
    }
}

void runWifiDirectionFinder() {
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);

    scanAps();
    drawList();
    beep(1500, 30);

    while (true) {
        if (digitalRead(BTN_DOWN) == LOW) {
            if (wifiDirCount > 0) {
                uint8_t oldSelected = wifiDirSelected;
                uint8_t oldScroll = wifiDirScroll;
                wifiDirSelected = (wifiDirSelected + 1) % wifiDirCount;
                if (wifiDirSelected < wifiDirScroll) {
                    wifiDirScroll = wifiDirSelected;
                }
                if (wifiDirSelected >= wifiDirScroll + WIFI_DIR_VISIBLE) {
                    wifiDirScroll = wifiDirSelected - WIFI_DIR_VISIBLE + 1;
                }
                beep(2200, 20);
                updateSelection(oldSelected, oldScroll);
            }
            delay(170);
        }

        if (digitalRead(BTN_UP) == LOW) {
            if (wifiDirCount > 0) {
                uint8_t oldSelected = wifiDirSelected;
                uint8_t oldScroll = wifiDirScroll;
                wifiDirSelected = wifiDirSelected == 0
                    ? wifiDirCount - 1
                    : wifiDirSelected - 1;
                if (wifiDirSelected < wifiDirScroll) {
                    wifiDirScroll = wifiDirSelected;
                }
                if (wifiDirSelected >= wifiDirScroll + WIFI_DIR_VISIBLE) {
                    wifiDirScroll = wifiDirSelected - WIFI_DIR_VISIBLE + 1;
                }
                beep(2200, 20);
                updateSelection(oldSelected, oldScroll);
            }
            delay(170);
        }

        if (digitalRead(BTN_OK) == LOW) {
            bool held = waitOkReleaseWasLong();
            delay(80);
            if (held) break;

            if (wifiDirCount == 0) {
                scanAps();
                drawList();
                continue;
            }

            wifiDirTarget = wifiDirAps[wifiDirSelected];
            beep(3200, 20);
            runDirectionForTarget();
            drawList();
        }

        delay(10);
    }

    WiFi.scanDelete();
    WiFi.disconnect(false);
}
