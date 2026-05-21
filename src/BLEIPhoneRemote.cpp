#include "BLEIPhoneRemote.h"

#include <Arduino.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <BLESecurity.h>
#include <WiFi.h>

#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"

static constexpr uint8_t BLE_REMOTE_VISIBLE = 6;
static constexpr int BLE_REMOTE_LIST_X = 8;
static constexpr int BLE_REMOTE_LIST_Y = 58;
static constexpr int BLE_REMOTE_LIST_W = 304;
static constexpr int BLE_REMOTE_ROW_H = 24;

static BLEServer* remoteServer = nullptr;
static BLEHIDDevice* remoteHid = nullptr;
static BLECharacteristic* keyboardInput = nullptr;
static BLECharacteristic* mediaInput = nullptr;
static BLESecurity* remoteSecurity = nullptr;
static bool remoteReady = false;
static bool remoteConnected = false;

struct RemoteEntry {
    const char* title;
    const char* subtitle;
    uint8_t action;
};

enum RemoteAction : uint8_t {
    ACT_BACK = 0,
    ACT_PAIR,
    ACT_OPEN_SAFARI,
    ACT_OPEN_UNBLOCK,
    ACT_OPEN_YOUTUBE,
    ACT_OPEN_SPOTIFY,
    ACT_OPEN_WHATSAPP,
    ACT_OPEN_INSTAGRAM,
    ACT_OPEN_PHOTOS,
    ACT_HOME,
    ACT_APP_SWITCH,
    ACT_TEXT_DEMO,
    ACT_WEB_SEARCH,
    ACT_MEDIA_MENU,
    ACT_CAMERA_MENU,
    ACT_MEDIA_PLAY,
    ACT_MEDIA_NEXT,
    ACT_MEDIA_PREV,
    ACT_MEDIA_STOP,
    ACT_MEDIA_VOL_UP,
    ACT_MEDIA_VOL_DOWN,
    ACT_MEDIA_MUTE,
    ACT_VOLUME_LIVE,
    ACT_CAMERA_OPEN,
    ACT_CAMERA_PHOTO,
    ACT_CAMERA_VIDEO_TOGGLE,
    ACT_CAMERA_BURST,
    ACT_CAMERA_TIMER
};

static const RemoteEntry MAIN_MENU[] = {
    {"PAIR / CONNECT", "Bluetooth > ESP32-TOOLS-PRO", ACT_PAIR},
    {"SAFARI", "Open with Spotlight", ACT_OPEN_SAFARI},
    {"UNBLOCK", "Desbloqueo remoto", ACT_OPEN_UNBLOCK},
    {"YOUTUBE", "Open with Spotlight", ACT_OPEN_YOUTUBE},
    {"SPOTIFY", "Open with Spotlight", ACT_OPEN_SPOTIFY},
    {"WHATSAPP", "Open with Spotlight", ACT_OPEN_WHATSAPP},
    {"INSTAGRAM", "Open with Spotlight", ACT_OPEN_INSTAGRAM},
    {"PHOTOS", "Open Photos", ACT_OPEN_PHOTOS},
    {"HOME", "Command+H", ACT_HOME},
    {"APP SWITCH", "Command+Tab", ACT_APP_SWITCH},
    {"TEXT DEMO", "Write local demo in Notes", ACT_TEXT_DEMO},
    {"WEB SEARCH", "Safari search demo", ACT_WEB_SEARCH},
    {"MEDIA", "Play, tracks and volume", ACT_MEDIA_MENU},
    {"CAMERA", "Camera remote tools", ACT_CAMERA_MENU},
    {"BACK", "Bluetooth menu", ACT_BACK},       
};

static const RemoteEntry MEDIA_MENU[] = {
    {"PLAY / PAUSE", "Media control", ACT_MEDIA_PLAY},
    {"NEXT", "Next track/video", ACT_MEDIA_NEXT},
    {"PREVIOUS", "Previous track/video", ACT_MEDIA_PREV},
    {"STOP", "Stop playback", ACT_MEDIA_STOP},
    {"VOLUME LIVE", "UP/DN volume, OK mute", ACT_VOLUME_LIVE},
    {"VOLUME +", "Increase volume", ACT_MEDIA_VOL_UP},
    {"VOLUME -", "Decrease volume", ACT_MEDIA_VOL_DOWN},
    {"MUTE", "Toggle mute", ACT_MEDIA_MUTE},
    {"BACK", "iPhone Remote", ACT_BACK},
};

