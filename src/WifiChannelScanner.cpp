#include "WifiChannelScanner.h"

#include <WiFi.h>

#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"

static constexpr uint8_t WIFI_CH_MIN = 1;
static constexpr uint8_t WIFI_CH_MAX = 13;
static constexpr uint8_t CHANNEL_COUNT = WIFI_CH_MAX - WIFI_CH_MIN + 1;
static constexpr uint8_t MAX_CHANNEL_NETS = 60;
static constexpr uint8_t CHANNEL_VISIBLE = 6;
static constexpr uint8_t NET_VISIBLE = 6;

struct ChannelNet {
    String ssid;
    String bssid;
    int channel = 0;
    int rssi = -127;
    uint8_t auth = WIFI_AUTH_OPEN;
};

static ChannelNet channelNets[MAX_CHANNEL_NETS];
static uint8_t netCount = 0;
static uint8_t channelCounts[WIFI_CH_MAX + 1];
static int channelBestRssi[WIFI_CH_MAX + 1];
static uint8_t channelOpenCounts[WIFI_CH_MAX + 1];

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
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
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

static uint8_t rssiBars(int rssi) {
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

static uint16_t rssiColor(int rssi) {
    if (rssi >= -60) return TFT_GREEN;
    if (rssi >= -75) return TFT_YELLOW;
    if (rssi >= -90) return TFT_ORANGE;
    return TFT_RED;
}

static int channelToFreq(int ch) {
    if (ch >= 1 && ch <= 13) return 2407 + ch * 5;
    if (ch == 14) return 2484;
    return 0;
}

struct OuiEntry {
    uint32_t prefix;
    const char* vendor;
};

static const OuiEntry OUI_TABLE[] = {
    {0x001882, "Huawei"}, {0x00E0FC, "Huawei"}, {0x0CFC18, "Huawei"},
    {0xE00630, "Huawei"}, {0xF0A0B1, "Huawei"}, {0x404F42, "Huawei"},
    {0x7066B9, "Huawei"}, {0xC4A1AE, "Huawei"}, {0x001E10, "Huawei"},
    {0x0022F7, "Sercomm"}, {0x14C03E, "Sercomm"}, {0xA84E3F, "Sercomm"},
    {0x5004B8, "Sercomm"}, {0x74884E, "Sercomm"},
    {0x0015E0, "Arcadyan"}, {0x002308, "Arcadyan"}, {0x60310F, "Arcadyan"},
    {0x507E5D, "Arcadyan"}, {0x001D7E, "Askey"}, {0xC85195, "Askey"},
    {0x889676, "Askey"}, {0x00603E, "Nokia"}, {0x1CD446, "Nokia"},
    {0xF8E4FB, "Nokia"}, {0x00194B, "Nokia"}, {0x80D4A5, "Nokia"},
    {0x3086F1, "Fiberhome"}, {0xD876E7, "Fiberhome"},
    {0x0015CE, "Arris"}, {0x0026CE, "Arris"}, {0x54A619, "Arris"},
    {0xB83F5D, "Arris"}, {0x00248C, "Arris"},
    {0x00147F, "Technicolor"}, {0x002417, "Technicolor"},
    {0xC42795, "Technicolor"}, {0x6C5697, "Technicolor"},
    {0xD44B5E, "Technicolor"}, {0x008A5A, "Hitron"},
    {0x688F2E, "Hitron"}, {0x749D8F, "Hitron"},
    {0x0015EB, "ZTE"}, {0x344B50, "ZTE"}, {0xD0154A, "ZTE"},
    {0x4C16F1, "ZTE"}, {0x14D864, "TP-Link"}, {0x40ED00, "TP-Link"},
    {0x68DDB7, "TP-Link"}, {0x002719, "TP-Link"}, {0x14CC20, "TP-Link"},
    {0x50C7BF, "TP-Link"}, {0xE894F6, "TP-Link"}, {0xA842A1, "TP-Link"},
    {0x50D4F7, "Mercusys"}, {0xB4B024, "Mercusys"},
    {0x8CBEBE, "Xiaomi"}, {0x28E31F, "Xiaomi"}, {0x508F4C, "Xiaomi"},
    {0xF0B429, "Xiaomi"}, {0x64B473, "Xiaomi"}, {0x74510F, "Xiaomi"},
    {0x000393, "Apple"}, {0xACBC32, "Apple"}, {0xF45C89, "Apple"},
    {0xA45E60, "Apple"}, {0x001B63, "Apple"},
    {0x0012FB, "Samsung"}, {0x34BE00, "Samsung"}, {0x5C0A5B, "Samsung"},
    {0xE8508B, "Samsung"}, {0x001D25, "Samsung"},
};

static uint32_t macToOui(const String& mac) {
    if (mac.length() < 8) return 0;
    char buf[7];
    buf[0] = mac[0];
    buf[1] = mac[1];
    buf[2] = mac[3];
    buf[3] = mac[4];
    buf[4] = mac[6];
    buf[5] = mac[7];
    buf[6] = '\0';
    return strtoul(buf, nullptr, 16);
}

static String lookupVendor(const String& mac) {
    uint32_t oui = macToOui(mac);
    for (uint8_t i = 0; i < sizeof(OUI_TABLE) / sizeof(OUI_TABLE[0]); i++) {
        if (OUI_TABLE[i].prefix == oui) return String(OUI_TABLE[i].vendor);
    }
    return "Unknown";
}

static void drawSignalBars(int x, int y, uint8_t bars, uint16_t color) {
    const uint8_t heights[4] = { 4, 8, 12, 16 };
    for (uint8_t i = 0; i < 4; i++) {
        int bx = x + i * 5;
        int by = y + 16 - heights[i];
        if (i < bars) {
            tft.fillRect(bx, by, 3, heights[i], color);
        } else {
            tft.drawRect(bx, by, 3, heights[i], TFT_DARKGREY);
        }
    }
}

static void drawHeader(const char* title, const String& rightText,
                       const char* footer) {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 320, 28, TFT_WHITE);
    drawStringCustom(10, 7, title, TFT_BLACK, 2);
    if (rightText.length()) {
        drawStringRight(308, 9, rightText, TFT_BLACK, 1);
    }
    tft.drawFastHLine(0, 216, 320, TFT_WHITE);
    drawStringCustom(8, 224, footer, TFT_WHITE, 1);
}

