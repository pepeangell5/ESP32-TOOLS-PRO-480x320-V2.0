#include "IrProtocolScanner.h"

#include <Arduino.h>

#include "IrProtocolDecoder.h"
#include "PepeDraw.h"
#include "Pins.h"

static constexpr uint16_t SCAN_RAW_MAX            = 768;
static constexpr uint32_t SCAN_CAPTURE_TIMEOUT_MS = 10000;
static constexpr uint32_t SCAN_GAP_US             = 25000;
static constexpr uint32_t SCAN_MAX_FRAME_US       = 220000;
static constexpr uint32_t SCAN_MIN_PULSE_US       = 220;
static constexpr uint32_t SCAN_START_MAX_US       = 25000;

static uint16_t scannerRaw[SCAN_RAW_MAX];
static uint16_t scannerRawCount = 0;
static bool scannerOverflow = false;

static void prepareScannerPins() {
    ledcDetachPin(IR_TX_PIN);
    pinMode(IR_TX_PIN, OUTPUT);
    digitalWrite(IR_TX_PIN, HIGH);
    pinMode(IR_RX_PIN, INPUT);
}

static void drawScannerFrame(const char* title) {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    tft.drawFastHLine(0, 34, 320, TFT_WHITE);
    tft.drawFastHLine(0, 212, 320, TFT_WHITE);
    drawStringBig(10, 8, title, TFT_WHITE, 1);
    drawStringCustom(10, 220, "OK: AGAIN  HOLD OK: BACK", TFT_WHITE, 1);
}

static bool captureScannerRaw() {
    scannerRawCount = 0;
    scannerOverflow = false;

    unsigned long waitStartMs = millis();
    bool started = false;

    while (!started) {
        if (digitalRead(BTN_OK) == LOW) return false;
        if (millis() - waitStartMs > SCAN_CAPTURE_TIMEOUT_MS) return false;

        if (digitalRead(IR_RX_PIN) == LOW) {
            uint32_t lowStart = micros();
            while (digitalRead(IR_RX_PIN) == LOW) {
                if (digitalRead(BTN_OK) == LOW) return false;
                if (millis() - waitStartMs > SCAN_CAPTURE_TIMEOUT_MS) {
                    return false;
                }
            }

            uint32_t lowDuration = micros() - lowStart;
            if (lowDuration >= SCAN_MIN_PULSE_US &&
                lowDuration <= SCAN_START_MAX_US) {
                if (lowDuration > 65535) lowDuration = 65535;
                scannerRaw[scannerRawCount++] =
                    static_cast<uint16_t>(lowDuration);
                started = true;
            }
        }
        delayMicroseconds(120);
    }

    int currentLevel = HIGH;
    uint32_t lastChange = micros();
    uint32_t lastEdge = lastChange;
    uint32_t captureStart = lastChange;

    while ((uint32_t)(micros() - lastEdge) < SCAN_GAP_US &&
           (uint32_t)(micros() - captureStart) < SCAN_MAX_FRAME_US) {
        int level = digitalRead(IR_RX_PIN);
        if (level != currentLevel) {
            uint32_t now = micros();
            uint32_t duration = now - lastChange;
            if (duration >= SCAN_MIN_PULSE_US) {
                if (scannerRawCount < SCAN_RAW_MAX) {
                    if (duration > 65535) duration = 65535;
                    scannerRaw[scannerRawCount++] =
                        static_cast<uint16_t>(duration);
                } else {
                    scannerOverflow = true;
                    break;
                }
                currentLevel = level;
                lastChange = now;
                lastEdge = now;
            }
        }
    }

    return scannerRawCount >= 4;
}

static void drawScanResult(const IrProtocolDecodeResult& result) {
    uint32_t total = irRawTotalUs(scannerRaw, scannerRawCount);

    drawScannerFrame("PROTOCOL SCAN");
    drawStringCustom(12, 46, "PROTOCOL", TFT_CYAN, 1);
    drawStringCustom(22, 64, result.protocol,
                     result.decoded ? TFT_GREEN : TFT_YELLOW, 2);

    drawStringCustom(12, 92, "CODE", TFT_CYAN, 1);
    drawStringFit(22, 110, result.code, TFT_WHITE, 278, 2);

    drawStringCustom(12, 140, "BITS: " + String(result.bits) +
                     "  CONF: " + String(result.confidence) + "%",
                     TFT_WHITE, 1);
    drawStringCustom(12, 156, "RAW: " + String(scannerRawCount) +
                     (scannerOverflow ? " FULL" : " OK") +
                     "  " + String(total / 1000.0f, 1) + " ms",
                     scannerOverflow ? TFT_RED : TFT_WHITE, 1);
    drawStringCustom(12, 172, result.detail, TFT_DARKGREY, 1);
    drawStringFit(12, 192, irRawPreview(scannerRaw, scannerRawCount),
                  TFT_WHITE, 296, 1);
}

static bool waitAgainOrBack() {
    while (digitalRead(BTN_OK) == HIGH) delay(20);
    bool held = waitOkReleaseWasLong();
    delay(60);
    return !held;
}

void runIrProtocolScanner() {
    prepareScannerPins();

    while (true) {
        drawScannerFrame("PROTOCOL SCAN");
        drawStringCustom(14, 54, "Point remote at IR receiver.", TFT_CYAN, 1);
        drawStringCustom(14, 74, "Press one button now.", TFT_CYAN, 1);
        drawStringCustom(14, 100, "Waiting up to 10 seconds...", TFT_WHITE, 1);
        drawStringCustom(14, 126, "Carrier frequency cannot be measured",
                         TFT_DARKGREY, 1);
        drawStringCustom(14, 140, "with this demodulated receiver.",
                         TFT_DARKGREY, 1);

        bool ok = captureScannerRaw();
        if (!ok) {
            drawScannerFrame("PROTOCOL SCAN");
            drawStringCustom(38, 92, "NO IR FRAME", TFT_RED, 2);
            drawStringCustom(18, 124, "Try again closer to receiver.",
                             TFT_WHITE, 1);
            if (!waitAgainOrBack()) return;
            continue;
        }

        IrProtocolDecodeResult result =
            decodeIrProtocol(scannerRaw, scannerRawCount);
        drawScanResult(result);
        if (!waitAgainOrBack()) return;
    }
}