static const RemoteEntry CAMERA_MENU[] = {
    {"OPEN CAMERA", "Spotlight: Camera", ACT_CAMERA_OPEN},
    {"PHOTO", "Volume+ shutter", ACT_CAMERA_PHOTO},
    {"VIDEO TOGGLE", "Volume+ in video mode", ACT_CAMERA_VIDEO_TOGGLE},
    {"BURST 3", "Three shutter taps", ACT_CAMERA_BURST},
    {"TIMER 3S", "Countdown then photo", ACT_CAMERA_TIMER},
    {"BACK", "iPhone Remote", ACT_BACK},
};

static constexpr uint8_t KEY_A = 0x04;
static constexpr uint8_t KEY_1 = 0x1E;
static constexpr uint8_t KEY_ENTER = 0x28;
static constexpr uint8_t KEY_TAB = 0x2B;
static constexpr uint8_t KEY_SPACE = 0x2C;
static constexpr uint8_t KEY_MINUS = 0x2D;
static constexpr uint8_t KEY_PERIOD = 0x37;
static constexpr uint8_t MOD_SHIFT = 0x02;
static constexpr uint8_t MOD_GUI = 0x08;
static constexpr uint16_t MEDIA_NEXT = 1 << 0;
static constexpr uint16_t MEDIA_PREV = 1 << 1;
static constexpr uint16_t MEDIA_STOP = 1 << 2;
static constexpr uint16_t MEDIA_PLAY = 1 << 3;
static constexpr uint16_t MEDIA_MUTE = 1 << 4;
static constexpr uint16_t MEDIA_VOL_UP = 1 << 5;
static constexpr uint16_t MEDIA_VOL_DOWN = 1 << 6;

struct KeyReport {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
};

static const uint8_t REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,
    0x05, 0x0C, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x10, 0x09, 0xB5, 0x09, 0xB6, 0x09, 0xB7,
    0x09, 0xCD, 0x09, 0xE2, 0x09, 0xE9, 0x09, 0xEA,
    0x0A, 0x23, 0x02, 0x0A, 0x24, 0x02, 0x0A, 0x25, 0x02,
    0x0A, 0x26, 0x02, 0x0A, 0x27, 0x02, 0x0A, 0x2A, 0x02,
    0x0A, 0xB1, 0x01, 0x0A, 0xB8, 0x01, 0x0A, 0xB7, 0x01,
    0x81, 0x02, 0xC0
};

class RemoteCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        remoteConnected = true;
    }

    void onDisconnect(BLEServer*) override {
        remoteConnected = false;
        if (remoteReady) BLEDevice::startAdvertising();
    }
};

static RemoteCallbacks callbacks;

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

static void drawStatus(const char* title, const String& line,
                       uint8_t progress = 100) {
    drawFrame("IPHONE REMOTE", "OK/HOLD:BACK");
    uint16_t color = remoteConnected ? TFT_CYAN : TFT_YELLOW;
    tft.drawRect(14, 44, 292, 132, color);
    drawStringFit(30, 62, title, color, 260, 1);
    drawStringFit(30, 88, line, TFT_WHITE, 260, 1);
    drawStringFit(30, 114, "Device: ESP32-TOOLS-PRO", TFT_GREEN, 260, 1);
    drawStringFit(30, 134, remoteConnected ? "Status: connected"
                                           : "Status: waiting/pairing",
                  remoteConnected ? TFT_GREEN : TFT_YELLOW, 260, 1);
    drawBar(30, 154, 260, 10, progress, color);
}

