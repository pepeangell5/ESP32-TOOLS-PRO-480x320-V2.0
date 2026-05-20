#include "IrSniffer.h"

#include <Arduino.h>

#include "IrProtocolDecoder.h"
#include "PepeDraw.h"
#include "Pins.h"

static constexpr uint16_t SNIFF_RAW_MAX       = 768;
static constexpr uint8_t  SNIFF_LOG_MAX       = 5;
static constexpr uint32_t SNIFF_MIN_PULSE_US  = 220;
static constexpr uint32_t SNIFF_FRAME_GAP_US  = 25000;
static constexpr uint32_t SNIFF_FRAME_MAX_US  = 220000;
static constexpr uint16_t SNIFF_REPEAT_MS     = 700;
static constexpr uint16_t SNIFF_DRAW_MS       = 180;

struct SniffLogEntry {
    String protocol;
    String code;
    uint16_t count = 0;
    uint32_t totalUs = 0;
    uint16_t gapMs = 0;
    bool overflow = false;
    bool repeat = false;
};

struct SniffState {
    int level = HIGH;
    bool frameActive = false;
    bool overflow = false;
    uint16_t raw[SNIFF_RAW_MAX];
    uint16_t rawCount = 0;
    uint32_t lastChangeUs = 0;
    uint32_t lastEdgeUs = 0;
    uint32_t frameStartUs = 0;
    unsigned long lastFrameMs = 0;
    unsigned long totalFrames = 0;
    unsigned long totalNoise = 0;
    SniffLogEntry log[SNIFF_LOG_MAX];
    uint8_t logCount = 0;
    bool dirty = true;
};

static void prepareSnifferPins() {
    ledcDetachPin(IR_TX_PIN);
    pinMode(IR_TX_PIN, OUTPUT);
    digitalWrite(IR_TX_PIN, HIGH);
    pinMode(IR_RX_PIN, INPUT);
}

static void drawSnifferFrame() {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    tft.drawFastHLine(0, 34, 320, TFT_WHITE);
    tft.drawFastHLine(0, 212, 320, TFT_WHITE);
    drawStringBig(10, 8, "IR SNIFFER", TFT_WHITE, 1);
    drawStringCustom(10, 220, "UP/DN: CLEAR  OK: BACK", TFT_WHITE, 1);
}

static void initSniffState(SniffState& state) {
    state = SniffState();
    state.level = digitalRead(IR_RX_PIN);
    state.lastChangeUs = micros();
    state.lastEdgeUs = state.lastChangeUs;
    state.dirty = true;
}

static void appendSniffDuration(SniffState& state, uint32_t duration) {
    if (state.rawCount < SNIFF_RAW_MAX) {
        if (duration > 65535) duration = 65535;
        state.raw[state.rawCount++] = static_cast<uint16_t>(duration);
    } else {
        state.overflow = true;
    }
}

static void pushSniffLog(SniffState& state) {
    if (state.rawCount < 4) {
        state.totalNoise++;
        state.frameActive = false;
        state.overflow = false;
        state.rawCount = 0;
        state.dirty = true;
        return;
    }

    IrProtocolDecodeResult decoded = decodeIrProtocol(state.raw, state.rawCount);
    uint32_t totalUs = irRawTotalUs(state.raw, state.rawCount);
    unsigned long nowMs = millis();
    uint16_t gapMs = state.lastFrameMs == 0
        ? 0
        : static_cast<uint16_t>(min<unsigned long>(9999, nowMs - state.lastFrameMs));

    bool repeat = decoded.repeat;
    if (!repeat && state.logCount > 0 && gapMs > 0 && gapMs < SNIFF_REPEAT_MS) {
        repeat = (state.log[0].protocol == String(decoded.protocol) &&
                  state.log[0].code == decoded.code);
    }

    for (int i = SNIFF_LOG_MAX - 1; i > 0; i--) {
        state.log[i] = state.log[i - 1];
    }

    state.log[0].protocol = decoded.protocol;
    state.log[0].code = decoded.code;
    state.log[0].count = state.rawCount;
    state.log[0].totalUs = totalUs;
    state.log[0].gapMs = gapMs;
    state.log[0].overflow = state.overflow;
    state.log[0].repeat = repeat;

    if (state.logCount < SNIFF_LOG_MAX) state.logCount++;
    state.totalFrames++;
    state.lastFrameMs = nowMs;
    state.frameActive = false;
    state.overflow = false;
    state.rawCount = 0;
    state.dirty = true;
}

