#include "BLEInspector.h"

#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <WiFi.h>

#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"

static constexpr uint8_t BLE_INSPECT_MAX_DEVICES = 28;
static constexpr uint8_t BLE_INSPECT_VISIBLE = 6;
static constexpr uint16_t BLE_INSPECT_SCAN_SECONDS = 4;
static constexpr int BLE_INSPECT_LIST_X = 8;
static constexpr int BLE_INSPECT_LIST_Y = 56;
static constexpr int BLE_INSPECT_LIST_W = 304;
static constexpr int BLE_INSPECT_ROW_H = 24;

struct BleInspectDevice {
    String name;
    String label;
    String kind;
    String address;
    String manufacturer;
    String services;
    String mfgHex;
    int rssi = -127;
    int addrType = 0;
    int8_t txPower = 0;
    uint8_t serviceCount = 0;
    uint16_t companyId = 0xFFFF;
    uint16_t appearance = 0;
    bool hasName = false;
    bool hasManufacturer = false;
    bool hasTxPower = false;
    bool hasAppearance = false;
};

static BleInspectDevice bleInspectDevices[BLE_INSPECT_MAX_DEVICES];
static uint8_t bleInspectCount = 0;
static uint8_t bleInspectSelected = 0;
static uint8_t bleInspectScroll = 0;

static String shortAddress(const String& address) {
    if (address.length() <= 8) return address;
    return address.substring(address.length() - 8);
}

static String addrTypeText(int type) {
    switch (type) {
        case 0: return "Public";
        case 1: return "Random";
        case 2: return "Public ID";
        case 3: return "Random ID";
        default: return "Unknown";
    }
}

static uint16_t extractCompanyId(BLEAdvertisedDevice& device) {
    if (!device.haveManufacturerData()) return 0xFFFF;
    std::string data = device.getManufacturerData();
    if (data.length() < 2) return 0xFFFF;
    return (uint8_t)data[0] | ((uint16_t)(uint8_t)data[1] << 8);
}

static String companyName(uint16_t id) {
    switch (id) {
        case 0x004C: return "Apple";
        case 0x0006: return "Microsoft";
        case 0x0075: return "Samsung";
        case 0x00E0: return "Google";
        case 0x0059: return "Nordic";
        case 0x000F: return "Broadcom";
        case 0x00D2: return "Sony";
        case 0x0131: return "Xiaomi";
        case 0x038F: return "Meta";
        case 0x0499: return "Espressif";
        case 0x0157: return "Fitbit";
        case 0x0087: return "Garmin";
        case 0x0505: return "Bose";
        case 0x01DA: return "Logitech";
        case 0x022B: return "Tile";
        case 0x02E1: return "Amazon";
        case 0x0154: return "Withings";
        case 0x004F: return "Nokia";
        default: break;
    }

    char label[14];
    snprintf(label, sizeof(label), "ID %04X", id);
    return String(label);
}

static String hexData(const std::string& data) {
    String out;
    size_t show = data.length() < 12 ? data.length() : 12;
    for (size_t i = 0; i < show; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X ", (uint8_t)data[i]);
        out += b;
    }
    if (data.length() > show) out += "...";
    return out.length() ? out : "--";
}

static String serviceName(const String& uuid) {
    String u = uuid;
    u.toLowerCase();
    if (u.indexOf("1800") >= 0) return "GAP";
    if (u.indexOf("1801") >= 0) return "GATT";
    if (u.indexOf("180a") >= 0) return "Device Info";
    if (u.indexOf("180f") >= 0) return "Battery";
    if (u.indexOf("1812") >= 0) return "HID";
    if (u.indexOf("180d") >= 0) return "Heart Rate";
    if (u.indexOf("1816") >= 0) return "Cycling";
    if (u.indexOf("181a") >= 0) return "Environment";
    if (u.indexOf("181c") >= 0) return "User Data";
    if (u.indexOf("1826") >= 0) return "Fitness";
    if (u.indexOf("fe9f") >= 0) return "Google";
    if (u.indexOf("fd6f") >= 0) return "Exposure";
    if (u.indexOf("fd5a") >= 0) return "GoPro";
    if (u.length() > 8) return u.substring(0, 8);
    return u;
}