static bool asciiToKey(char c, uint8_t& usage, uint8_t& modifier) {
    modifier = 0;
    if (c >= 'a' && c <= 'z') {
        usage = KEY_A + (c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        usage = KEY_A + (c - 'A');
        modifier = MOD_SHIFT;
        return true;
    }
    if (c >= '1' && c <= '9') {
        usage = KEY_1 + (c - '1');
        return true;
    }
    if (c == '0') {
        usage = 0x27;
        return true;
    }
    if (c == ' ') {
        usage = KEY_SPACE;
        return true;
    }
    if (c == '\n' || c == '\r') {
        usage = KEY_ENTER;
        return true;
    }
    if (c == '-') {
        usage = KEY_MINUS;
        return true;
    }
    if (c == '.') {
        usage = KEY_PERIOD;
        return true;
    }
    usage = 0;
    return false;
}

static bool canSend() {
    return remoteReady && remoteConnected && keyboardInput && mediaInput;
}

static void sendKey(uint8_t usage, uint8_t modifier = 0,
                    uint16_t holdMs = 70) {
    if (!canSend()) return;
    KeyReport report = {};
    report.modifiers = modifier;
    report.keys[0] = usage;
    keyboardInput->setValue((uint8_t*)&report, sizeof(report));
    keyboardInput->notify();
    delay(holdMs);
    memset(&report, 0, sizeof(report));
    keyboardInput->setValue((uint8_t*)&report, sizeof(report));
    keyboardInput->notify();
    delay(110);
}

static void typeText(const char* text, uint16_t charDelay = 28) {
    for (const char* p = text; *p; p++) {
        uint8_t usage = 0;
        uint8_t modifier = 0;
        if (asciiToKey(*p, usage, modifier)) {
            sendKey(usage, modifier, 45);
            delay(charDelay);
        }
    }
}

static void sendShortcut(uint8_t modifier, uint8_t usage) {
    sendKey(usage, modifier, 90);
}

static void sendMedia(uint16_t mask) {
    if (!canSend()) return;
    mediaInput->setValue((uint8_t*)&mask, sizeof(mask));
    mediaInput->notify();
    delay(80);
    mask = 0;
    mediaInput->setValue((uint8_t*)&mask, sizeof(mask));
    mediaInput->notify();
    delay(120);
}

static void beginRemote() {
    if (remoteReady) {
        if (!remoteConnected) BLEDevice::startAdvertising();
        return;
    }

    WiFi.mode(WIFI_OFF);
    drawStatus("Starting BLE", "Creating HID keyboard/media...", 20);
    BLEDevice::init("ESP32-TOOLS-PRO");
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);

    remoteServer = BLEDevice::createServer();
    remoteServer->setCallbacks(&callbacks);
    remoteHid = new BLEHIDDevice(remoteServer);
    keyboardInput = remoteHid->inputReport(1);
    mediaInput = remoteHid->inputReport(2);

    BLECharacteristic* manufacturer = remoteHid->manufacturer();
    if (manufacturer) manufacturer->setValue("PepeAngell");
    remoteHid->pnp(0x02, 0x303A, 0x1002, 0x0110);
    remoteHid->hidInfo(0x00, 0x02);
    remoteHid->reportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
    remoteHid->startServices();

    remoteSecurity = new BLESecurity();
    remoteSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);
    remoteSecurity->setCapability(ESP_IO_CAP_NONE);
    remoteSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK |
                                         ESP_BLE_ID_KEY_MASK);
    remoteSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK |
                                         ESP_BLE_ID_KEY_MASK);

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    BLEAdvertisementData advData;
    advData.setFlags(0x06);
    advData.setAppearance(HID_KEYBOARD);
    advData.setCompleteServices(BLEUUID((uint16_t)0x1812));
    BLEAdvertisementData scanData;
    scanData.setName("ESP32-TOOLS-PRO");
    advertising->setAdvertisementData(advData);
    advertising->setScanResponseData(scanData);
    advertising->setAdvertisementType(ADV_TYPE_IND);
    advertising->setScanResponse(true);
    advertising->setMinInterval(0x20);
    advertising->setMaxInterval(0x40);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    remoteReady = true;
}

static void stopRemote() {
    if (!remoteReady) return;
    BLEDevice::stopAdvertising();
    BLEDevice::deinit(false);
    remoteServer = nullptr;
    remoteHid = nullptr;
    keyboardInput = nullptr;
    mediaInput = nullptr;
    remoteSecurity = nullptr;
    remoteReady = false;
    remoteConnected = false;
}

