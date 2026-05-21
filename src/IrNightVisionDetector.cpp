#include "IrNightVisionDetector.h"

#include <Arduino.h>

#include "PepeDraw.h"
#include "Pins.h"

static constexpr uint32_t NV_MIN_PULSE_US = 120;
static constexpr uint16_t NV_DRAW_MS = 250;
static constexpr uint16_t NV_ACTIVE_MS = 700;

struct NightVisionState {
    int level = HIGH;
    uint32_t lastChangeUs = 0;
    unsigned long lastPulseMs = 0;
    unsigned long windowStartMs = 0;
    unsigned long windowPulses = 0;
    unsigned long windowEdges = 0;
    uint32_t windowLowUs = 0;
    uint32_t longestLowUs = 0;
    unsigned long totalPulses = 0;
    uint16_t pulsesPerSec = 0;
    uint16_t edgesPerSec = 0;
    uint16_t dutyPermille = 0;
};

struct NightVisionView {
    bool initialized = false;
    String status;
    uint8_t bars = 255;
    uint16_t pulsesPerSec = 65535;
    uint16_t edgesPerSec = 65535;
    uint16_t dutyPermille = 65535;
    uint32_t longestLowUs = 0xFFFFFFFF;
    unsigned long totalPulses = 0xFFFFFFFF;
    int level = -1;
};

static void prepareNightVisionPins() {
    ledcDetachPin(IR_TX_PIN);
    pinMode(IR_TX_PIN, OUTPUT);
    digitalWrite(IR_TX_PIN, HIGH);
    pinMode(IR_RX_PIN, INPUT);
}

static void initNightVisionState(NightVisionState& state) {
    state = NightVisionState();
    state.level = digitalRead(IR_RX_PIN);
    state.lastChangeUs = micros();
    state.windowStartMs = millis();
}

static void drawNightVisionFrame() {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    tft.drawFastHLine(0, 34, 320, TFT_WHITE);
    tft.drawFastHLine(0, 212, 320, TFT_WHITE);
    drawStringBig(10, 8, "IR NIGHT", TFT_WHITE, 1);
    drawStringCustom(12, 42, "Detects pulsed/modulated IR only.", TFT_CYAN, 1);
    drawStringCustom(10, 220, "UP/DN: RESET  OK: BACK", TFT_WHITE, 1);
}

static void processNightVisionSample(NightVisionState& state) {
    uint32_t nowUs = micros();
    int level = digitalRead(IR_RX_PIN);

    if (level != state.level) {
        uint32_t duration = nowUs - state.lastChangeUs;
        int previousLevel = state.level;

        state.windowEdges++;
        if (previousLevel == LOW && duration >= NV_MIN_PULSE_US) {
            state.windowPulses++;
            state.totalPulses++;
            state.windowLowUs += duration;
            if (duration > state.longestLowUs) state.longestLowUs = duration;
            state.lastPulseMs = millis();
        }

        state.level = level;
        state.lastChangeUs = nowUs;
    }

    unsigned long nowMs = millis();
    unsigned long elapsed = nowMs - state.windowStartMs;
    if (elapsed >= 1000) {
        state.pulsesPerSec = static_cast<uint16_t>(
            min<unsigned long>(999, (state.windowPulses * 1000UL) / elapsed));
        state.edgesPerSec = static_cast<uint16_t>(
            min<unsigned long>(999, (state.windowEdges * 1000UL) / elapsed));
        state.dutyPermille = static_cast<uint16_t>(
            min<uint32_t>(999, state.windowLowUs / elapsed));

        state.windowPulses = 0;
        state.windowEdges = 0;
        state.windowLowUs = 0;
        state.windowStartMs = nowMs;
    }
}

static String nightVisionStatus(const NightVisionState& state) {
    bool active = (millis() - state.lastPulseMs) < NV_ACTIVE_MS;
    bool heldLow = digitalRead(IR_RX_PIN) == LOW &&
                   (micros() - state.lastChangeUs) > 30000UL;

    if (heldLow) return "SATURATED";
    if (!active) return "IDLE";
    if (state.pulsesPerSec >= 25 || state.dutyPermille >= 120) return "STRONG IR";
    if (state.pulsesPerSec >= 3) return "PULSED IR";
    return "WEAK IR";
}