static void drawScanScreen() {
    drawHeader("CHANNEL SCAN", "", "Scanning WiFi 2.4GHz...");
    drawStringCustom(38, 98, "SCANNING", TFT_WHITE, 3);
    drawStringFit(38, 132, "Counting networks per channel.",
                  TFT_CYAN, 250, 1);
}

static void clearScanData() {
    netCount = 0;
    for (uint8_t ch = 0; ch <= WIFI_CH_MAX; ch++) {
        channelCounts[ch] = 0;
        channelOpenCounts[ch] = 0;
        channelBestRssi[ch] = -127;
    }
}

static void sortByChannelAndSignal() {
    for (uint8_t i = 0; i + 1 < netCount; i++) {
        for (uint8_t j = 0; j + 1 < netCount - i; j++) {
            bool swapIt = false;
            if (channelNets[j].channel > channelNets[j + 1].channel) {
                swapIt = true;
            } else if (channelNets[j].channel == channelNets[j + 1].channel &&
                       channelNets[j].rssi < channelNets[j + 1].rssi) {
                swapIt = true;
            }
            if (swapIt) {
                ChannelNet tmp = channelNets[j];
                channelNets[j] = channelNets[j + 1];
                channelNets[j + 1] = tmp;
            }
        }
    }
}

static bool performChannelScan() {
    clearScanData();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(120);

    drawScanScreen();
    int found = WiFi.scanNetworks(false, true);
    if (found <= 0) {
        WiFi.scanDelete();
        drawHeader("CHANNEL SCAN", "", "OK:BACK");
        drawStringCustom(40, 100, "NO NETWORKS", TFT_RED, 2);
        while (digitalRead(BTN_OK) == HIGH) delay(20);
        while (digitalRead(BTN_OK) == LOW) delay(5);
        delay(80);
        return false;
    }

    if (found > MAX_CHANNEL_NETS) found = MAX_CHANNEL_NETS;
    for (int i = 0; i < found; i++) {
        int ch = WiFi.channel(i);
        if (ch < WIFI_CH_MIN || ch > WIFI_CH_MAX) continue;

        ChannelNet& net = channelNets[netCount++];
        net.ssid = WiFi.SSID(i);
        net.bssid = WiFi.BSSIDstr(i);
        net.channel = ch;
        net.rssi = WiFi.RSSI(i);
        net.auth = WiFi.encryptionType(i);

        channelCounts[ch]++;
        if (net.auth == WIFI_AUTH_OPEN) channelOpenCounts[ch]++;
        if (net.rssi > channelBestRssi[ch]) channelBestRssi[ch] = net.rssi;
    }

    WiFi.scanDelete();
    sortByChannelAndSignal();
    return netCount > 0;
}

