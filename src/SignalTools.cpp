#include "SignalTools.h"

#include <Preferences.h>
#include <SPI.h>

#include "MenuSystem.h"
#include "PepeDraw.h"
#include "Pins.h"
#include "SoundUtils.h"
#include "IrAnalyzer.h"
#include "IrProtocolScanner.h"
#include "IrSniffer.h"
#include "IrVirtualRemotes.h"
#include "VirtualKeyboard.h"

// ═══════════════════════════════════════════════════════════════════════════
//  CC1101 register addresses and command strobes
// ═══════════════════════════════════════════════════════════════════════════
static constexpr uint8_t CC1101_READ_SINGLE = 0x80;
static constexpr uint8_t CC1101_READ_BURST  = 0xC0;
static constexpr uint8_t CC1101_IOCFG0      = 0x02;
static constexpr uint8_t CC1101_PARTNUM     = 0x30;
static constexpr uint8_t CC1101_VERSION     = 0x31;
static constexpr uint8_t CC1101_RSSI        = 0x34;
static constexpr uint8_t CC1101_MARCSTATE   = 0x35;
static constexpr uint8_t CC1101_SRES        = 0x30;
static constexpr uint8_t CC1101_SRX         = 0x34;
static constexpr uint8_t CC1101_SIDLE       = 0x36;

static constexpr uint8_t CC1101_GDO_CARRIER_SENSE = 0x0E;

// ═══════════════════════════════════════════════════════════════════════════
//  IR capture / transmit constants
// ═══════════════════════════════════════════════════════════════════════════
static constexpr int      IR_LEDC_CHANNEL      = 1;
static constexpr int      IR_LEDC_FREQ         = 38000;
static constexpr int      IR_LEDC_RESOLUTION   = 8;
static constexpr int      IR_LEDC_MARK_DUTY    = 170; // Active-low TX: 33% LOW carrier.
static constexpr int      IR_LEDC_SPACE_DUTY   = 255; // Active-low TX: steady HIGH = LED off.
static constexpr uint16_t IR_RAW_MAX           = 768;
static constexpr uint32_t IR_CAPTURE_TIMEOUT_MS = 10000;
static constexpr uint32_t IR_CAPTURE_GAP_US    = 25000;
static constexpr uint32_t IR_CAPTURE_MAX_US    = 180000;
static constexpr uint32_t IR_MIN_PULSE_US      = 250;
static constexpr uint32_t IR_START_MIN_US      = 1800;
static constexpr uint32_t IR_START_MAX_US      = 20000;
static constexpr uint8_t  IR_SAVE_MAX          = 6;
static constexpr uint8_t  IR_SAVE_NAME_MAX     = 14;
static constexpr const char* IR_SAVE_NAMESPACE = "irsignals";

// ═══════════════════════════════════════════════════════════════════════════
//  Module state
// ═══════════════════════════════════════════════════════════════════════════
static SPISettings cc1101SpiSettings(4000000, MSBFIRST, SPI_MODE0);

static uint16_t lastIrRaw[IR_RAW_MAX];
static uint16_t lastIrRawCount = 0;
static bool     lastIrOverflow = false;

static bool ledcAttached = false;   // tracks whether IR_LEDC_CHANNEL is bound to IR_TX_PIN

// ═══════════════════════════════════════════════════════════════════════════
//  Forward declarations (so order of definitions does not matter)
// ═══════════════════════════════════════════════════════════════════════════
static void    irRxMode();
static void    irTxBegin();
static void    irTxEnd();
static void    irMark(uint32_t usec);
static void    irSpace(uint32_t usec);
static void    sendIrRaw(const uint16_t* raw, uint16_t count);

static bool    cc1101Select();
static void    cc1101Deselect();
static uint8_t cc1101ReadRegister(uint8_t reg, bool* ready);
static void    cc1101WriteRegister(uint8_t reg, uint8_t value);
static void    cc1101Strobe(uint8_t command);
static bool    cc1101PrepareMonitor();
static int     cc1101RssiDbm(uint8_t raw);

static void    drawToolFrame(const char* title);
static void    drawIrRawPreview(uint16_t count, bool overflow);
static bool    captureIrRaw();
static void    waitOkReleased();