static bool ensureConnected() {
    beginRemote();
    if (remoteConnected) return true;
    drawStatus("Waiting for phone", "Bluetooth > ESP32-TOOLS-PRO", 35);
    delay(900);
    return false;
}

static void drawPairStatus() {
    uint16_t color = remoteConnected ? TFT_CYAN : TFT_GREEN;
    tft.drawRect(16, 44, 288, 140, color);
    tft.fillRect(32, 144, 256, 14, TFT_BLACK);
    drawStringFit(32, 146,
                  remoteConnected ? "Status: connected"
                                  : "Status: advertising",
                  remoteConnected ? TFT_GREEN : TFT_YELLOW, 256, 1);
}

static void drawPairScreen() {
    drawFrame("PAIR IPHONE", "OK/HOLD:BACK");
    tft.drawRect(16, 44, 288, 140, remoteConnected ? TFT_CYAN : TFT_GREEN);
    drawStringFit(32, 62, "1. iPhone: Settings > Bluetooth",
                  TFT_CYAN, 256, 1);
    drawStringFit(32, 88, "2. Tap ESP32-TOOLS-PRO",
                  TFT_WHITE, 256, 1);
    drawStringFit(32, 114, "3. Accept pairing",
                  TFT_WHITE, 256, 1);
    drawPairStatus();
}

static void runPairScreen() {
    beginRemote();
    bool lastConnected = remoteConnected;
    drawPairScreen();

    while (true) {
        if (remoteConnected != lastConnected) {
            drawPairStatus();
            lastConnected = remoteConnected;
        }

        if (digitalRead(BTN_OK) == LOW) {
            waitOkReleaseWasLong();
            delay(80);
            return;
        }
        delay(10);
    }
}

static void drawMenuRow(const RemoteEntry* items, uint8_t count,
                        uint8_t selected, uint8_t scroll, uint8_t row) {
    int y = BLE_REMOTE_LIST_Y + row * BLE_REMOTE_ROW_H;
    tft.fillRect(BLE_REMOTE_LIST_X, y - 2, BLE_REMOTE_LIST_W,
                 BLE_REMOTE_ROW_H - 2, TFT_BLACK);
    uint8_t idx = scroll + row;
    if (idx >= count) return;

    bool isSelected = idx == selected;
    uint16_t bg = isSelected ? TFT_CYAN : TFT_BLACK;
    uint16_t fg = isSelected ? TFT_BLACK : TFT_WHITE;
    uint16_t sub = isSelected ? TFT_BLACK : TFT_DARKGREY;
    tft.fillRect(BLE_REMOTE_LIST_X, y - 2, BLE_REMOTE_LIST_W,
                 BLE_REMOTE_ROW_H - 2, bg);
    tft.drawRect(BLE_REMOTE_LIST_X, y - 2, BLE_REMOTE_LIST_W,
                 BLE_REMOTE_ROW_H - 2, isSelected ? TFT_WHITE : TFT_DARKGREY);
    drawStringFit(16, y, items[idx].title, fg, 150, 1);
    drawStringRight(304, y, remoteConnected ? "BLE OK" : "PAIR",
                    isSelected ? TFT_BLACK : (remoteConnected ? TFT_GREEN : TFT_YELLOW), 1);
    drawStringFit(16, y + 12, items[idx].subtitle, sub, 278, 1);
}

static void drawMenuRows(const RemoteEntry* items, uint8_t count,
                         uint8_t selected, uint8_t scroll) {
    for (uint8_t row = 0; row < BLE_REMOTE_VISIBLE; row++) {
        drawMenuRow(items, count, selected, scroll, row);
    }
}