static uint16_t statusColor(const String& status) {
    if (status == "SATURATED") return TFT_RED;
    if (status == "STRONG IR") return TFT_YELLOW;
    if (status == "PULSED IR") return TFT_GREEN;
    if (status == "WEAK IR") return TFT_CYAN;
    return TFT_DARKGREY;
}

static uint8_t nightVisionBars(const NightVisionState& state,
                               const String& status) {
    if (status == "IDLE") return 0;
    if (status == "SATURATED") return 10;

    uint16_t score = state.pulsesPerSec / 3 + state.dutyPermille / 25;
    if (score == 0) score = 1;
    if (score > 10) score = 10;
    return static_cast<uint8_t>(score);
}

static void drawNightVisionBar(uint8_t bars, uint16_t color) {
    for (uint8_t i = 0; i < 10; i++) {
        int x = 18 + i * 18;
        tft.drawRect(x, 86, 13, 16, TFT_WHITE);
        tft.fillRect(x + 2, 88, 9, 12, i < bars ? color : TFT_BLACK);
    }
}

static void drawNightVisionLive(const NightVisionState& state,
                                NightVisionView& view) {
    String status = nightVisionStatus(state);
    uint16_t color = statusColor(status);
    uint8_t bars = nightVisionBars(state, status);
    int level = digitalRead(IR_RX_PIN);

    if (!view.initialized || view.status != status) {
        tft.fillRect(16, 58, 190, 22, TFT_BLACK);
        drawStringCustom(18, 60, status, color, 2);
        view.status = status;
    }

    if (!view.initialized || view.level != level) {
        tft.fillRect(224, 62, 70, 14, TFT_BLACK);
        drawStringCustom(224, 64, "LVL " + String(level), TFT_WHITE, 1);
        view.level = level;
    }

    if (!view.initialized || view.bars != bars || view.status != status) {
        drawNightVisionBar(bars, color);
        view.bars = bars;
    }

    if (!view.initialized ||
        view.pulsesPerSec != state.pulsesPerSec ||
        view.edgesPerSec != state.edgesPerSec ||
        view.dutyPermille != state.dutyPermille ||
        view.longestLowUs != state.longestLowUs ||
        view.totalPulses != state.totalPulses) {
        tft.fillRect(12, 118, 296, 86, TFT_BLACK);
        drawStringCustom(16, 122, "PULSES/S : " + String(state.pulsesPerSec),
                         TFT_WHITE, 1);
        drawStringCustom(166, 122, "EDGES/S: " + String(state.edgesPerSec),
                         TFT_WHITE, 1);
        drawStringCustom(16, 140, "LOW DUTY : " +
                         String(state.dutyPermille / 10.0f, 1) + "%",
                         TFT_WHITE, 1);
        drawStringCustom(166, 140, "TOTAL: " + String(state.totalPulses),
                         TFT_WHITE, 1);
        drawStringCustom(16, 158, "LONG LOW : " +
                         String(state.longestLowUs) + " us",
                         TFT_DARKGREY, 1);
        drawStringFit(16, 182, "Steady IR may not show on this receiver.",
                      TFT_DARKGREY, 286, 1);

        view.pulsesPerSec = state.pulsesPerSec;
        view.edgesPerSec = state.edgesPerSec;
        view.dutyPermille = state.dutyPermille;
        view.longestLowUs = state.longestLowUs;
        view.totalPulses = state.totalPulses;
    }

    view.initialized = true;
}

void runIrNightVisionDetector() {
    prepareNightVisionPins();

    NightVisionState state;
    NightVisionView view;
    initNightVisionState(state);
    drawNightVisionFrame();

    unsigned long lastDraw = 0;
    while (true) {
        processNightVisionSample(state);

        if (millis() - lastDraw >= NV_DRAW_MS) {
            drawNightVisionLive(state, view);
            lastDraw = millis();
        }

        if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
            while (digitalRead(BTN_UP) == LOW ||
                   digitalRead(BTN_DOWN) == LOW) delay(5);
            initNightVisionState(state);
            view = NightVisionView();
            drawNightVisionFrame();
            lastDraw = 0;
        }

        if (digitalRead(BTN_OK) == LOW) {
            while (digitalRead(BTN_OK) == LOW) delay(5);
            delay(60);
            return;
        }

        delayMicroseconds(100);
    }
}
