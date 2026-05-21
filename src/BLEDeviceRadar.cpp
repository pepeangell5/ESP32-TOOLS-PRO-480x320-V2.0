#include "BLEDeviceRadar.h"

#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <WiFi.h>
#include <math.h>

#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"

static constexpr uint8_t BLE_RADAR_MAX_DEVICES = 24;
static constexpr uint8_t BLE_RADAR_VISIBLE = 6;
static constexpr uint8_t BLE_HISTORY_LEN = 36;
static constexpr uint16_t BLE_LIST_SCAN_SECONDS = 4;
static constexpr uint16_t BLE_TRACK_SCAN_SECONDS = 1;
static constexpr int BLE_LIST_X = 8;
static constexpr int BLE_LIST_Y = 58;
static constexpr int BLE_LIST_W = 304;
static constexpr int BLE_ROW_H = 24;

struct BleRadarDevice {
    String name;
    String label;
    String kind;
    String address;
    int rssi = -127;
    int bestRssi = -127;
    int8_t txPower = 0;
    uint8_t serviceCount = 0;
    uint16_t companyId = 0xFFFF;
    uint16_t appearance = 0;
    bool hasName = false;
    bool hasTxPower = false;
    bool hasManufacturer = false;
    bool hasAppearance = false;
};

static BleRadarDevice bleDevices[BLE_RADAR_MAX_DEVICES];
static uint8_t bleDeviceCount = 0;
static uint8_t bleSelected = 0;
static uint8_t bleScroll = 0;

static BleRadarDevice bleTarget;
static bool bleTargetSeen = false;
static int bleTargetRssi = -127;
static int bleLastTargetRssi = -127;
static int bleBestRssi = -127;
static int bleTrendDb = 0;
static int16_t bleHistory[BLE_HISTORY_LEN];
static uint8_t bleHistoryHead = 0;
static uint32_t bleScanPass = 0;

static String addressShort(const String& address) {
    if (address.length() <= 8) return address;
    return address.substring(address.length() - 8);
}

static uint16_t manufacturerId(BLEAdvertisedDevice& device) {
    if (!device.haveManufacturerData()) return 0xFFFF;
    const std::string data = device.getManufacturerData();
    if (data.length() < 2) return 0xFFFF;
    return (uint8_t)data[0] | ((uint16_t)(uint8_t)data[1] << 8);
}

static String companyLabel(uint16_t companyId) {
    switch (companyId) {
        case 0x004C: return "Apple BLE";
        case 0x0006: return "Microsoft BLE";
        case 0x0075: return "Samsung BLE";
        case 0x00E0: return "Google BLE";
        case 0x0059: return "Nordic BLE";
        case 0x000F: return "Broadcom BLE";
        case 0x00D2: return "Sony BLE";
        case 0x0131: return "Xiaomi BLE";
        case 0x038F: return "Meta BLE";
        case 0x0499: return "Espressif BLE";
        case 0x0157: return "Fitbit BLE";
        case 0x0087: return "Garmin BLE";
        case 0x0505: return "Bose BLE";
        case 0x01DA: return "Logitech BLE";
        default: break;
    }
    char label[12];
    snprintf(label, sizeof(label), "MFG %04X", companyId);
    return String(label);
}

static String serviceLabel(BLEAdvertisedDevice& device) {
    for (int i = 0; i < device.getServiceUUIDCount(); i++) {
        String uuid = String(device.getServiceUUID(i).toString().c_str());
        uuid.toLowerCase();
        if (uuid.indexOf("1812") >= 0) return "HID Device";
        if (uuid.indexOf("180f") >= 0) return "Battery BLE";
        if (uuid.indexOf("180d") >= 0) return "Heart Rate";
        if (uuid.indexOf("180a") >= 0) return "Device Info";
        if (uuid.indexOf("1809") >= 0) return "Thermo BLE";
        if (uuid.indexOf("181a") >= 0) return "Env Sensor";
        if (uuid.indexOf("1816") >= 0) return "Cycling BLE";
        if (uuid.indexOf("1814") >= 0) return "Phone Alert";
        if (uuid.indexOf("feaa") >= 0) return "Beacon BLE";
    }
    return "";
}

static String appearanceLabel(uint16_t appearance) {
    const uint16_t category = appearance >> 6;
    switch (category) {
        case 1: return "Phone BLE";
        case 2: return "Computer BLE";
        case 3: return "Watch BLE";
        case 5: return "Display BLE";
        case 10: return "Tag BLE";
        case 15: return "HID BLE";
        case 49: return "Sensor BLE";
        default: break;
    }
    return "";
}