static uint8_t maxChannelCount() {
    uint8_t maxCount = 1;
    for (uint8_t ch = WIFI_CH_MIN; ch <= WIFI_CH_MAX; ch++) {
        if (channelCounts[ch] > maxCount) maxCount = channelCounts[ch];
    }
    return maxCount;
}

static void drawChannelRow(uint8_t ch, uint8_t row, bool selected,
                           uint8_t maxCount) {
    if (ch > WIFI_CH_MAX) return;
    int y = 38 + row * 29;
    uint16_t bg = selected ? TFT_WHITE : TFT_BLACK;
    uint16_t fg = selected ? TFT_BLACK : TFT_WHITE;

    tft.fillRect(8, y - 4, 304, 25, bg);
    tft.drawRect(8, y - 4, 304, 25, TFT_DARKGREY);

    drawStringCustom(16, y, "CH " + String(ch), fg, 2);
    drawStringCustom(82, y + 2, String(channelCounts[ch]) + " nets", fg, 1);

    int barW = map(channelCounts[ch], 0, maxCount, 0, 112);
    uint16_t barColor = channelCounts[ch] == 0
                        ? TFT_DARKGREY
                        : rssiColor(channelBestRssi[ch]);
    tft.drawRect(148, y + 2, 114, 12, selected ? TFT_BLACK : TFT_DARKGREY);
    if (barW > 0) tft.fillRect(149, y + 3, barW, 10, barColor);

    if (channelCounts[ch] > 0) {
        drawStringRight(304, y + 2, String(channelBestRssi[ch]) + "dBm",
                        fg, 1);
    } else {
        drawStringRight(304, y + 2, "--", fg, 1);
    }

    if (channelOpenCounts[ch] > 0) {
        drawStringCustom(266, y + 14, "OP:" + String(channelOpenCounts[ch]),
                         selected ? TFT_BLACK : TFT_RED, 1);
    }
}

static void drawChannelListFrame(uint8_t cursor, uint8_t scrollOffset,
                                 bool fullRedraw = true) {
    if (fullRedraw) {
        drawHeader("CHANNELS", String(netCount) + " NETS",
                   "OK:OPEN  HOLD:BACK  UP/DN");
    }
    uint8_t maxCount = maxChannelCount();

    tft.startWrite();
    for (uint8_t row = 0; row < CHANNEL_VISIBLE; row++) {
        uint8_t ch = WIFI_CH_MIN + scrollOffset + row;
        drawChannelRow(ch, row, ch == cursor, maxCount);
    }
    tft.endWrite();
}

static uint8_t networksOnChannel(uint8_t channel) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < netCount; i++) {
        if (channelNets[i].channel == channel) count++;
    }
    return count;
}

static uint8_t networkIndexForChannel(uint8_t channel, uint8_t channelIndex) {
    uint8_t seen = 0;
    for (uint8_t i = 0; i < netCount; i++) {
        if (channelNets[i].channel != channel) continue;
        if (seen == channelIndex) return i;
        seen++;
    }
    return 0;
}

