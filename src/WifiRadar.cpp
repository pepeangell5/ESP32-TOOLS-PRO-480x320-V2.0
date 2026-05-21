#include "WifiRadar.h"

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>

#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"

static constexpr uint8_t WIFI_RADAR_MAX_APS = 40;
static constexpr uint8_t WIFI_RADAR_VISIBLE = 6;
static constexpr uint8_t WIFI_RADAR_HISTORY = 36;
static constexpr int WIFI_LIST_X = 8;
static constexpr int WIFI_LIST_Y = 56;
static constexpr int WIFI_LIST_W = 304;
static constexpr int WIFI_ROW_H = 24;

struct WifiRadarAp {
    String ssid;
    String bssid;
    int channel = 0;
    int rssi = -127;
    uint8_t auth = WIFI_AUTH_OPEN;
    bool hidden = false;
};

static WifiRadarAp wifiRadarAps[WIFI_RADAR_MAX_APS];
static uint8_t wifiRadarCount = 0;
static uint8_t wifiRadarSelected = 0;
static uint8_t wifiRadarScroll = 0;

static WifiRadarAp wifiTarget;
static bool wifiTargetSeen = false;
static int wifiTargetRssi = -127;
static int wifiLastTargetRssi = -127;
static int wifiBestRssi = -127;
static int wifiTrendDb = 0;
static int16_t wifiHistory[WIFI_RADAR_HISTORY];
static uint8_t wifiHistoryHead = 0;
static uint32_t wifiScanPass = 0;

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

static String authLong(uint8_t type) {
    switch (type) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        default: return "?";
    }
}

static uint16_t authColor(uint8_t type) {
    switch (type) {
        case WIFI_AUTH_OPEN: return TFT_RED;
        case WIFI_AUTH_WEP: return TFT_ORANGE;
        case WIFI_AUTH_WPA_PSK: return TFT_YELLOW;
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK: return TFT_GREEN;
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK: return TFT_CYAN;
        default: return TFT_WHITE;
    }
}

static uint16_t rssiColor(int rssi) {
    if (rssi >= -58) return TFT_GREEN;
    if (rssi >= -74) return TFT_YELLOW;
    if (rssi >= -88) return TFT_ORANGE;
    return TFT_RED;
}

static uint8_t rssiPct(int rssi) {
    if (rssi <= -100) return 0;
    if (rssi >= -35) return 100;
    return (uint8_t)(((rssi + 100) * 100) / 65);
}

static String proximityText(uint8_t pct) {
    if (!wifiTargetSeen) return "SEARCH";
    if (pct >= 76) return "VERY CLOSE";
    if (pct >= 52) return "CLOSE";
    if (pct >= 28) return "NEAR";
    return "FAR";
}

static String trendText() {
    if (!wifiTargetSeen) return "SEARCHING";
    if (wifiTrendDb >= 4) return "APPROACH";
    if (wifiTrendDb <= -4) return "MOVING AWAY";
    return "STABLE";
}

static uint16_t trendColor() {
    if (!wifiTargetSeen) return TFT_RED;
    if (wifiTrendDb >= 4) return TFT_GREEN;
    if (wifiTrendDb <= -4) return TFT_YELLOW;
    return TFT_CYAN;
}

static float estimateMeters(int rssi) {
    if (rssi <= -120) return 99.0f;
    const float txAtOneMeter = -45.0f;
    const float pathLoss = 2.15f;
    float meters = powf(10.0f, (txAtOneMeter - rssi) / (10.0f * pathLoss));
    if (meters < 0.2f) meters = 0.2f;
    if (meters > 99.0f) meters = 99.0f;
    return meters;
}

static String ssidLabel(const WifiRadarAp& ap) {
    if (ap.ssid.length() == 0 || ap.hidden) return "<HIDDEN>";
    return ap.ssid;
}