static void processSniffSample(SniffState& state) {
    uint32_t nowUs = micros();
    int level = digitalRead(IR_RX_PIN);

    if (level != state.level) {
        uint32_t duration = nowUs - state.lastChangeUs;
        int previousLevel = state.level;

        if (duration >= SNIFF_MIN_PULSE_US) {
            state.lastEdgeUs = nowUs;

            if (!state.frameActive && previousLevel == LOW) {
                state.frameActive = true;
                state.frameStartUs = nowUs - duration;
                state.rawCount = 0;
                state.overflow = false;
                appendSniffDuration(state, duration);
            } else if (state.frameActive) {
                appendSniffDuration(state, duration);
            }
        }

        state.level = level;
        state.lastChangeUs = nowUs;
    }

    if (state.frameActive &&
        ((nowUs - state.lastEdgeUs) > SNIFF_FRAME_GAP_US ||
         (nowUs - state.frameStartUs) > SNIFF_FRAME_MAX_US)) {
        pushSniffLog(state);
    }
}

static String trimCodeForLog(const String& code) {
    if (code.length() <= 12) return code;
    return code.substring(0, 12);
}

static void drawSnifferLog(const SniffState& state) {
    tft.fillRect(1, 36, 318, 175, TFT_BLACK);
    drawStringCustom(10, 42, "FRAMES: " + String(state.totalFrames) +
                     "  NOISE: " + String(state.totalNoise),
                     TFT_CYAN, 1);

    if (state.logCount == 0) {
        drawStringCustom(22, 92, "Listening for IR frames...", TFT_WHITE, 1);
        drawStringCustom(22, 110, "Press remote buttons now.", TFT_WHITE, 1);
        return;
    }

    for (uint8_t i = 0; i < state.logCount; i++) {
        const SniffLogEntry& entry = state.log[i];
        int y = 62 + i * 29;
        uint16_t color = entry.overflow ? TFT_RED :
                         entry.repeat ? TFT_YELLOW :
                         entry.protocol == "RAW" ? TFT_DARKGREY : TFT_GREEN;

        tft.drawFastHLine(8, y - 4, 304, TFT_DARKGREY);
        drawStringCustom(12, y, String(i + 1) + " " + entry.protocol,
                         color, 1);
        drawStringFit(106, y, trimCodeForLog(entry.code), TFT_WHITE, 104, 1);
        drawStringCustom(218, y, entry.repeat ? "REP" : "NEW",
                         entry.repeat ? TFT_YELLOW : TFT_WHITE, 1);
        drawStringCustom(12, y + 13,
                         "cnt " + String(entry.count) +
                         "  " + String(entry.totalUs / 1000.0f, 1) + "ms" +
                         "  gap " + String(entry.gapMs) + "ms",
                         TFT_WHITE, 1);
    }
}

void runIrSniffer() {
    prepareSnifferPins();

    SniffState state;
    initSniffState(state);
    drawSnifferFrame();

    unsigned long lastDraw = 0;
    while (true) {
        processSniffSample(state);

        if (state.dirty || millis() - lastDraw >= SNIFF_DRAW_MS) {
            if (state.dirty) {
                drawSnifferLog(state);
                state.dirty = false;
            }
            lastDraw = millis();
        }

        if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
            while (digitalRead(BTN_UP) == LOW ||
                   digitalRead(BTN_DOWN) == LOW) delay(5);
            initSniffState(state);
            drawSnifferFrame();
            drawSnifferLog(state);
            lastDraw = millis();
        }

        if (digitalRead(BTN_OK) == LOW) {
            while (digitalRead(BTN_OK) == LOW) delay(5);
            delay(60);
            return;
        }

        delayMicroseconds(90);
    }
}