static void updateMenuSelection(const RemoteEntry* items, uint8_t count,
                                uint8_t selected, uint8_t scroll,
                                uint8_t oldSelected, uint8_t oldScroll) {
    if (oldScroll != scroll) {
        drawMenuRows(items, count, selected, scroll);
    } else {
        if (oldSelected >= scroll && oldSelected < scroll + BLE_REMOTE_VISIBLE) {
            drawMenuRow(items, count, selected, scroll, oldSelected - scroll);
        }
        if (selected >= scroll && selected < scroll + BLE_REMOTE_VISIBLE) {
            drawMenuRow(items, count, selected, scroll, selected - scroll);
        }
    }
}

static void drawMenu(const char* title, const char* subtitle,
                     const RemoteEntry* items, uint8_t count,
                     uint8_t selected, uint8_t scroll) {
    drawFrame(title, "UP/DN MOVE  OK:SEND  HOLD:BACK");
    drawStringFit(12, 40, subtitle, TFT_CYAN, 296, 1);
    drawMenuRows(items, count, selected, scroll);
    tft.fillRect(12, 204, 296, 10, TFT_BLACK);
    drawStringFit(12, 204,
                  remoteConnected ? "Connected. Actions go to paired device."
                                  : "Pair first from Bluetooth settings.",
                  remoteConnected ? TFT_GREEN : TFT_YELLOW, 296, 1);
}

static uint8_t runRemoteMenu(const char* title, const char* subtitle,
                             const RemoteEntry* items, uint8_t count,
                             uint8_t startSelected = 0) {
    uint8_t selected = min<uint8_t>(startSelected, count - 1);
    uint8_t scroll = selected >= BLE_REMOTE_VISIBLE
        ? selected - BLE_REMOTE_VISIBLE + 1
        : 0;
    drawMenu(title, subtitle, items, count, selected, scroll);

    while (true) {
        if (digitalRead(BTN_DOWN) == LOW) {
            uint8_t oldSelected = selected;
            uint8_t oldScroll = scroll;
            selected = (selected + 1) % count;
            if (selected < scroll) scroll = selected;
            if (selected >= scroll + BLE_REMOTE_VISIBLE) {
                scroll = selected - BLE_REMOTE_VISIBLE + 1;
            }
            beep(2200, 20);
            updateMenuSelection(items, count, selected, scroll,
                                oldSelected, oldScroll);
            delay(170);
        }

        if (digitalRead(BTN_UP) == LOW) {
            uint8_t oldSelected = selected;
            uint8_t oldScroll = scroll;
            selected = selected == 0 ? count - 1 : selected - 1;
            if (selected < scroll) scroll = selected;
            if (selected >= scroll + BLE_REMOTE_VISIBLE) {
                scroll = selected - BLE_REMOTE_VISIBLE + 1;
            }
            beep(2200, 20);
            updateMenuSelection(items, count, selected, scroll,
                                oldSelected, oldScroll);
            delay(170);
        }

        if (digitalRead(BTN_OK) == LOW) {
            bool held = waitOkReleaseWasLong();
            delay(80);
            if (held) return 255;
            beep(3200, 20);
            return selected;
        }

        delay(10);
    }
}

static void openApp(const char* appName) {
    if (!ensureConnected()) return;
    drawStatus("Opening app", String("Spotlight: ") + appName, 45);
    sendShortcut(MOD_GUI, KEY_SPACE);
    delay(650);
    typeText(appName, 24);
    delay(250);
    sendKey(KEY_ENTER);
    delay(650);
}

static void writeNotesDemo() {
    if (!ensureConnected()) return;
    drawStatus("Notes demo", "Opening Notes and typing", 30);
    openApp("Notes");
    delay(1700);
    sendShortcut(MOD_GUI, KEY_A + ('n' - 'a'));
    delay(600);
    typeText("ESP32 TOOLS PRO BLE REMOTE\n", 24);
    typeText("Control local por Bluetooth desde mi dispositivo.\n", 24);
    typeText("Demo segura con acciones seleccionadas fisicamente.\n", 24);
    drawStatus("Notes demo", "Text sent", 100);
    delay(700);
}

static void safariSearchDemo() {
    if (!ensureConnected()) return;
    drawStatus("Safari demo", "Opening safe search", 30);
    openApp("Safari");
    delay(1500);
    sendShortcut(MOD_GUI, KEY_A + ('l' - 'a'));
    delay(500);
    typeText("instagram.com/pepeangelll", 24);
    sendKey(KEY_ENTER);
    drawStatus("Safari demo", "Search sent", 100);
    delay(700);
}

