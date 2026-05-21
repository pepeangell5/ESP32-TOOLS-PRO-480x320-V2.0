#include "IrProximityTest.h"

#include <Arduino.h>

#include "PepeDraw.h"
#include "Pins.h"

static constexpr int PROX_LEDC_CHANNEL = 2;
static constexpr int PROX_LEDC_FREQ = 38000;
static constexpr int PROX_LEDC_RESOLUTION = 8;
static constexpr int PROX_MARK_DUTY = 170;
static constexpr int PROX_SPACE_DUTY = 255;

static constexpr uint8_t PROX_BURSTS = 7;
static constexpr uint16_t PROX_MARK_US = 650;
static constexpr uint16_t PROX_SPACE_US = 950;
static constexpr uint16_t PROX_SAMPLE_STEP_US = 35;
static constexpr uint8_t PROX_CAL_SAMPLES = 24;
static constexpr uint16_t PROX_DRAW_MS = 220;
static constexpr uint8_t PROX_MIN_THRESHOLD = 4;
static constexpr uint8_t PROX_THRESHOLD_DIVISOR = 32;

struct ProximityState {
    uint16_t baseline = 0;
    uint16_t score = 0;
    int16_t delta = 0;
    uint16_t change = 0;
    uint16_t threshold = PROX_MIN_THRESHOLD;
    uint16_t peak = 0;
    uint8_t bars = 0;
    String status = "CAL";
    unsigned long sampleCount = 0;
};

struct ProximityView {
    bool initialized = false;
    uint16_t baseline = 65535;
    uint16_t score = 65535;
    int16_t delta = -32768;
    uint16_t change = 65535;
    uint16_t threshold = 65535;
    uint16_t peak = 65535;
    uint8_t bars = 255;
    String status;
    unsigned long sampleCount = 0xFFFFFFFF;
};

static void txBegin() {
    pinMode(IR_TX_PIN, OUTPUT);
    digitalWrite(IR_TX_PIN, HIGH);
    ledcSetup(PROX_LEDC_CHANNEL, PROX_LEDC_FREQ, PROX_LEDC_RESOLUTION);
    ledcAttachPin(IR_TX_PIN, PROX_LEDC_CHANNEL);
    ledcWrite(PROX_LEDC_CHANNEL, PROX_SPACE_DUTY);
    pinMode(IR_RX_PIN, INPUT);
}

static void txEnd() {
    ledcWrite(PROX_LEDC_CHANNEL, PROX_SPACE_DUTY);
    ledcDetachPin(IR_TX_PIN);
    pinMode(IR_TX_PIN, OUTPUT);
    digitalWrite(IR_TX_PIN, HIGH);
    pinMode(IR_RX_PIN, INPUT);
}

static void drawProximityFrame() {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    tft.drawFastHLine(0, 34, 320, TFT_WHITE);
    tft.drawFastHLine(0, 212, 320, TFT_WHITE);
    drawStringBig(10, 8, "IR PROX", TFT_WHITE, 1);
    drawStringCustom(12, 42, "Experimental reflected-burst test.", TFT_CYAN, 1);
    drawStringCustom(10, 220, "UP/DN: CAL  OK: BACK", TFT_WHITE, 1);
}

static uint16_t sampleReceiverWindow(uint16_t durationUs) {
    uint16_t score = 0;
    int lastLevel = digitalRead(IR_RX_PIN);
    uint32_t start = micros();

    while ((uint32_t)(micros() - start) < durationUs) {
        int level = digitalRead(IR_RX_PIN);
        if (level == LOW) score += 2;
        if (level != lastLevel) {
            score += 5;
            lastLevel = level;
        }
        delayMicroseconds(PROX_SAMPLE_STEP_US);
    }

    return score;
}

static uint16_t measureReflectionScore() {
    uint16_t score = 0;

    for (uint8_t i = 0; i < PROX_BURSTS; i++) {
        ledcWrite(PROX_LEDC_CHANNEL, PROX_MARK_DUTY);
        score += sampleReceiverWindow(PROX_MARK_US);

        ledcWrite(PROX_LEDC_CHANNEL, PROX_SPACE_DUTY);
        score += sampleReceiverWindow(PROX_SPACE_US);
    }

    ledcWrite(PROX_LEDC_CHANNEL, PROX_SPACE_DUTY);
    delayMicroseconds(600);
    return score;
}

static uint16_t calibrateBaseline() {
    drawProximityFrame();
    drawStringCustom(22, 80, "CALIBRATING", TFT_YELLOW, 2);
    drawStringCustom(22, 112, "Keep front area clear.", TFT_WHITE, 1);

    uint32_t total = 0;
    for (uint8_t i = 0; i < PROX_CAL_SAMPLES; i++) {
        total += measureReflectionScore();
        int fillW = ((i + 1) * 250) / PROX_CAL_SAMPLES;
        tft.drawRect(34, 150, 252, 10, TFT_WHITE);
        tft.fillRect(35, 151, fillW, 8, TFT_GREEN);
        delay(25);
    }

    return static_cast<uint16_t>(total / PROX_CAL_SAMPLES);
}