static String shortBssid(const String& bssid) {
    if (bssid.length() <= 8) return bssid;
    return bssid.substring(bssid.length() - 8);
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

static void drawScanning(const String& line) {
    drawFrame("WIFI RADAR", "Scanning...");
    tft.drawRect(18, 58, 284, 96, TFT_CYAN);
    drawStringFit(32, 78, line, TFT_CYAN, 256, 1);
    drawStringFit(32, 104, "Modo pasivo: no conecta ni autentica.",
                  TFT_WHITE, 256, 1);
    drawStringFit(32, 128, "Elige un AP para rastrear RSSI.",
                  TFT_YELLOW, 256, 1);
}

static void sortAps() {
    for (uint8_t i = 0; i < wifiRadarCount; i++) {
        for (uint8_t j = i + 1; j < wifiRadarCount; j++) {
            if (wifiRadarAps[j].rssi > wifiRadarAps[i].rssi) {
                WifiRadarAp temp = wifiRadarAps[i];
                wifiRadarAps[i] = wifiRadarAps[j];
                wifiRadarAps[j] = temp;
            }
        }
    }
}

static void scanAps() {
    wifiPrepare();
    drawScanning("Escaneando redes WiFi...");
    wifiRadarCount = 0;
    wifiRadarSelected = 0;
    wifiRadarScroll = 0;

    int n = WiFi.scanNetworks(false, true, false, 160);
    if (n < 0) n = 0;
    for (int i = 0; i < n && wifiRadarCount < WIFI_RADAR_MAX_APS; i++) {
        WifiRadarAp& ap = wifiRadarAps[wifiRadarCount++];
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
    int y = WIFI_LIST_Y + row * WIFI_ROW_H;
    tft.fillRect(WIFI_LIST_X, y - 2, WIFI_LIST_W, WIFI_ROW_H - 2,
                 TFT_BLACK);

    uint8_t idx = wifiRadarScroll + row;
    if (idx >= wifiRadarCount) return;

    const WifiRadarAp& ap = wifiRadarAps[idx];
    bool selected = idx == wifiRadarSelected;
    uint16_t bg = selected ? TFT_CYAN : TFT_BLACK;
    uint16_t fg = selected ? TFT_BLACK : TFT_WHITE;
    uint16_t sub = selected ? TFT_BLACK : TFT_DARKGREY;
    uint16_t rssiCol = selected ? TFT_BLACK : rssiColor(ap.rssi);

    tft.fillRect(WIFI_LIST_X, y - 2, WIFI_LIST_W, WIFI_ROW_H - 2, bg);
    tft.drawRect(WIFI_LIST_X, y - 2, WIFI_LIST_W, WIFI_ROW_H - 2,
                 selected ? TFT_WHITE : TFT_DARKGREY);
    drawStringFit(16, y, ssidLabel(ap), fg, 170, 1);
    drawStringRight(304, y, String(ap.rssi) + "dBm CH" + ap.channel,
                    rssiCol, 1);
    drawStringFit(16, y + 12, shortBssid(ap.bssid) + "  " +
                  authLong(ap.auth), sub, 226, 1);
    drawStringRight(304, y + 12, authShort(ap.auth),
                    selected ? TFT_BLACK : authColor(ap.auth), 1);
}

static void drawVisibleRows() {
    for (uint8_t row = 0; row < WIFI_RADAR_VISIBLE; row++) {
        drawApRow(row);
    }
}

static void drawSelectedFooter() {
    tft.fillRect(12, 200, 296, 12, TFT_BLACK);
    if (wifiRadarCount == 0) return;
    const WifiRadarAp& ap = wifiRadarAps[wifiRadarSelected];
    drawStringFit(12, 200, ap.bssid + "  " + authLong(ap.auth),
                  TFT_DARKGREY, 296, 1);
}

static void drawApList() {
    drawFrame("WIFI RADAR", "UP/DN MOVE  OK:RADAR  HOLD:BACK");
    drawStringCustom(12, 40, "Networks: " + String(wifiRadarCount),
                     TFT_CYAN, 1);

    if (wifiRadarCount == 0) {
        drawStringCustom(44, 104, "NO WIFI NETWORKS", TFT_YELLOW, 2);
        drawStringFit(44, 134, "Tap OK to scan again.",
                      TFT_WHITE, 240, 1);
        return;
    }

    drawVisibleRows();
    drawSelectedFooter();
}

static void updateSelection(uint8_t oldSelected, uint8_t oldScroll) {
    if (oldScroll != wifiRadarScroll) {
        drawVisibleRows();
    } else {
        if (oldSelected >= wifiRadarScroll &&
            oldSelected < wifiRadarScroll + WIFI_RADAR_VISIBLE) {
            drawApRow(oldSelected - wifiRadarScroll);
        }
        if (wifiRadarSelected >= wifiRadarScroll &&
            wifiRadarSelected < wifiRadarScroll + WIFI_RADAR_VISIBLE) {
            drawApRow(wifiRadarSelected - wifiRadarScroll);
        }
    }
    drawSelectedFooter();
}

static void resetTarget() {
    wifiTargetSeen = false;
    wifiTargetRssi = -127;
    wifiLastTargetRssi = -127;
    wifiBestRssi = -127;
    wifiTrendDb = 0;
    wifiHistoryHead = 0;
    wifiScanPass = 0;
    memset(wifiHistory, 0, sizeof(wifiHistory));
}

static void rememberSample(int rssi) {
    wifiHistory[wifiHistoryHead] = (int16_t)rssi;
    wifiHistoryHead = (wifiHistoryHead + 1) % WIFI_RADAR_HISTORY;
}

static bool scanTargetOnce(int& rssiOut) {
    wifiPrepare();
    int n = WiFi.scanNetworks(false, true, false, 170, wifiTarget.channel);
    bool found = false;
    int best = -127;

    if (n > 0) {
        for (int i = 0; i < n; i++) {
            String bssid = WiFi.BSSIDstr(i);
            if (bssid.equalsIgnoreCase(wifiTarget.bssid)) {
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

static void updateTarget() {
    int rssi = -127;
    bool found = scanTargetOnce(rssi);
    wifiScanPass++;
    wifiLastTargetRssi = wifiTargetRssi;
    wifiTargetSeen = found;

    if (found) {
        wifiTargetRssi = rssi;
        if (wifiBestRssi < -120 || rssi > wifiBestRssi) wifiBestRssi = rssi;
        wifiTrendDb = wifiLastTargetRssi < -120 ? 0 : rssi - wifiLastTargetRssi;
        rememberSample(rssi);
    } else {
        wifiTargetRssi = -127;
        wifiTrendDb = -8;
        rememberSample(-100);
    }
}

static void drawRadarFrame() {
    drawFrame("WIFI RADAR", "OK:RESCAN  HOLD:LIST");
    drawStringFit(14, 38, ssidLabel(wifiTarget), TFT_CYAN, 292, 1);
    drawStringFit(14, 204, wifiTarget.bssid + "  CH" + wifiTarget.channel,
                  TFT_DARKGREY, 292, 1);

    tft.drawRect(14, 52, 128, 128, TFT_CYAN);
    tft.drawRect(154, 52, 154, 62, TFT_DARKGREY);
    tft.drawRect(154, 124, 154, 72, TFT_DARKGREY);
    tft.drawRect(18, 188, 124, 22, TFT_DARKGREY);
}

static void drawRadarPlot(uint8_t pct, uint16_t color) {
    const int cx = 78;
    const int cy = 116;
    const int maxR = 58;

    tft.fillCircle(cx, cy, maxR + 4, TFT_BLACK);
    tft.drawCircle(cx, cy, maxR, TFT_CYAN);
    tft.drawCircle(cx, cy, 42, TFT_DARKGREY);
    tft.drawCircle(cx, cy, 26, TFT_DARKGREY);
    tft.drawCircle(cx, cy, 12, TFT_DARKGREY);
    tft.drawFastHLine(cx - maxR, cy, maxR * 2, TFT_DARKGREY);
    tft.drawFastVLine(cx, cy - maxR, maxR * 2, TFT_DARKGREY);

    int dotR = map(pct, 0, 100, maxR - 3, 9);
    float angle = ((wifiScanPass * 37) % 360) * 0.0174532925f;
    int dx = cx + (int)(cosf(angle) * dotR);
    int dy = cy + (int)(sinf(angle) * dotR);

    if (!wifiTargetSeen) color = TFT_RED;
    tft.drawCircle(dx, dy, 12, color);
    tft.drawCircle(dx, dy, 18, pct > 45 ? TFT_DARKGREEN : TFT_DARKGREY);
    tft.fillCircle(dx, dy, 6, color);
    tft.fillCircle(cx, cy, 4, TFT_CYAN);

    drawStringCustom(26, 62, proximityText(pct), color, 1);
    drawStringCustom(32, 160, String(pct) + "%", color, 2);
}

static void drawHistory() {
    const int x = 18;
    const int y = 188;
    const int w = 124;
    const int h = 22;
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
    tft.drawRect(x, y, w, h, TFT_DARKGREY);
    for (uint8_t i = 0; i < WIFI_RADAR_HISTORY; i++) {
        uint8_t idx = (wifiHistoryHead + i) % WIFI_RADAR_HISTORY;
        if (wifiHistory[idx] == 0) continue;
        uint8_t pct = rssiPct(wifiHistory[idx]);
        int px = x + 2 + (i * (w - 4)) / WIFI_RADAR_HISTORY;
        int barH = max(1, (pct * (h - 4)) / 100);
        uint16_t color = pct > 70 ? TFT_GREEN :
                         (pct > 38 ? TFT_YELLOW : TFT_RED);
        tft.drawFastVLine(px, y + h - 2 - barH, barH, color);
    }
}

static void drawStats(uint8_t pct, uint16_t color) {
    tft.fillRect(158, 56, 146, 54, TFT_BLACK);
    String rssiLine = wifiTargetSeen ? String(wifiTargetRssi) + " dBm"
                                     : "-- dBm";
    drawStringCustom(164, 62, rssiLine + "  " + String(pct) + "%",
                     color, 1);
    drawStringCustom(164, 80, "Peak " +
                     (wifiBestRssi > -120 ? String(wifiBestRssi) : "--") +
                     " dBm",
                     wifiBestRssi > -120 ? TFT_GREEN : TFT_DARKGREY, 1);
    drawStringCustom(164, 98, "Trend " + String(wifiTrendDb) + " dB",
                     trendColor(), 1);

    tft.fillRect(158, 128, 146, 64, TFT_BLACK);
    drawStringCustom(164, 136, trendText(), trendColor(), 1);
    drawStringCustom(164, 154, "CH " + String(wifiTarget.channel) +
                     "  Scan " + String(wifiScanPass), TFT_DARKGREY, 1);
    String meters = wifiTargetSeen ? String(estimateMeters(wifiTargetRssi), 1)
                                   : "--";
    drawStringCustom(164, 172, "Dist ~ " + meters + " m", TFT_YELLOW, 1);
    drawBar(164, 188, 132, 6, pct, color);
}

static void drawRadarDynamic() {
    uint8_t pct = wifiTargetSeen ? rssiPct(wifiTargetRssi) : 0;
    uint16_t color = wifiTargetSeen ? rssiColor(wifiTargetRssi) : TFT_RED;
    drawRadarPlot(pct, color);
    drawStats(pct, color);
    drawHistory();
}

static void runTargetRadar() {
    resetTarget();
    drawRadarFrame();
    updateTarget();
    drawRadarDynamic();
    uint32_t lastScan = millis();

    while (true) {
        if (digitalRead(BTN_OK) == LOW) {
            bool held = waitOkReleaseWasLong();
            delay(80);
            if (held) return;
            updateTarget();
            drawRadarDynamic();
            lastScan = millis();
        }

        if (millis() - lastScan > 950) {
            updateTarget();
            drawRadarDynamic();
            lastScan = millis();
        }

        delay(15);
    }
}

void runWifiRadar() {
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);

    scanAps();
    drawApList();
    beep(1500, 30);

    while (true) {
        if (digitalRead(BTN_DOWN) == LOW) {
            if (wifiRadarCount > 0) {
                uint8_t oldSelected = wifiRadarSelected;
                uint8_t oldScroll = wifiRadarScroll;
                wifiRadarSelected = (wifiRadarSelected + 1) % wifiRadarCount;
                if (wifiRadarSelected < wifiRadarScroll) {
                    wifiRadarScroll = wifiRadarSelected;
                }
                if (wifiRadarSelected >= wifiRadarScroll + WIFI_RADAR_VISIBLE) {
                    wifiRadarScroll = wifiRadarSelected - WIFI_RADAR_VISIBLE + 1;
                }
                beep(2200, 20);
                updateSelection(oldSelected, oldScroll);
            }
            delay(170);
        }

        if (digitalRead(BTN_UP) == LOW) {
            if (wifiRadarCount > 0) {
                uint8_t oldSelected = wifiRadarSelected;
                uint8_t oldScroll = wifiRadarScroll;
                wifiRadarSelected = wifiRadarSelected == 0
                    ? wifiRadarCount - 1
                    : wifiRadarSelected - 1;
                if (wifiRadarSelected < wifiRadarScroll) {
                    wifiRadarScroll = wifiRadarSelected;
                }
                if (wifiRadarSelected >= wifiRadarScroll + WIFI_RADAR_VISIBLE) {
                    wifiRadarScroll = wifiRadarSelected - WIFI_RADAR_VISIBLE + 1;
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

            if (wifiRadarCount == 0) {
                scanAps();
                drawApList();
                continue;
            }

            wifiTarget = wifiRadarAps[wifiRadarSelected];
            beep(3200, 20);
            runTargetRadar();
            drawApList();
        }

        delay(10);
    }

    WiFi.scanDelete();
    WiFi.disconnect(false);
}