static void runMediaAction(uint8_t action) {
    if (!ensureConnected()) return;
    drawStatus("Media", "Sending media command", 80);
    switch (action) {
        case ACT_MEDIA_PLAY: sendMedia(MEDIA_PLAY); break;
        case ACT_MEDIA_NEXT: sendMedia(MEDIA_NEXT); break;
        case ACT_MEDIA_PREV: sendMedia(MEDIA_PREV); break;
        case ACT_MEDIA_STOP: sendMedia(MEDIA_STOP); break;
        case ACT_MEDIA_VOL_UP: sendMedia(MEDIA_VOL_UP); break;
        case ACT_MEDIA_VOL_DOWN: sendMedia(MEDIA_VOL_DOWN); break;
        case ACT_MEDIA_MUTE: sendMedia(MEDIA_MUTE); break;
        default: break;
    }
    delay(260);
}

static void drawVolumeScreen(uint8_t level, const char* lastAction) {
    drawFrame("IPHONE VOLUME", "UP/DN VOL  OK:MUTE  HOLD:BACK");
    tft.drawRect(18, 48, 284, 132, TFT_DARKGREY);
    drawStringFit(34, 66, "Control de volumen por Bluetooth",
                  TFT_CYAN, 250, 1);
    drawStringFit(34, 92, "UP: subir  DOWN: bajar",
                  TFT_WHITE, 250, 1);
    drawStringFit(34, 112, "OK: mute",
                  TFT_WHITE, 250, 1);
    drawStringFit(34, 136, lastAction, TFT_YELLOW, 250, 1);
    drawBar(34, 156, 252, 10, level, TFT_CYAN);
}

static void runVolumeLive() {
    if (!ensureConnected()) return;
    uint8_t level = 50;
    const char* lastAction = "Ready";
    drawVolumeScreen(level, lastAction);

    while (true) {
        if (digitalRead(BTN_UP) == LOW) {
            sendMedia(MEDIA_VOL_UP);
            level = min<uint8_t>(100, level + 4);
            drawVolumeScreen(level, "Volume +");
            delay(135);
        }

        if (digitalRead(BTN_DOWN) == LOW) {
            sendMedia(MEDIA_VOL_DOWN);
            level = level < 4 ? 0 : level - 4;
            drawVolumeScreen(level, "Volume -");
            delay(135);
        }

        if (digitalRead(BTN_OK) == LOW) {
            bool held = waitOkReleaseWasLong();
            delay(80);
            if (held) return;
            sendMedia(MEDIA_MUTE);
            drawVolumeScreen(level, "Mute sent");
        }

        delay(10);
    }
}

static void drawCameraStatus(const char* action, const String& detail,
                             uint8_t progress = 100) {
    drawFrame("IPHONE CAMERA", "OK/HOLD:BACK");
    tft.drawRect(14, 44, 292, 132, TFT_CYAN);
    drawStringFit(30, 62, action, TFT_CYAN, 260, 1);
    drawStringFit(30, 88, detail, TFT_WHITE, 260, 1);
    drawStringFit(30, 114, "Volume+ acts as shutter on iOS.",
                  TFT_YELLOW, 260, 1);
    drawBar(30, 146, 260, 10, progress, TFT_CYAN);
}

static void cameraShutter(const char* action, const String& detail) {
    if (!ensureConnected()) return;
    drawCameraStatus(action, detail, 75);
    sendMedia(MEDIA_VOL_UP);
    delay(420);
}

static void cameraBurst() {
    if (!ensureConnected()) return;
    for (uint8_t i = 0; i < 3; i++) {
        drawCameraStatus("BURST 3", String("Shot ") + (i + 1) + "/3",
                         35 + i * 25);
        sendMedia(MEDIA_VOL_UP);
        delay(520);
    }
    drawCameraStatus("BURST 3", "Done", 100);
    delay(450);
}