static bool    runIrCaptureActions();
static void    runHardwareDiag();
static void    runInputMonitor();
static void    runIrRawCapture();
static void    runIrReplayLast();
static void    runSavedIrCaptures();
static void    runIrTxTest();

// ═══════════════════════════════════════════════════════════════════════════
//  Small helpers
// ═══════════════════════════════════════════════════════════════════════════
static void waitOkReleased() {
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(60);
}

// ═══════════════════════════════════════════════════════════════════════════
//  IR pin / LEDC management
//  We track whether LEDC is bound so we never call detach on an unattached
//  pin (which on some ESP32 cores leaves the pin in a weird state).
// ═══════════════════════════════════════════════════════════════════════════
static void irRxMode() {
    if (ledcAttached) {
        ledcWrite(IR_LEDC_CHANNEL, IR_LEDC_SPACE_DUTY);
        ledcDetachPin(IR_TX_PIN);
        ledcAttached = false;
    }
    pinMode(IR_TX_PIN, OUTPUT);
    digitalWrite(IR_TX_PIN, HIGH);
    pinMode(IR_RX_PIN, INPUT);
}

static void irTxBegin() {
    pinMode(IR_TX_PIN, OUTPUT);
    digitalWrite(IR_TX_PIN, HIGH);
    ledcSetup(IR_LEDC_CHANNEL, IR_LEDC_FREQ, IR_LEDC_RESOLUTION);
    ledcAttachPin(IR_TX_PIN, IR_LEDC_CHANNEL);
    ledcWrite(IR_LEDC_CHANNEL, IR_LEDC_SPACE_DUTY);
    ledcAttached = true;
}

static void irTxEnd() {
    if (ledcAttached) {
        ledcWrite(IR_LEDC_CHANNEL, IR_LEDC_SPACE_DUTY);
        ledcDetachPin(IR_TX_PIN);
        ledcAttached = false;
    }
    pinMode(IR_TX_PIN, OUTPUT);
    digitalWrite(IR_TX_PIN, HIGH);
}

static void irMark(uint32_t usec) {
    ledcWrite(IR_LEDC_CHANNEL, IR_LEDC_MARK_DUTY);
    delayMicroseconds(usec);
}

static void irSpace(uint32_t usec) {
    ledcWrite(IR_LEDC_CHANNEL, IR_LEDC_SPACE_DUTY);
    delayMicroseconds(usec);
}

static void sendIrRaw(const uint16_t* raw, uint16_t count) {
    if (!raw || count == 0) return;

    irTxBegin();
    for (uint16_t i = 0; i < count; i++) {
        if ((i % 2) == 0) irMark(raw[i]);
        else              irSpace(raw[i]);
    }
    irTxEnd();
}

// ═══════════════════════════════════════════════════════════════════════════
//  CC1101 SPI helpers (shared bus with TFT + nRF24)
//
//  IMPORTANT: We no longer wait on MISO inside the CS routine, because MISO
//  is a shared line. With no CC1101 attached, MISO floats and we would burn
//  20 ms per access. Instead we give the chip a short, fixed settling
//  delay; if it is really there it responds well within that window.
// ═══════════════════════════════════════════════════════════════════════════
static bool cc1101Select() {
    SPI.beginTransaction(cc1101SpiSettings);
    digitalWrite(CC1101_CSN_PIN, LOW);
    delayMicroseconds(5);
    return true;
}

static void cc1101Deselect() {
    digitalWrite(CC1101_CSN_PIN, HIGH);
    SPI.endTransaction();
}

// `ready` is optional. Pass nullptr when you don't care about the status byte.
static uint8_t cc1101ReadRegister(uint8_t reg, bool* ready) {
    cc1101Select();
    uint8_t readMode = (reg >= 0x30) ? CC1101_READ_BURST : CC1101_READ_SINGLE;
    uint8_t status   = SPI.transfer(reg | readMode);  // header byte returns chip status
    uint8_t value    = SPI.transfer(0x00);
    cc1101Deselect();

    // CC1101 chip-not-ready bit is the MSB of every status byte. When the
    // module is absent we usually read 0xFF, which means ready=false.
    if (ready) *ready = ((status & 0x80) == 0) && (status != 0xFF);
    return value;
}

static void cc1101WriteRegister(uint8_t reg, uint8_t value) {
    cc1101Select();
    SPI.transfer(reg);
    SPI.transfer(value);
    cc1101Deselect();
}