static uint16_t proximityThreshold(uint16_t baseline) {
    return max<uint16_t>(PROX_MIN_THRESHOLD, baseline / PROX_THRESHOLD_DIVISOR);
}

static String proximityStatus(uint16_t change, uint16_t threshold) {
    if (change < threshold) return "NONE";
    if (change < threshold * 2) return "MAYBE";
    if (change < threshold * 5) return "NEAR";
    return "CLOSE";
}

static uint8_t proximityBars(uint16_t change, uint16_t threshold) {
    if (change < threshold) return 0;
    uint16_t span = max<uint16_t>(1, threshold * 6);
    int16_t scaled = ((change - threshold) * 10) / span;
    if (scaled < 1) scaled = 1;
    if (scaled > 10) scaled = 10;
    return static_cast<uint8_t>(scaled);
}

static uint16_t proximityColor(const String& status) {
    if (status == "CLOSE") return TFT_RED;
    if (status == "NEAR") return TFT_YELLOW;
    if (status == "MAYBE") return TFT_CYAN;
    return TFT_DARKGREY;
}

static void drawProximityBar(uint8_t bars, uint16_t color) {
    for (uint8_t i = 0; i < 10; i++) {
        int x = 18 + i * 18;
        tft.drawRect(x, 92, 13, 18, TFT_WHITE);
        tft.fillRect(x + 2, 94, 9, 14, i < bars ? color : TFT_BLACK);
    }
}

static void updateProximityState(ProximityState& state) {
    state.score = measureReflectionScore();
    state.delta = static_cast<int16_t>(state.score) -
                  static_cast<int16_t>(state.baseline);
    state.change = static_cast<uint16_t>(abs(state.delta));
    if (state.change > state.peak) state.peak = state.change;
    state.status = proximityStatus(state.change, state.threshold);
    state.bars = proximityBars(state.change, state.threshold);
    state.sampleCount++;
}

static void drawProximityLive(const ProximityState& state,
                              ProximityView& view) {
    uint16_t color = proximityColor(state.status);

    if (!view.initialized || view.status != state.status) {
        tft.fillRect(18, 62, 190, 24, TFT_BLACK);
        drawStringCustom(20, 64, state.status, color, 2);
        view.status = state.status;
    }

    if (!view.initialized || view.bars != state.bars ||
        view.status != state.status) {
        drawProximityBar(state.bars, color);
        view.bars = state.bars;
    }

    if (!view.initialized ||
        view.baseline != state.baseline ||
        view.score != state.score ||
        view.delta != state.delta ||
        view.change != state.change ||
        view.threshold != state.threshold ||
        view.peak != state.peak ||
        view.sampleCount != state.sampleCount) {
        tft.fillRect(12, 124, 296, 82, TFT_BLACK);
        drawStringCustom(18, 128, "BASE : " + String(state.baseline),
                         TFT_WHITE, 1);
        drawStringCustom(164, 128, "SCORE: " + String(state.score),
                         TFT_WHITE, 1);
        drawStringCustom(18, 146, "DELTA: " + String(state.delta),
                         color, 1);
        drawStringCustom(164, 146, "DIFF : " + String(state.change),
                         color, 1);
        drawStringCustom(18, 164, "THR  : " + String(state.threshold),
                         TFT_DARKGREY, 1);
        drawStringCustom(164, 164, "PEAK : " + String(state.peak),
                         TFT_DARKGREY, 1);
        drawStringFit(18, 186, "Detects score change, not real distance.",
                      TFT_DARKGREY, 286, 1);

        view.baseline = state.baseline;
        view.score = state.score;
        view.delta = state.delta;
        view.change = state.change;
        view.threshold = state.threshold;
        view.peak = state.peak;
        view.sampleCount = state.sampleCount;
    }

    view.initialized = true;
}

void runIrProximityTest() {
    txBegin();

    ProximityState state;
    ProximityView view;
    state.baseline = calibrateBaseline();
    state.threshold = proximityThreshold(state.baseline);
    drawProximityFrame();

    unsigned long lastDraw = 0;
    while (true) {
        updateProximityState(state);

        if (millis() - lastDraw >= PROX_DRAW_MS) {
            drawProximityLive(state, view);
            lastDraw = millis();
        }

        if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
            while (digitalRead(BTN_UP) == LOW ||
                   digitalRead(BTN_DOWN) == LOW) delay(5);
            state = ProximityState();
            view = ProximityView();
            state.baseline = calibrateBaseline();
            state.threshold = proximityThreshold(state.baseline);
            drawProximityFrame();
            lastDraw = 0;
        }

        if (digitalRead(BTN_OK) == LOW) {
            while (digitalRead(BTN_OK) == LOW) delay(5);
            delay(60);
            txEnd();
            return;
        }
    }
}