static void showNetworkDetails(const ChannelNet& net) {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 320, 32, TFT_WHITE);
    drawStringCustom(10, 8, "NETWORK DETAILS", TFT_BLACK, 2);

    int y = 44;
    bool hidden = net.ssid.length() == 0;
    String displaySsid = hidden ? "<HIDDEN>" : net.ssid;

    drawStringCustom(10, y, "SSID:", TFT_WHITE, 1);
    if (!hidden && getTextWidth(displaySsid, 2) > 300) {
        drawStringFit(10, y + 10, displaySsid, TFT_WHITE, 300, 1);
    } else {
        drawStringFit(10, y + 10, displaySsid,
                      hidden ? TFT_RED : TFT_WHITE, 300, 2);
    }
    y += 34;

    drawStringCustom(10, y, "CHANNEL:", TFT_WHITE, 1);
    drawStringCustom(10, y + 10,
                     "CH " + String(net.channel) + "  " +
                     String(channelToFreq(net.channel)) + " MHz",
                     TFT_WHITE, 2);
    y += 34;

    drawStringCustom(10, y, "SIGNAL:", TFT_WHITE, 1);
    drawStringCustom(10, y + 10, String(net.rssi) + " dBm", TFT_WHITE, 2);
    drawSignalBars(150, y + 10, rssiBars(net.rssi), rssiColor(net.rssi));
    y += 34;

    String vendor = lookupVendor(net.bssid);
    drawStringCustom(10, y, "BSSID:", TFT_WHITE, 1);
    drawStringCustom(10, y + 10, net.bssid, TFT_YELLOW, 1);
    drawStringCustom(180, y + 10, "(" + vendor + ")",
                     vendor == "Unknown" ? TFT_DARKGREY : TFT_CYAN, 1);
    y += 22;

    drawStringCustom(10, y, "SECURITY:", TFT_WHITE, 1);
    drawStringCustom(10, y + 10, authLong(net.auth), authColor(net.auth), 2);

    tft.drawFastHLine(0, 215, 320, TFT_WHITE);
    drawStringCustom(10, 222, "PRESS OK TO RETURN", TFT_WHITE, 2);

    delay(250);
    while (digitalRead(BTN_OK) == HIGH) delay(20);
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(80);
}

static void drawNetworkRow(uint8_t channel, uint8_t localIdx,
                           uint8_t row, bool selected) {
    uint8_t idx = networkIndexForChannel(channel, localIdx);
    const ChannelNet& net = channelNets[idx];
    int y = 38 + row * 29;
    uint16_t bg = selected ? TFT_WHITE : TFT_BLACK;
    uint16_t fg = selected ? TFT_BLACK : TFT_WHITE;

    tft.fillRect(8, y - 4, 304, 25, bg);
    tft.drawRect(8, y - 4, 304, 25, TFT_DARKGREY);
    drawSignalBars(14, y + 1, rssiBars(net.rssi),
                   selected ? TFT_BLACK : rssiColor(net.rssi));

    String label = net.ssid.length() ? net.ssid : "<HIDDEN>";
    uint16_t nameColor = net.ssid.length()
                         ? fg
                         : (selected ? TFT_BLACK : TFT_RED);
    drawStringFit(42, y, label, nameColor, 190, 1);
    drawStringCustom(238, y, String(net.rssi), fg, 1);
    drawStringCustom(276, y, authShort(net.auth),
                     selected ? TFT_BLACK : authColor(net.auth), 1);
    drawStringFit(42, y + 13, net.bssid,
                  selected ? TFT_BLACK : TFT_DARKGREY, 190, 1);
}

static void drawChannelNetworksFrame(uint8_t channel, uint8_t cursor,
                                     uint8_t scrollOffset,
                                     bool fullRedraw = true) {
    uint8_t count = networksOnChannel(channel);
    if (fullRedraw) {
        drawHeader(("CH " + String(channel)).c_str(), String(count) + " NETS",
                   "OK:DETAIL  HOLD:BACK");
    }

    if (count == 0) {
        drawStringCustom(38, 104, "NO NETWORKS", TFT_DARKGREY, 2);
        return;
    }

    tft.startWrite();
    for (uint8_t row = 0; row < NET_VISIBLE; row++) {
        uint8_t localIdx = scrollOffset + row;
        if (localIdx >= count) break;
        drawNetworkRow(channel, localIdx, row, localIdx == cursor);
    }
    tft.endWrite();
}