static String buildLabel(BLEAdvertisedDevice& device, const String& address) {
    if (device.haveName()) {
        String name = String(device.getName().c_str());
        name.trim();
        if (name.length()) return name;
    }

    const uint16_t companyId = manufacturerId(device);
    if (companyId != 0xFFFF) return companyLabel(companyId);

    const String service = serviceLabel(device);
    if (service.length()) return service;

    if (device.haveAppearance()) {
        const String appearance = appearanceLabel(device.getAppearance());
        if (appearance.length()) return appearance;
    }

    return String("BLE ") + addressShort(address);
}

static String kindText(const BleRadarDevice& device) {
    if (device.hasName) return "nombre adv";
    if (device.hasManufacturer) return device.kind;
    if (device.serviceCount) return "servicio BLE";
    if (device.hasAppearance) return "apariencia";
    return "direccion";
}

static uint8_t rssiPct(int rssi) {
    if (rssi <= -100) return 0;
    if (rssi >= -35) return 100;
    return (uint8_t)map(rssi, -100, -35, 0, 100);
}

static uint16_t estimateMeters(int rssi) {
    if (rssi < -120) return 0;
    const float rssiAtOneMeter = -59.0f;
    const float pathLoss = 2.3f;
    float meters = powf(10.0f, (rssiAtOneMeter - (float)rssi) /
                              (10.0f * pathLoss));
    if (meters < 1.0f) meters = 1.0f;
    if (meters > 99.0f) meters = 99.0f;
    return (uint16_t)roundf(meters);
}

static uint16_t proximityColor(uint8_t pct) {
    if (!bleTargetSeen) return TFT_RED;
    if (pct >= 70) return TFT_GREEN;
    if (pct >= 38) return TFT_YELLOW;
    return TFT_RED;
}

static String proximityText(uint8_t pct) {
    if (!bleTargetSeen) return "BUSCANDO";
    if (pct >= 70) return "CERCA";
    if (pct >= 38) return "MEDIA";
    return "LEJOS";
}

static String trendText() {
    if (!bleTargetSeen) return "BUSCANDO";
    if (bleTrendDb >= 4) return "ACERCANDO";
    if (bleTrendDb <= -4) return "ALEJANDO";
    return "ESTABLE";
}

static uint16_t trendColor() {
    if (!bleTargetSeen) return TFT_RED;
    if (bleTrendDb >= 4) return TFT_GREEN;
    if (bleTrendDb <= -4) return TFT_YELLOW;
    return TFT_CYAN;
}

static void drawFrame(const char* title, const char* footer) {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    tft.drawFastHLine(0, 32, 320, TFT_WHITE);
    tft.drawFastHLine(0, 216, 320, TFT_WHITE);
    drawStringBig(10, 8, title, TFT_WHITE, 1);
    drawStringCustom(10, 224, footer, TFT_WHITE, 1);
}

static void drawHorizontalBar(int x, int y, int w, int h, uint8_t pct,
                              uint16_t color) {
    pct = constrain(pct, 0, 100);
    tft.drawRect(x, y, w, h, TFT_DARKGREY);
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
    int fillW = ((w - 2) * pct) / 100;
    if (fillW > 0) tft.fillRect(x + 1, y + 1, fillW, h - 2, color);
}

static bool prepareBleScan() {
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    BLEDevice::init("");
    BLEScan* scan = BLEDevice::getScan();
    if (!scan) return false;
    scan->setActiveScan(true);
    scan->setInterval(96);
    scan->setWindow(64);
    return true;
}

static void drawScanning(const String& line) {
    drawFrame("BLE DEVICE RADAR", "OK-HOLD:BACK");
    tft.drawRect(18, 58, 284, 96, TFT_CYAN);
    drawStringFit(32, 78, line, TFT_CYAN, 256, 1);
    drawStringFit(32, 104, "Escucha anuncios BLE cercanos.",
                  TFT_WHITE, 256, 1);
    drawStringFit(32, 128, "No conecta ni empareja.",
                  TFT_YELLOW, 256, 1);
}