static String appearanceText(uint16_t appearance) {
    switch (appearance) {
        case 0: return "--";
        case 64: return "Phone";
        case 128: return "Computer";
        case 192: return "Watch";
        case 320: return "Display";
        case 512: return "Remote";
        case 768: return "Thermometer";
        case 832: return "Heart Sensor";
        case 833: return "Heart Belt";
        case 960: return "Keyboard";
        case 961: return "Mouse";
        case 962: return "Joystick";
        case 963: return "Gamepad";
        default: break;
    }
    return "0x" + String(appearance, HEX);
}

static String servicesSummary(BLEAdvertisedDevice& device) {
    if (device.getServiceUUIDCount() == 0) return "--";

    String out;
    int show = min(device.getServiceUUIDCount(), 3);
    for (int i = 0; i < show; i++) {
        if (i > 0) out += ", ";
        out += serviceName(String(device.getServiceUUID(i).toString().c_str()));
    }
    if (device.getServiceUUIDCount() > show) out += " +";
    return out;
}

static String kindFrom(BLEAdvertisedDevice& device, uint16_t companyId,
                       uint16_t appearance) {
    for (int i = 0; i < device.getServiceUUIDCount(); i++) {
        String svc = serviceName(String(device.getServiceUUID(i).toString().c_str()));
        if (svc == "HID") return "Input / HID";
        if (svc == "Battery") return "Battery device";
        if (svc == "Heart Rate") return "Health sensor";
        if (svc == "Fitness") return "Fitness";
        if (svc == "Environment") return "Sensor";
        if (svc == "Google") return "Google beacon";
        if (svc == "Exposure") return "Exposure tag";
    }

    String app = appearanceText(appearance);
    if (app != "--" && !app.startsWith("0x")) return app;
    if (companyId != 0xFFFF) return companyName(companyId) + " BLE";
    return "BLE device";
}

static uint8_t rssiPercent(int rssi) {
    if (rssi <= -100) return 0;
    if (rssi >= -35) return 100;
    return (uint8_t)(((rssi + 100) * 100) / 65);
}

static String rssiText(int rssi) {
    if (rssi >= -50) return "VERY CLOSE";
    if (rssi >= -65) return "CLOSE";
    if (rssi >= -80) return "NEAR";
    return "FAR";
}