static void cameraTimer() {
    if (!ensureConnected()) return;
    for (int i = 3; i > 0; i--) {
        drawCameraStatus("TIMER 3S", String("Shot in ") + i,
                         (3 - i) * 28);
        delay(850);
    }
    cameraShutter("TIMER 3S", "Taking photo");
}

static void handleMainAction(uint8_t action, uint8_t& mediaSelected,
                             uint8_t& cameraSelected) {
    switch (action) {
        case ACT_PAIR:
            runPairScreen();
            break;
        case ACT_OPEN_SAFARI:
            openApp("Safari");
            break;
        case ACT_OPEN_UNBLOCK:
            openApp("06454");
            break;
        case ACT_OPEN_YOUTUBE:
            openApp("YouTube");
            break;
        case ACT_OPEN_SPOTIFY:
            openApp("Spotify");
            break;
        case ACT_OPEN_WHATSAPP:
            openApp("WhatsApp");
            break;
        case ACT_OPEN_INSTAGRAM:
            openApp("Instagram");
            break;
        case ACT_OPEN_PHOTOS:
            openApp("Photos");
            break;
        case ACT_HOME:
            if (ensureConnected()) {
                drawStatus("HOME", "Sending Command+H", 80);
                sendShortcut(MOD_GUI, KEY_A + ('h' - 'a'));
                delay(400);
            }
            break;
        case ACT_APP_SWITCH:
            if (ensureConnected()) {
                drawStatus("APP SWITCH", "Sending Command+Tab", 80);
                sendShortcut(MOD_GUI, KEY_TAB);
                delay(400);
            }
            break;
        case ACT_TEXT_DEMO:
            writeNotesDemo();
            break;
        case ACT_WEB_SEARCH:
            safariSearchDemo();
            break;
        case ACT_MEDIA_MENU:
            while (true) {
                uint8_t idx = runRemoteMenu("IPHONE MEDIA", "Multimedia BLE",
                                            MEDIA_MENU,
                                            sizeof(MEDIA_MENU) / sizeof(MEDIA_MENU[0]),
                                            mediaSelected);
                if (idx == 255) break;
                mediaSelected = idx;
                const RemoteEntry& entry = MEDIA_MENU[idx];
                if (entry.action == ACT_BACK) break;
                if (entry.action == ACT_VOLUME_LIVE) runVolumeLive();
                else runMediaAction(entry.action);
            }
            break;
        case ACT_CAMERA_MENU:
            while (true) {
                uint8_t idx = runRemoteMenu("IPHONE CAMERA", "Camera remote",
                                            CAMERA_MENU,
                                            sizeof(CAMERA_MENU) / sizeof(CAMERA_MENU[0]),
                                            cameraSelected);
                if (idx == 255) break;
                cameraSelected = idx;
                const RemoteEntry& entry = CAMERA_MENU[idx];
                if (entry.action == ACT_BACK) break;
                if (entry.action == ACT_CAMERA_OPEN) openApp("Camera");
                else if (entry.action == ACT_CAMERA_PHOTO) {
                    cameraShutter("PHOTO", "Sending shutter");
                } else if (entry.action == ACT_CAMERA_VIDEO_TOGGLE) {
                    cameraShutter("VIDEO", "Toggle record if Camera is ready");
                } else if (entry.action == ACT_CAMERA_BURST) {
                    cameraBurst();
                } else if (entry.action == ACT_CAMERA_TIMER) {
                    cameraTimer();
                }
            }
            break;
        default:
            break;
    }
}

void runBLEIPhoneRemote() {
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);

    uint8_t selected = 0;
    uint8_t mediaSelected = 0;
    uint8_t cameraSelected = 0;

    while (true) {
        uint8_t idx = runRemoteMenu("IPHONE REMOTE", "BLE HID keyboard/media",
                                    MAIN_MENU,
                                    sizeof(MAIN_MENU) / sizeof(MAIN_MENU[0]),
                                    selected);
        if (idx == 255) break;
        selected = idx;

        const RemoteEntry& entry = MAIN_MENU[idx];
        if (entry.action == ACT_BACK) break;
        handleMainAction(entry.action, mediaSelected, cameraSelected);
    }

    stopRemote();
}