static void sortDevices() {
    for (uint8_t i = 0; i < bleDeviceCount; i++) {
        for (uint8_t j = i + 1; j < bleDeviceCount; j++) {
            if (bleDevices[j].rssi > bleDevices[i].rssi) {
                BleRadarDevice tmp = bleDevices[i];
                bleDevices[i] = bleDevices[j];
                bleDevices[j] = tmp;
            }
        }
    }
}

static bool scanDevices() {
    bleDeviceCount = 0;
    if (!prepareBleScan()) return false;

    drawScanning("Escaneando dispositivos BLE...");
    BLEScan* scan = BLEDevice::getScan();
    BLEScanResults results = scan->start(BLE_LIST_SCAN_SECONDS, false);
    int count = results.getCount();
    if (count > BLE_RADAR_MAX_DEVICES) count = BLE_RADAR_MAX_DEVICES;

    for (uint8_t i = 0; i < count; i++) {
        BLEAdvertisedDevice d = results.getDevice(i);
        BleRadarDevice& out = bleDevices[bleDeviceCount++];
        out.name = d.haveName() ? String(d.getName().c_str()) : "";
        out.address = String(d.getAddress().toString().c_str());
        out.companyId = manufacturerId(d);
        out.label = buildLabel(d, out.address);
        out.kind = out.companyId != 0xFFFF ? companyLabel(out.companyId)
                                           : serviceLabel(d);
        out.rssi = d.getRSSI();
        out.bestRssi = d.getRSSI();
        out.hasName = d.haveName();
        out.hasTxPower = d.haveTXPower();
        out.txPower = d.haveTXPower() ? d.getTXPower() : 0;
        out.serviceCount = d.getServiceUUIDCount();
        out.appearance = d.haveAppearance() ? d.getAppearance() : 0;
        out.hasManufacturer = out.companyId != 0xFFFF;
        out.hasAppearance = d.haveAppearance();
    }

    scan->clearResults();
    sortDevices();
    bleSelected = 0;
    bleScroll = 0;
    return true;
}

static void drawDeviceRow(uint8_t row) {
    int y = BLE_LIST_Y + row * BLE_ROW_H;
    tft.fillRect(BLE_LIST_X, y - 2, BLE_LIST_W, BLE_ROW_H - 2, TFT_BLACK);

    uint8_t idx = bleScroll + row;
    if (idx >= bleDeviceCount) {
        return;
    }

    bool selected = idx == bleSelected;
    uint16_t bg = selected ? TFT_CYAN : TFT_BLACK;
    uint16_t fg = selected ? TFT_BLACK : TFT_WHITE;
    uint16_t sub = selected ? TFT_BLACK : TFT_DARKGREY;

    tft.fillRect(BLE_LIST_X, y - 2, BLE_LIST_W, BLE_ROW_H - 2, bg);
    tft.drawRect(BLE_LIST_X, y - 2, BLE_LIST_W, BLE_ROW_H - 2,
                 selected ? TFT_WHITE : TFT_DARKGREY);

    drawStringFit(16, y, bleDevices[idx].label, fg, 168, 1);
    drawStringRight(304, y, String(bleDevices[idx].rssi) + "dBm",
                    bleDevices[idx].rssi > -65 && !selected
                        ? TFT_GREEN
                        : fg,
                    1);
    drawStringFit(16, y + 12, kindText(bleDevices[idx]) + "  " +
                  addressShort(bleDevices[idx].address), sub, 238, 1);
    drawStringRight(304, y + 12,
                    "SVC" + String(bleDevices[idx].serviceCount),
                    sub, 1);
}

static void drawVisibleDeviceRows() {
    for (uint8_t row = 0; row < BLE_RADAR_VISIBLE; row++) {
        drawDeviceRow(row);
    }
}

static void drawSelectedDeviceFooter() {
    tft.fillRect(12, 200, 296, 12, TFT_BLACK);
    if (bleDeviceCount == 0) return;
    const BleRadarDevice& active = bleDevices[bleSelected];
    drawStringFit(12, 200, active.address +
                  (active.hasTxPower ? String(" TX ") + active.txPower : ""),
                  TFT_DARKGREY, 296, 1);
}

static void drawDeviceList() {
    drawFrame("BLE DEVICE RADAR", "UP/DN MOVE  OK:RADAR  HOLD:BACK");
    drawStringCustom(12, 40, "Devices: " + String(bleDeviceCount),
                     TFT_CYAN, 1);

    if (bleDeviceCount == 0) {
        drawStringCustom(40, 104, "NO BLE DEVICES", TFT_YELLOW, 2);
        drawStringFit(40, 134, "Tap OK to rescan.",
                      TFT_WHITE, 240, 1);
        return;
    }

    drawVisibleDeviceRows();
    drawSelectedDeviceFooter();
}