static uint16_t rssiColor(int rssi) {
    if (rssi >= -55) return TFT_GREEN;
    if (rssi >= -75) return TFT_YELLOW;
    return TFT_RED;
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

static bool prepareScan() {
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

static void drawScanning() {
    drawFrame("BLE INSPECTOR", "Scanning...");
    tft.drawRect(18, 58, 284, 96, TFT_CYAN);
    drawStringFit(32, 78, "Escaneando anuncios BLE cercanos.",
                  TFT_CYAN, 256, 1);
    drawStringFit(32, 104, "Busca nombre, fabricante y servicios.",
                  TFT_WHITE, 256, 1);
    drawStringFit(32, 128, "No conecta ni empareja.",
                  TFT_YELLOW, 256, 1);
}

static int findByAddress(const String& address) {
    for (uint8_t i = 0; i < bleInspectCount; i++) {
        if (bleInspectDevices[i].address.equalsIgnoreCase(address)) return i;
    }
    return -1;
}

static void sortDevices() {
    for (uint8_t i = 0; i < bleInspectCount; i++) {
        for (uint8_t j = i + 1; j < bleInspectCount; j++) {
            if (bleInspectDevices[j].rssi > bleInspectDevices[i].rssi) {
                BleInspectDevice temp = bleInspectDevices[i];
                bleInspectDevices[i] = bleInspectDevices[j];
                bleInspectDevices[j] = temp;
            }
        }
    }
}

static void storeDevice(BLEAdvertisedDevice& device) {
    String address = String(device.getAddress().toString().c_str());
    int idx = findByAddress(address);
    if (idx < 0) {
        if (bleInspectCount >= BLE_INSPECT_MAX_DEVICES) return;
        idx = bleInspectCount++;
    }

    BleInspectDevice& out = bleInspectDevices[idx];
    out.name = device.haveName() ? String(device.getName().c_str()) : "";
    out.address = address;
    out.rssi = device.getRSSI();
    out.addrType = (int)device.getAddressType();
    out.companyId = extractCompanyId(device);
    out.hasManufacturer = out.companyId != 0xFFFF;
    out.manufacturer = out.hasManufacturer ? companyName(out.companyId) : "--";
    out.hasName = device.haveName();
    out.hasTxPower = device.haveTXPower();
    out.txPower = out.hasTxPower ? device.getTXPower() : 0;
    out.hasAppearance = device.haveAppearance();
    out.appearance = out.hasAppearance ? device.getAppearance() : 0;
    out.serviceCount = device.getServiceUUIDCount();
    out.services = servicesSummary(device);
    out.kind = kindFrom(device, out.companyId, out.appearance);

    if (device.haveManufacturerData()) {
        out.mfgHex = hexData(device.getManufacturerData());
    } else {
        out.mfgHex = "--";
    }

    if (out.name.length() > 0) out.label = out.name;
    else if (out.kind.length() > 0) out.label = out.kind;
    else out.label = "BLE " + shortAddress(out.address);
}

static bool scanDevices() {
    drawScanning();
    bleInspectCount = 0;
    bleInspectSelected = 0;
    bleInspectScroll = 0;

    if (!prepareScan()) return false;
    BLEScan* scan = BLEDevice::getScan();
    BLEScanResults results = scan->start(BLE_INSPECT_SCAN_SECONDS, false);
    for (int i = 0; i < results.getCount(); i++) {
        BLEAdvertisedDevice device = results.getDevice(i);
        storeDevice(device);
    }

    scan->clearResults();
    sortDevices();
    return true;
}

static void drawDeviceRow(uint8_t row) {
    int y = BLE_INSPECT_LIST_Y + row * BLE_INSPECT_ROW_H;
    tft.fillRect(BLE_INSPECT_LIST_X, y - 2, BLE_INSPECT_LIST_W,
                 BLE_INSPECT_ROW_H - 2, TFT_BLACK);

    uint8_t idx = bleInspectScroll + row;
    if (idx >= bleInspectCount) return;

    const BleInspectDevice& d = bleInspectDevices[idx];
    bool selected = idx == bleInspectSelected;
    uint16_t bg = selected ? TFT_CYAN : TFT_BLACK;
    uint16_t fg = selected ? TFT_BLACK : TFT_WHITE;
    uint16_t sub = selected ? TFT_BLACK : TFT_DARKGREY;
    uint16_t rssiCol = selected ? TFT_BLACK : rssiColor(d.rssi);

    tft.fillRect(BLE_INSPECT_LIST_X, y - 2, BLE_INSPECT_LIST_W,
                 BLE_INSPECT_ROW_H - 2, bg);
    tft.drawRect(BLE_INSPECT_LIST_X, y - 2, BLE_INSPECT_LIST_W,
                 BLE_INSPECT_ROW_H - 2, selected ? TFT_WHITE : TFT_DARKGREY);

    drawStringFit(16, y, d.label, fg, 166, 1);
    drawStringRight(304, y, String(d.rssi) + "dBm", rssiCol, 1);
    drawStringFit(16, y + 12, d.kind + "  " + shortAddress(d.address),
                  sub, 226, 1);
    drawStringRight(304, y + 12, "SVC" + String(d.serviceCount), sub, 1);
}

static void drawVisibleRows() {
    for (uint8_t row = 0; row < BLE_INSPECT_VISIBLE; row++) {
        drawDeviceRow(row);
    }
}

static void drawSelectedFooter() {
    tft.fillRect(12, 200, 296, 12, TFT_BLACK);
    if (bleInspectCount == 0) return;
    const BleInspectDevice& d = bleInspectDevices[bleInspectSelected];
    drawStringFit(12, 200, d.manufacturer + "  " + d.services,
                  TFT_DARKGREY, 296, 1);
}

static void drawDeviceList() {
    drawFrame("BLE INSPECTOR", "UP/DN MOVE  OK:DETAIL  HOLD:BACK");
    drawStringCustom(12, 40, "Devices: " + String(bleInspectCount),
                     TFT_CYAN, 1);

    if (bleInspectCount == 0) {
        drawStringCustom(40, 104, "NO BLE DEVICES", TFT_YELLOW, 2);
        drawStringFit(40, 134, "Tap OK to scan again.",
                      TFT_WHITE, 240, 1);
        return;
    }

    drawVisibleRows();
    drawSelectedFooter();
}

static void updateSelection(uint8_t oldSelected, uint8_t oldScroll) {
    if (oldScroll != bleInspectScroll) {
        drawVisibleRows();
    } else {
        if (oldSelected >= bleInspectScroll &&
            oldSelected < bleInspectScroll + BLE_INSPECT_VISIBLE) {
            drawDeviceRow(oldSelected - bleInspectScroll);
        }
        if (bleInspectSelected >= bleInspectScroll &&
            bleInspectSelected < bleInspectScroll + BLE_INSPECT_VISIBLE) {
            drawDeviceRow(bleInspectSelected - bleInspectScroll);
        }
    }
    drawSelectedFooter();
}

static void drawLine(int x, int& y, const String& label,
                     const String& value, uint16_t color = TFT_WHITE) {
    drawStringCustom(x, y, label, TFT_DARKGREY, 1);
    drawStringFit(x + 78, y, value, color, 218, 1);
    y += 14;
}

static void drawDetails(const BleInspectDevice& d) {
    drawFrame("BLE DETAILS", "OK:LIST");

    int y = 42;
    drawStringFit(12, y, d.label, TFT_CYAN, 296, 1);
    y += 18;

    uint8_t pct = rssiPercent(d.rssi);
    uint16_t col = rssiColor(d.rssi);
    drawStringCustom(12, y, String(d.rssi) + " dBm", col, 2);
    drawStringRight(304, y + 4, rssiText(d.rssi), col, 1);
    y += 22;
    drawBar(12, y, 296, 8, pct, col);
    y += 18;

    drawLine(12, y, "MAC", d.address);
    drawLine(12, y, "Type", d.kind, TFT_CYAN);
    drawLine(12, y, "Mfg", d.manufacturer,
             d.hasManufacturer ? TFT_YELLOW : TFT_DARKGREY);

    String company = "--";
    if (d.hasManufacturer) {
        company = "0x";
        company += String(d.companyId, HEX);
    }
    company.toUpperCase();
    drawLine(12, y, "Company", company);
    drawLine(12, y, "Services", String(d.serviceCount) + "  " + d.services);
    drawLine(12, y, "Appear", appearanceText(d.appearance));
    drawLine(12, y, "Addr", addrTypeText(d.addrType));
    String txPower = "--";
    if (d.hasTxPower) txPower = String(d.txPower) + " dBm";
    drawLine(12, y, "TX Power", txPower);

    y += 4;
    tft.drawFastHLine(12, y, 296, TFT_DARKGREY);
    y += 8;
    drawStringCustom(12, y, "Manufacturer Data", TFT_DARKGREY, 1);
    y += 14;
    drawStringFit(12, y, d.mfgHex, TFT_WHITE, 296, 1);
}

void runBLEInspector() {
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);

    scanDevices();
    drawDeviceList();
    beep(1500, 30);

    while (true) {
        if (digitalRead(BTN_DOWN) == LOW) {
            if (bleInspectCount > 0) {
                uint8_t oldSelected = bleInspectSelected;
                uint8_t oldScroll = bleInspectScroll;
                bleInspectSelected = (bleInspectSelected + 1) % bleInspectCount;
                if (bleInspectSelected < bleInspectScroll) {
                    bleInspectScroll = bleInspectSelected;
                }
                if (bleInspectSelected >= bleInspectScroll + BLE_INSPECT_VISIBLE) {
                    bleInspectScroll = bleInspectSelected - BLE_INSPECT_VISIBLE + 1;
                }
                beep(2200, 20);
                updateSelection(oldSelected, oldScroll);
            }
            delay(170);
        }

        if (digitalRead(BTN_UP) == LOW) {
            if (bleInspectCount > 0) {
                uint8_t oldSelected = bleInspectSelected;
                uint8_t oldScroll = bleInspectScroll;
                bleInspectSelected = bleInspectSelected == 0
                    ? bleInspectCount - 1
                    : bleInspectSelected - 1;
                if (bleInspectSelected < bleInspectScroll) {
                    bleInspectScroll = bleInspectSelected;
                }
                if (bleInspectSelected >= bleInspectScroll + BLE_INSPECT_VISIBLE) {
                    bleInspectScroll = bleInspectSelected - BLE_INSPECT_VISIBLE + 1;
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

            if (bleInspectCount == 0) {
                scanDevices();
                drawDeviceList();
                continue;
            }

            drawDetails(bleInspectDevices[bleInspectSelected]);
            beep(3200, 20);
            while (digitalRead(BTN_OK) == HIGH) delay(10);
            waitOkReleaseWasLong();
            delay(80);
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
}