static void showChannelNetworks(uint8_t channel) {
    uint8_t count = networksOnChannel(channel);
    uint8_t cursor = 0;
    uint8_t scrollOffset = 0;
    drawChannelNetworksFrame(channel, cursor, scrollOffset);

    while (true) {
        if (digitalRead(BTN_OK) == LOW) {
            bool held = waitOkReleaseWasLong();
            delay(80);
            if (held || count == 0) return;

            uint8_t idx = networkIndexForChannel(channel, cursor);
            beep(1200, 45);
            showNetworkDetails(channelNets[idx]);
            drawChannelNetworksFrame(channel, cursor, scrollOffset);
            continue;
        }

        if (count > 0 && digitalRead(BTN_DOWN) == LOW) {
            uint8_t oldCursor = cursor;
            uint8_t oldScroll = scrollOffset;
            cursor = (cursor + 1) % count;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + NET_VISIBLE) {
                scrollOffset = cursor - NET_VISIBLE + 1;
            }
            beep(2000, 25);
            if (scrollOffset != oldScroll) {
                drawChannelNetworksFrame(channel, cursor, scrollOffset, false);
            } else {
                drawNetworkRow(channel, oldCursor, oldCursor - scrollOffset,
                               false);
                drawNetworkRow(channel, cursor, cursor - scrollOffset, true);
            }
            delay(160);
        }

        if (count > 0 && digitalRead(BTN_UP) == LOW) {
            uint8_t oldCursor = cursor;
            uint8_t oldScroll = scrollOffset;
            cursor = (cursor + count - 1) % count;
            if (cursor < scrollOffset) scrollOffset = cursor;
            if (cursor >= scrollOffset + NET_VISIBLE) {
                scrollOffset = cursor - NET_VISIBLE + 1;
            }
            beep(2000, 25);
            if (scrollOffset != oldScroll) {
                drawChannelNetworksFrame(channel, cursor, scrollOffset, false);
            } else {
                drawNetworkRow(channel, oldCursor, oldCursor - scrollOffset,
                               false);
                drawNetworkRow(channel, cursor, cursor - scrollOffset, true);
            }
            delay(160);
        }

        delay(10);
    }
}

void runWifiChannelScanner() {
    if (!performChannelScan()) return;

    uint8_t cursor = 1;
    uint8_t scrollOffset = 0;
    drawChannelListFrame(cursor, scrollOffset);

    while (true) {
        if (digitalRead(BTN_DOWN) == LOW) {
            uint8_t oldCursor = cursor;
            uint8_t oldScroll = scrollOffset;
            cursor = (cursor >= WIFI_CH_MAX) ? WIFI_CH_MIN : cursor + 1;
            if (cursor < WIFI_CH_MIN + scrollOffset) {
                scrollOffset = cursor - WIFI_CH_MIN;
            }
            if (cursor >= WIFI_CH_MIN + scrollOffset + CHANNEL_VISIBLE) {
                scrollOffset = cursor - WIFI_CH_MIN - CHANNEL_VISIBLE + 1;
            }
            beep(2000, 25);
            if (scrollOffset != oldScroll) {
                drawChannelListFrame(cursor, scrollOffset, false);
            } else {
                uint8_t maxCount = maxChannelCount();
                drawChannelRow(oldCursor, oldCursor - WIFI_CH_MIN - scrollOffset,
                               false, maxCount);
                drawChannelRow(cursor, cursor - WIFI_CH_MIN - scrollOffset,
                               true, maxCount);
            }
            delay(160);
        }

        if (digitalRead(BTN_UP) == LOW) {
            uint8_t oldCursor = cursor;
            uint8_t oldScroll = scrollOffset;
            cursor = (cursor <= WIFI_CH_MIN) ? WIFI_CH_MAX : cursor - 1;
            if (cursor < WIFI_CH_MIN + scrollOffset) {
                scrollOffset = cursor - WIFI_CH_MIN;
            }
            if (cursor >= WIFI_CH_MIN + scrollOffset + CHANNEL_VISIBLE) {
                scrollOffset = cursor - WIFI_CH_MIN - CHANNEL_VISIBLE + 1;
            }
            beep(2000, 25);
            if (scrollOffset != oldScroll) {
                drawChannelListFrame(cursor, scrollOffset, false);
            } else {
                uint8_t maxCount = maxChannelCount();
                drawChannelRow(oldCursor, oldCursor - WIFI_CH_MIN - scrollOffset,
                               false, maxCount);
                drawChannelRow(cursor, cursor - WIFI_CH_MIN - scrollOffset,
                               true, maxCount);
            }
            delay(160);
        }

        if (digitalRead(BTN_OK) == LOW) {
            bool held = waitOkReleaseWasLong();
            delay(80);
            if (held) return;
            beep(1200, 45);
            showChannelNetworks(cursor);
            drawChannelListFrame(cursor, scrollOffset);
        }

        delay(10);
    }
}