static void updateDeviceListSelection(uint8_t oldSelected,
                                      uint8_t oldScroll) {
    if (oldScroll != bleScroll) {
        drawVisibleDeviceRows();
    } else {
        if (oldSelected >= oldScroll &&
            oldSelected < oldScroll + BLE_RADAR_VISIBLE) {
            drawDeviceRow(oldSelected - oldScroll);
        }
        if (bleSelected >= bleScroll &&
            bleSelected < bleScroll + BLE_RADAR_VISIBLE) {
            drawDeviceRow(bleSelected - bleScroll);
        }
    }
    drawSelectedDeviceFooter();
}

static bool scanTargetOnce(int& rssiOut) {
    if (!prepareBleScan()) return false;
    BLEScan* scan = BLEDevice::getScan();
    BLEScanResults results = scan->start(BLE_TRACK_SCAN_SECONDS, false);
    bool found = false;
    int best = -127;

    for (int i = 0; i < results.getCount(); i++) {
        BLEAdvertisedDevice d = results.getDevice(i);
        String address = String(d.getAddress().toString().c_str());
        if (address.equalsIgnoreCase(bleTarget.address)) {
            best = d.getRSSI();
            found = true;
            break;
        }
    }

    scan->clearResults();
    rssiOut = best;
    return found;
}

static void rememberHistory(int rssi) {
    bleHistory[bleHistoryHead] = (int16_t)rssi;
    bleHistoryHead = (bleHistoryHead + 1) % BLE_HISTORY_LEN;
}

static void resetTargetState() {
    bleTargetSeen = false;
    bleTargetRssi = -127;
    bleLastTargetRssi = -127;
    bleBestRssi = -127;
    bleTrendDb = 0;
    bleHistoryHead = 0;
    bleScanPass = 0;
    memset(bleHistory, 0, sizeof(bleHistory));
}

static void updateTargetRssi(bool drawWait) {
    if (drawWait) drawScanning("Rastreando dispositivo BLE...");
    int rssi = -127;
    bool found = scanTargetOnce(rssi);
    bleScanPass++;
    bleLastTargetRssi = bleTargetRssi;
    bleTargetSeen = found;

    if (found) {
        bleTargetRssi = rssi;
        if (bleBestRssi < -120 || rssi > bleBestRssi) bleBestRssi = rssi;
        bleTrendDb = bleLastTargetRssi < -120 ? 0 : rssi - bleLastTargetRssi;
        rememberHistory(rssi);
    } else {
        bleTargetRssi = -127;
        bleTrendDb = -8;
        rememberHistory(-100);
    }
}

static void drawHistory(int x, int y, int w, int h) {
    tft.drawRect(x, y, w, h, TFT_DARKGREY);
    for (uint8_t i = 0; i < BLE_HISTORY_LEN; i++) {
        uint8_t idx = (bleHistoryHead + i) % BLE_HISTORY_LEN;
        if (bleHistory[idx] == 0) continue;
        uint8_t pct = rssiPct(bleHistory[idx]);
        int px = x + 2 + (i * (w - 4)) / BLE_HISTORY_LEN;
        int barH = max(1, (pct * (h - 4)) / 100);
        uint16_t color = pct > 70 ? TFT_GREEN :
                         (pct > 38 ? TFT_YELLOW : TFT_RED);
        tft.drawFastVLine(px, y + h - 2 - barH, barH, color);
    }
}