static void cc1101Strobe(uint8_t command) {
    cc1101Select();
    SPI.transfer(command);
    cc1101Deselect();
}

static bool cc1101PrepareMonitor() {
    bool ready = false;
    cc1101Strobe(CC1101_SRES);
    delay(2);
    cc1101WriteRegister(CC1101_IOCFG0, CC1101_GDO_CARRIER_SENSE);
    cc1101Strobe(CC1101_SIDLE);
    delay(1);
    cc1101Strobe(CC1101_SRX);
    cc1101ReadRegister(CC1101_PARTNUM, &ready);
    return ready;
}

static int cc1101RssiDbm(uint8_t raw) {
    int rssi = raw;
    if (rssi >= 128) rssi -= 256;
    return (rssi / 2) - 74;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Drawing helpers
// ═══════════════════════════════════════════════════════════════════════════
static void drawToolFrame(const char* title) {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    tft.drawFastHLine(0, 34, 320, TFT_WHITE);
    tft.drawFastHLine(0, 212, 320, TFT_WHITE);
    drawStringBig(10, 8, title, TFT_WHITE, 1);
    drawStringCustom(10, 220, "OK: RETURN", TFT_WHITE, 1);
}

static void drawIrRawPreview(uint16_t count, bool overflow) {
    tft.fillRect(1, 36, 318, 175, TFT_BLACK);

    unsigned long total = 0;
    uint16_t minPulse = 65535;
    uint16_t maxPulse = 0;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t value = lastIrRaw[i];
        total += value;
        if (value < minPulse) minPulse = value;
        if (value > maxPulse) maxPulse = value;
    }

    drawStringCustom(12,  46, "Captured timings", TFT_CYAN, 2);
    drawStringCustom(18,  74, "COUNT : " + String(count), TFT_WHITE, 1);
    drawStringCustom(18,  90, "TOTAL : " + String(total / 1000.0f, 1) + " ms", TFT_WHITE, 1);
    drawStringCustom(18, 106, "MIN/MAX: " + String(minPulse) + "/" + String(maxPulse) + " us", TFT_WHITE, 1);
    drawStringCustom(18, 122, overflow ? "BUFFER: FULL" : "BUFFER: OK",
                     overflow ? TFT_RED : TFT_GREEN, 1);

    drawStringCustom(12, 146, "First timings us:", TFT_YELLOW, 1);
    for (uint16_t i = 0; i < count && i < 12; i++) {
        int x = 14 + (i % 4) * 76;
        int y = 164 + (i / 4) * 14;
        drawStringCustom(x, y, String(lastIrRaw[i]), TFT_WHITE, 1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  IR raw capture
// ═══════════════════════════════════════════════════════════════════════════
static void showIrMessage(const char* title, const String& line1,
                          const String& line2, uint16_t color) {
    drawToolFrame(title);
    drawStringCustom(24,  82, line1, color, 2);
    if (line2.length() > 0) {
        drawStringCustom(22, 116, line2, TFT_WHITE, 1);
    }
    while (digitalRead(BTN_OK) == HIGH) delay(20);
    waitOkReleased();
}

// Keep IR captures in their own small NVS namespace. Each slot stores:
// name, timing count, and the raw uint16_t timing buffer.
static String irSaveKey(char prefix, uint8_t slot) {
    char key[5];
    snprintf(key, sizeof(key), "%c%02u", prefix, slot);
    return String(key);
}

static bool loadSavedIrSlot(uint8_t slot, String* name,
                            uint16_t* raw, uint16_t* count) {
    if (slot >= IR_SAVE_MAX) return false;

    Preferences prefs;
    if (!prefs.begin(IR_SAVE_NAMESPACE, true)) return false;

    String nameKey  = irSaveKey('n', slot);
    String countKey = irSaveKey('c', slot);
    String dataKey  = irSaveKey('d', slot);

    uint32_t storedCount = prefs.getUInt(countKey.c_str(), 0);
    size_t expectedLen = storedCount * sizeof(uint16_t);
    bool ok = false;

    if (storedCount > 0 && storedCount <= IR_RAW_MAX &&
        prefs.getBytesLength(dataKey.c_str()) == expectedLen) {
        ok = true;
        if (raw) {
            ok = (prefs.getBytes(dataKey.c_str(), raw, expectedLen) == expectedLen);
        }
        if (ok) {
            if (count) *count = static_cast<uint16_t>(storedCount);
            if (name) {
                *name = prefs.getString(nameKey.c_str(),
                                        String("IR ") + String(slot + 1));
                name->trim();
                if (name->length() == 0) {
                    *name = String("IR ") + String(slot + 1);
                }
            }
        }
    }

    prefs.end();
    return ok;
}

static int findFirstEmptyIrSlot() {
    for (uint8_t slot = 0; slot < IR_SAVE_MAX; slot++) {
        if (!loadSavedIrSlot(slot, nullptr, nullptr, nullptr)) return slot;
    }
    return -1;
}

static bool deleteSavedIrSlot(uint8_t slot) {
    if (slot >= IR_SAVE_MAX) return false;

    Preferences prefs;
    if (!prefs.begin(IR_SAVE_NAMESPACE, false)) return false;

    prefs.remove(irSaveKey('n', slot).c_str());
    prefs.remove(irSaveKey('c', slot).c_str());
    prefs.remove(irSaveKey('d', slot).c_str());
    prefs.end();
    return true;
}

static bool renameSavedIrSlot(uint8_t slot, const String& requestedName) {
    if (slot >= IR_SAVE_MAX) return false;

    String name = requestedName;
    name.trim();
    if (name.length() == 0) return false;
    if (name.length() > IR_SAVE_NAME_MAX) {
        name = name.substring(0, IR_SAVE_NAME_MAX);
    }

    Preferences prefs;
    if (!prefs.begin(IR_SAVE_NAMESPACE, false)) return false;

    String nameKey = irSaveKey('n', slot);
    bool ok = (prefs.putString(nameKey.c_str(), name) > 0);
    prefs.end();
    return ok;
}

static bool saveCurrentIrCapture(const String& requestedName, int* savedSlot) {
    if (savedSlot) *savedSlot = -2;
    if (lastIrRawCount == 0) return false;

    int slot = findFirstEmptyIrSlot();
    if (slot < 0) {
        if (savedSlot) *savedSlot = -1;
        return false;
    }

    String name = requestedName;
    name.trim();
    if (name.length() == 0) return false;
    if (name.length() > IR_SAVE_NAME_MAX) {
        name = name.substring(0, IR_SAVE_NAME_MAX);
    }

    Preferences prefs;
    if (!prefs.begin(IR_SAVE_NAMESPACE, false)) return false;

    String nameKey  = irSaveKey('n', slot);
    String countKey = irSaveKey('c', slot);
    String dataKey  = irSaveKey('d', slot);
    size_t dataLen = lastIrRawCount * sizeof(uint16_t);

    prefs.remove(nameKey.c_str());
    prefs.remove(countKey.c_str());
    prefs.remove(dataKey.c_str());

    bool ok = (prefs.putString(nameKey.c_str(), name) > 0);
    ok = (prefs.putUInt(countKey.c_str(), lastIrRawCount) > 0) && ok;
    ok = (prefs.putBytes(dataKey.c_str(), lastIrRaw, dataLen) == dataLen) && ok;
    prefs.end();

    if (ok && savedSlot) *savedSlot = slot;
    return ok;
}

static bool loadSavedIrToLast(uint8_t slot, String* name) {
    uint16_t count = 0;
    bool ok = loadSavedIrSlot(slot, name, lastIrRaw, &count);
    if (ok) {
        lastIrRawCount = count;
        lastIrOverflow = false;
    }
    return ok;
}

static void replayLastIrAndWait(const char* title, const String& label) {
    if (lastIrRawCount == 0) {
        showIrMessage(title, "NO CAPTURE", "Capture or load a signal first.",
                      TFT_RED);
        return;
    }

    drawToolFrame(title);
    drawStringCustom(20,  58, label, TFT_CYAN, 1);
    drawStringCustom(20,  78, "Timings: " + String(lastIrRawCount),
                     TFT_WHITE, 1);
    drawStringCustom(20, 104, "Sending once...", TFT_YELLOW, 1);

    sendIrRaw(lastIrRaw, lastIrRawCount);
    irRxMode();

    drawStringCustom(20, 138, "SENT", TFT_GREEN, 2);
    while (digitalRead(BTN_OK) == HIGH) delay(20);
    waitOkReleased();
}

static void promptAndSaveCurrentIrCapture() {
    if (lastIrRawCount == 0) {
        showIrMessage("SAVE IR", "NO CAPTURE", "Capture a signal first.",
                      TFT_RED);
        return;
    }

    String name = virtualKeyboardInput("IR NAME", "Name this capture",
                                       IR_SAVE_NAME_MAX, false);
    name.trim();
    if (name.length() == 0) {
        showIrMessage("SAVE IR", "CANCELED", "", TFT_YELLOW);
        return;
    }

    int slot = -2;
    if (saveCurrentIrCapture(name, &slot)) {
        showIrMessage("SAVE IR", "SAVED",
                      "Slot " + String(slot + 1) + " / " + String(IR_SAVE_MAX),
                      TFT_GREEN);
    } else if (slot == -1) {
        showIrMessage("SAVE IR", "MEMORY FULL",
                      "Delete a saved capture first.", TFT_RED);
    } else {
        showIrMessage("SAVE IR", "SAVE FAILED",
                      "NVS write did not complete.", TFT_RED);
    }
}

static bool runIrCaptureActions() {
    static const char* actions[] = {
        "Replay Last",
        "Save Capture",
        "Capture Again",
        "Back"
    };

    while (true) {
        int choice = runSubMenu("IR CAPTURE", actions,
                                sizeof(actions) / sizeof(char*));
        switch (choice) {
            case -1:
            case  3:
                return false;
            case  0:
                replayLastIrAndWait("IR REPLAY", "Last captured signal");
                break;
            case  1:
                promptAndSaveCurrentIrCapture();
                break;
            case  2:
                return true;
        }
    }
}

static bool confirmDeleteIrSlot(const String& name) {
    static const char* actions[] = {
        "Cancel",
        "Delete"
    };

    drawToolFrame("DELETE IR");
    drawStringCustom(20,  58, "Delete saved signal?", TFT_YELLOW, 1);
    drawStringCustom(20,  78, name, TFT_WHITE, 2);
    delay(350);

    int choice = runSubMenu("DELETE IR", actions,
                            sizeof(actions) / sizeof(char*), false);
    return choice == 1;
}

static void promptAndRenameSavedIrSlot(uint8_t slot, const String& currentName) {
    String name = virtualKeyboardInput("IR RENAME", currentName,
                                       IR_SAVE_NAME_MAX, false);
    name.trim();
    if (name.length() == 0) {
        showIrMessage("IR RENAME", "CANCELED", "", TFT_YELLOW);
        return;
    }

    if (renameSavedIrSlot(slot, name)) {
        showIrMessage("IR RENAME", "RENAMED", name, TFT_GREEN);
    } else {
        showIrMessage("IR RENAME", "RENAME FAILED", "", TFT_RED);
    }
}

static void runSavedIrSlot(uint8_t slot) {
    static const char* actions[] = {
        "Replay",
        "Load as Last",
        "Rename",
        "Delete",
        "Back"
    };

    while (true) {
        String name;
        if (!loadSavedIrSlot(slot, &name, nullptr, nullptr)) {
            showIrMessage("SAVED IR", "NOT FOUND",
                          "This slot is empty now.", TFT_RED);
            return;
        }

        int choice = runSubMenu("IR OPTIONS", actions,
                                sizeof(actions) / sizeof(char*));
        switch (choice) {
            case -1:
            case  4:
                return;
            case  0:
                if (loadSavedIrToLast(slot, &name)) {
                    replayLastIrAndWait("IR REPLAY", name);
                } else {
                    showIrMessage("IR REPLAY", "LOAD FAILED", "", TFT_RED);
                }
                break;
            case  1:
                if (loadSavedIrToLast(slot, &name)) {
                    showIrMessage("SAVED IR", "LOADED",
                                  name + " (" + String(lastIrRawCount) + ")",
                                  TFT_GREEN);
                } else {
                    showIrMessage("SAVED IR", "LOAD FAILED", "", TFT_RED);
                }
                break;
            case  2:
                promptAndRenameSavedIrSlot(slot, name);
                break;
            case  3:
                if (confirmDeleteIrSlot(name)) {
                    if (deleteSavedIrSlot(slot)) {
                        showIrMessage("SAVED IR", "DELETED", name, TFT_GREEN);
                    } else {
                        showIrMessage("SAVED IR", "DELETE FAILED", "", TFT_RED);
                    }
                    return;
                }
                break;
        }
    }
}

static void runSavedIrCaptures() {
    while (true) {
        String labels[IR_SAVE_MAX];
        const char* items[IR_SAVE_MAX];
        uint8_t slots[IR_SAVE_MAX];
        int itemCount = 0;

        for (uint8_t slot = 0; slot < IR_SAVE_MAX; slot++) {
            String name;
            uint16_t count = 0;
            if (loadSavedIrSlot(slot, &name, nullptr, &count)) {
                labels[itemCount] = String(slot + 1) + ": " + name;
                items[itemCount] = labels[itemCount].c_str();
                slots[itemCount] = slot;
                itemCount++;
            }
        }

        if (itemCount == 0) {
            showIrMessage("SAVED IR", "NO SAVED", "Capture and save first.",
                          TFT_YELLOW);
            return;
        }

        int choice = runSubMenu("SAVED IR", items, itemCount);
        if (choice < 0) return;
        runSavedIrSlot(slots[choice]);
    }
}

static bool captureIrRaw() {
    lastIrRawCount = 0;
    lastIrOverflow = false;

    unsigned long waitStart = millis();
    bool started = false;

    // -- 1) Wait for the first valid LOW header pulse -----------------------
    while (!started) {
        if (digitalRead(BTN_OK) == LOW) {
            waitOkReleased();
            return false;
        }
        if (millis() - waitStart > IR_CAPTURE_TIMEOUT_MS) {
            return false;
        }

        if (digitalRead(IR_RX_PIN) == LOW) {
            uint32_t lowStart = micros();
            while (digitalRead(IR_RX_PIN) == LOW) {
                if (digitalRead(BTN_OK) == LOW) {
                    waitOkReleased();
                    return false;
                }
                if (millis() - waitStart > IR_CAPTURE_TIMEOUT_MS) {
                    return false;
                }
            }

            uint32_t lowDuration = micros() - lowStart;
            if (lowDuration >= IR_START_MIN_US && lowDuration <= IR_START_MAX_US) {
                if (lowDuration > 65535) lowDuration = 65535;
                lastIrRaw[lastIrRawCount++] = static_cast<uint16_t>(lowDuration);
                started = true;
                break;
            }
        }
        delay(1);
    }

    // -- 2) Record the rest of the frame until we hit a long idle gap ------
    int      currentLevel  = HIGH;
    uint32_t lastChange    = micros();
    uint32_t lastEdge      = lastChange;
    uint32_t captureStart  = lastChange;

    while ((uint32_t)(micros() - lastEdge)     < IR_CAPTURE_GAP_US &&
           (uint32_t)(micros() - captureStart) < IR_CAPTURE_MAX_US) {
        int level = digitalRead(IR_RX_PIN);
        if (level != currentLevel) {
            uint32_t now      = micros();
            uint32_t duration = now - lastChange;
            if (duration >= IR_MIN_PULSE_US) {
                if (lastIrRawCount < IR_RAW_MAX) {
                    if (duration > 65535) duration = 65535;
                    lastIrRaw[lastIrRawCount++] = static_cast<uint16_t>(duration);
                } else {
                    lastIrOverflow = true;
                    break;
                }
                currentLevel = level;
                lastChange   = now;
                lastEdge     = now;
            }
        }
    }

    return lastIrRawCount >= 4 && !lastIrOverflow;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Tools
// ═══════════════════════════════════════════════════════════════════════════
static void runHardwareDiag() {
    irRxMode();
    pinMode(CC1101_CSN_PIN, OUTPUT);
    digitalWrite(CC1101_CSN_PIN, HIGH);
    pinMode(CC1101_GDO0_PIN, INPUT);
    bool monitorReady = cc1101PrepareMonitor();

    drawToolFrame("SIGNAL DIAG");

    bool    ready   = false;
    uint8_t partnum = cc1101ReadRegister(CC1101_PARTNUM,   &ready);
    uint8_t version = cc1101ReadRegister(CC1101_VERSION,   nullptr);
    uint8_t marc    = cc1101ReadRegister(CC1101_MARCSTATE, nullptr);
    uint8_t rssiRaw = cc1101ReadRegister(CC1101_RSSI,      nullptr);

    drawStringCustom(12,  50, "IR UNIT", TFT_CYAN, 2);
    drawStringCustom(22,  74, "OUT/TX GPIO: " + String(IR_TX_PIN), TFT_WHITE, 1);
    drawStringCustom(22,  90, "IN/RX GPIO : " + String(IR_RX_PIN), TFT_WHITE, 1);
    drawStringCustom(22, 106, "IN LEVEL   : " + String(digitalRead(IR_RX_PIN)), TFT_YELLOW, 1);

    drawStringCustom(12, 132, "CC1101", TFT_CYAN, 2);
    drawStringCustom(22, 156, "CSN GPIO: "  + String(CC1101_CSN_PIN),  TFT_WHITE, 1);
    drawStringCustom(22, 172, "GDO0 GPIO: " + String(CC1101_GDO0_PIN), TFT_WHITE, 1);
    drawStringCustom(22, 188, "SPI READY: " + String((ready && monitorReady) ? "YES" : "NO"),
                     (ready && monitorReady) ? TFT_GREEN : TFT_RED, 1);

    drawStringCustom(170,  74, "PART: 0x" + String(partnum, HEX), TFT_WHITE, 1);
    drawStringCustom(170,  90, "VER : 0x" + String(version, HEX), TFT_WHITE, 1);
    drawStringCustom(170, 106, "MARC: 0x" + String(marc,    HEX), TFT_WHITE, 1);
    drawStringCustom(170, 122, "RSSI: " + String(cc1101RssiDbm(rssiRaw)) + " dBm", TFT_WHITE, 1);

    while (digitalRead(BTN_OK) == HIGH) delay(20);
    waitOkReleased();
}

static void runInputMonitor() {
    irRxMode();

    drawToolFrame("INPUT MONITOR");
    drawStringCustom(12, 48, "IR receiver only. TX is disabled.", TFT_CYAN, 1);
    drawStringCustom(12, 64, "Use remote to check real frames.", TFT_CYAN, 1);

    unsigned long irFrames = 0;
    unsigned long irNoise  = 0;
    unsigned long lastDraw = 0;
    bool     irLowTracking = false;
    uint32_t irLowStart    = 0;

    while (true) {
        int ir = digitalRead(IR_RX_PIN);

        if (ir == LOW && !irLowTracking) {
            irLowTracking = true;
            irLowStart    = micros();
        }
        if (ir == HIGH && irLowTracking) {
            uint32_t duration = micros() - irLowStart;
            if (duration >= IR_START_MIN_US && duration <= IR_START_MAX_US) {
                irFrames++;
            } else if (duration >= IR_MIN_PULSE_US) {
                irNoise++;
            }
            irLowTracking = false;
        }

        if (millis() - lastDraw > 120) {
            tft.fillRect(12, 94, 296, 96, TFT_BLACK);
            drawStringCustom(20, 100, "IR IN LEVEL : " + String(ir),       TFT_WHITE,    2);
            drawStringCustom(20, 126, "IR FRAMES   : " + String(irFrames), TFT_YELLOW,   1);
            drawStringCustom(20, 142, "IR NOISE    : " + String(irNoise),  TFT_DARKGREY, 1);
            lastDraw = millis();
        }

        if (digitalRead(BTN_OK) == LOW) {
            waitOkReleased();
            return;
        }

        delayMicroseconds(100);
    }
}

static void runIrRawCapture() {
    while (true) {
        irRxMode();

        drawToolFrame("IR RAW CAPTURE");
        drawStringCustom(12, 52, "Point remote at IR module.", TFT_CYAN,  1);
        drawStringCustom(12, 70, "Press a remote button now.", TFT_CYAN,  1);
        drawStringCustom(12, 92, "Waiting up to 10 seconds...", TFT_WHITE, 1);

        bool ok = captureIrRaw();
        if (!ok) {
            tft.fillRect(1, 36, 318, 175, TFT_BLACK);
            drawStringCustom(30,  92, "NO IR FRAME", TFT_RED, 2);
            drawStringCustom(22, 122, "Try again closer to receiver.", TFT_WHITE, 1);
            while (digitalRead(BTN_OK) == HIGH) delay(20);
            waitOkReleased();
            return;
        }

        drawIrRawPreview(lastIrRawCount, lastIrOverflow);
        tft.fillRect(1, 213, 318, 26, TFT_BLACK);
        drawStringCustom(10, 220, "OK: OPTIONS", TFT_WHITE, 1);

        Serial.print("[IR] Raw count=");
        Serial.print(lastIrRawCount);
        Serial.print(" data=");
        for (uint16_t i = 0; i < lastIrRawCount; i++) {
            if (i) Serial.print(',');
            Serial.print(lastIrRaw[i]);
        }
        Serial.println();

        while (digitalRead(BTN_OK) == HIGH) delay(20);
        waitOkReleased();

        bool captureAgain = runIrCaptureActions();
        if (!captureAgain) return;
    }
}

static void runIrReplayLast() {
    drawToolFrame("IR REPLAY");

    if (lastIrRawCount == 0) {
        drawStringCustom(30,  92, "NO CAPTURE SAVED", TFT_RED, 2);
        drawStringCustom(22, 122, "Run IR Raw Capture first.", TFT_WHITE, 1);
        while (digitalRead(BTN_OK) == HIGH) delay(20);
        waitOkReleased();
        return;
    }

    drawStringCustom(20,  58, "Ready to replay last frame.",    TFT_CYAN,   1);
    drawStringCustom(20,  78, "Timings: " + String(lastIrRawCount), TFT_WHITE, 1);
    drawStringCustom(20, 102, "Tap OK to send once.",           TFT_YELLOW, 1);
    drawStringCustom(20, 122, "Hold OK after send to return.",  TFT_WHITE,  1);

    while (true) {
        if (digitalRead(BTN_OK) == LOW) {
            sendIrRaw(lastIrRaw, lastIrRawCount);

            tft.fillRect(20, 150, 280, 22, TFT_BLACK);
            drawStringCustom(20, 154, "SENT " + String(lastIrRawCount) + " timings",
                             TFT_GREEN, 1);
            bool held = waitOkReleaseWasLong();
            if (held) return;
        }
        delay(10);
    }
}

static void runIrTxTest() {
    drawToolFrame("IR TX TEST");
    drawStringCustom(20,  58, "Use phone camera to view LEDs.", TFT_CYAN,  1);
    drawStringCustom(20,  82, "Sending 3 carrier bursts...",    TFT_WHITE, 1);

    irTxBegin();
    for (int i = 0; i < 3; i++) {
        irMark(120000);
        irSpace(160000);
    }
    irTxEnd();

    // Leave the IR pin in a clean RX-ready state so subsequent tools work.
    irRxMode();

    drawStringCustom(20, 118, "DONE", TFT_GREEN, 2);
    while (digitalRead(BTN_OK) == HIGH) delay(20);
    waitOkReleased();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Public entry point
// ═══════════════════════════════════════════════════════════════════════════
uint8_t signalToolsSavedIrMax() {
    return IR_SAVE_MAX;
}

bool signalToolsLoadSavedIrInfo(uint8_t slot, String* name, uint16_t* count) {
    return loadSavedIrSlot(slot, name, nullptr, count);
}

bool signalToolsReplaySavedIrSlot(uint8_t slot) {
    String name;
    if (!loadSavedIrToLast(slot, &name)) return false;
    sendIrRaw(lastIrRaw, lastIrRawCount);
    irRxMode();
    return true;
}

void runSignalTools() {
    static const char* items[] = {
        "Hardware Diag",
        "Input Monitor",
        "IR Analyzer",
        "Protocol Scan",
        "IR Sniffer",
        "IR Raw Capture",
        "IR Replay Last",
        "Saved IR",
        "IR Remotes",
        "IR TX Test"
    };

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenu("SIGNAL TOOLS", items,
                                sizeof(items) / sizeof(char*));
        switch (choice) {
            case -1: exitSub = true;      break;
            case  0: runHardwareDiag();   break;
            case  1: runInputMonitor();   break;
            case  2: runIrAnalyzer();     break;
            case  3: runIrProtocolScanner(); break;
            case  4: runIrSniffer();      break;
            case  5: runIrRawCapture();   break;
            case  6: runIrReplayLast();   break;
            case  7: runSavedIrCaptures(); break;
            case  8: runIrVirtualRemotes(); break;
            case  9: runIrTxTest();       break;
        }
    }

    // Leave the IR hardware in a known idle state when we leave the submenu.
    irRxMode();
}