static void drawRadarScreen() {
    drawFrame("BLE PULSE", "OK:RESCAN  HOLD:LIST");

    uint8_t pct = bleTargetSeen ? rssiPct(bleTargetRssi) : 0;
    uint16_t color = proximityColor(pct);
    uint32_t now = millis();

    tft.drawRect(14, 44, 128, 136, TFT_CYAN);
    drawStringCustom(24, 54, proximityText(pct), color, 1);

    int iconX = 58;
    int iconY = 116;
    for (uint8_t wave = 0; wave < 3; wave++) {
        int radius = 18 + ((now / 120 + wave * 10) % 34);
        tft.drawCircle(iconX, iconY, radius,
                       bleTargetSeen ? (wave == 0 ? color : TFT_DARKGREY)
                                     : TFT_DARKGREY);
    }
    tft.fillCircle(iconX, iconY, 8, color);
    tft.drawCircle(iconX, iconY, 13, TFT_WHITE);

    int trackX = 112;
    int trackTop = 68;
    int trackH = 88;
    int fillH = (pct * trackH) / 100;
    tft.drawRect(trackX - 8, trackTop, 16, trackH, TFT_DARKGREY);
    if (fillH > 0) {
        tft.fillRect(trackX - 7, trackTop + trackH - fillH,
                     14, fillH, color);
    }
    int markerY = trackTop + trackH - fillH;
    tft.fillCircle(trackX, markerY, 5, TFT_WHITE);

    tft.drawRect(154, 42, 154, 86, TFT_DARKGREY);
    drawStringFit(164, 52, bleTarget.label, TFT_GREEN, 134, 1);
    drawStringCustom(164, 70,
                     String(bleTargetRssi) + " dBm  " + String(pct) + "%",
                     bleTargetSeen ? TFT_CYAN : TFT_RED, 1);
    drawStringCustom(164, 88, "Peak " + String(bleBestRssi) + " dBm",
                     bleBestRssi > -127 ? TFT_GREEN : TFT_DARKGREY, 1);
    String meters = bleTargetSeen ? String(estimateMeters(bleTargetRssi)) + " m"
                                  : "-- m";
    drawStringCustom(164, 106, "Dist ~ " + meters, TFT_YELLOW, 1);

    tft.drawRect(154, 138, 154, 62, TFT_DARKGREY);
    drawStringCustom(164, 148, trendText(), trendColor(), 1);
    drawStringCustom(164, 166, "Scan " + String(bleScanPass) + " adv",
                     TFT_DARKGREY, 1);
    drawHorizontalBar(164, 184, 132, 8, pct, color);

    drawHistory(18, 188, 124, 22);
}

static void runTargetRadar() {
    resetTargetState();
    updateTargetRssi(true);
    drawRadarScreen();

    uint32_t lastScan = millis();

    while (true) {
        if (digitalRead(BTN_OK) == LOW) {
            bool held = waitOkReleaseWasLong();
            delay(80);
            if (held) return;
            updateTargetRssi(true);
            drawRadarScreen();
            lastScan = millis();
        }

        if (millis() - lastScan > 1400) {
            updateTargetRssi(false);
            drawRadarScreen();
            lastScan = millis();
        }

        delay(10);
    }
}

void runBLEDeviceRadar() {
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);

    scanDevices();
    drawDeviceList();
    beep(1500, 30);

    while (true) {
        if (digitalRead(BTN_DOWN) == LOW) {
            if (bleDeviceCount > 0) {
                uint8_t oldSelected = bleSelected;
                uint8_t oldScroll = bleScroll;
                bleSelected = (bleSelected + 1) % bleDeviceCount;
                if (bleSelected < bleScroll) bleScroll = bleSelected;
                if (bleSelected >= bleScroll + BLE_RADAR_VISIBLE) {
                    bleScroll = bleSelected - BLE_RADAR_VISIBLE + 1;
                }
                beep(2200, 20);
                updateDeviceListSelection(oldSelected, oldScroll);
            }
            delay(170);
        }

        if (digitalRead(BTN_UP) == LOW) {
            if (bleDeviceCount > 0) {
                uint8_t oldSelected = bleSelected;
                uint8_t oldScroll = bleScroll;
                bleSelected = bleSelected == 0 ? bleDeviceCount - 1
                                               : bleSelected - 1;
                if (bleSelected < bleScroll) bleScroll = bleSelected;
                if (bleSelected >= bleScroll + BLE_RADAR_VISIBLE) {
                    bleScroll = bleSelected - BLE_RADAR_VISIBLE + 1;
                }
                beep(2200, 20);
                updateDeviceListSelection(oldSelected, oldScroll);
            }
            delay(170);
        }

        if (digitalRead(BTN_OK) == LOW) {
            bool held = waitOkReleaseWasLong();
            delay(80);
            if (held) break;

            if (bleDeviceCount == 0) {
                scanDevices();
                drawDeviceList();
                continue;
            }

            bleTarget = bleDevices[bleSelected];
            beep(3200, 20);
            runTargetRadar();
            drawDeviceList();
        }

        delay(10);
    }

    BLEScan* scan = BLEDevice::getScan();
    if (scan) {
        scan->stop();
        scan->clearResults();
    }
    BLEDevice::deinit(false);
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);
}
