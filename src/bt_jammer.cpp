#include "bt_jammer.h"

#include <RF24.h>
#include <SPI.h>

#include "DisplayTFT.h"
#include "PepeDraw.h"
#include "Pins.h"

extern DisplayTFT tft;

static RF24 btJam1(NRF1_CE_PIN, NRF1_CSN_PIN);
static RF24 btJam2(NRF2_CE_PIN, NRF2_CSN_PIN);
static bool btJam1Ok = false;
static bool btJam2Ok = false;
static bool isBtJamming = false;
static bool exitRequested = false;
static uint8_t btFrame = 0;
static bool backlightPwmActive = false;

static constexpr int BT_BL_LEDC_CHANNEL = 7;
static constexpr int BT_BL_LEDC_FREQ = 5000;
static constexpr int BT_BL_LEDC_RESOLUTION = 8;
static constexpr uint8_t BT_BL_MIN_DUTY = 165;
static constexpr uint8_t BT_BL_MAX_DUTY = 255;
static constexpr unsigned long BT_BL_PULSE_MS = 1300;

static const uint8_t hoppingChannels[] = {
    2, 4, 6, 8, 10, 12, 14, 16, 18, 20,
    22, 24, 26, 28, 30, 32, 34, 36, 38, 40,
    42, 44, 46, 48, 50, 52, 54, 56, 58, 60,
    62, 64, 66, 68, 70, 72, 74, 76, 78, 80
};
static const int totalBtChans =
    sizeof(hoppingChannels) / sizeof(hoppingChannels[0]);

static void configureBtRadio(RF24& radio) {
    radio.powerUp();
    radio.setAutoAck(false);
    radio.setPALevel(RF24_PA_MAX, true);
    radio.setDataRate(RF24_1MBPS);
    radio.setCRCLength(RF24_CRC_DISABLED);
    radio.setRetries(0, 0);
    radio.stopListening();
}

static int activeBtRadioCount() {
    return (btJam1Ok ? 1 : 0) + (btJam2Ok ? 1 : 0);
}

static void drawBtScreen();

static void beginBacklightPulse() {
    if (!backlightPwmActive) {
        ledcSetup(BT_BL_LEDC_CHANNEL, BT_BL_LEDC_FREQ, BT_BL_LEDC_RESOLUTION);
        ledcAttachPin(TFT_LED_PIN, BT_BL_LEDC_CHANNEL);
        backlightPwmActive = true;
    }
    ledcWrite(BT_BL_LEDC_CHANNEL, BT_BL_MAX_DUTY);
}

static void updateBacklightPulse() {
    if (!backlightPwmActive) return;

    unsigned long phase = millis() % BT_BL_PULSE_MS;
    unsigned long half = BT_BL_PULSE_MS / 2;
    unsigned long ramp = (phase < half) ? phase : (BT_BL_PULSE_MS - phase);
    uint8_t duty = BT_BL_MIN_DUTY +
        ((BT_BL_MAX_DUTY - BT_BL_MIN_DUTY) * ramp) / half;
    ledcWrite(BT_BL_LEDC_CHANNEL, duty);
}

static void restoreBacklight() {
    if (backlightPwmActive) {
        ledcWrite(BT_BL_LEDC_CHANNEL, BT_BL_MAX_DUTY);
        ledcDetachPin(TFT_LED_PIN);
        backlightPwmActive = false;
    }
    pinMode(TFT_LED_PIN, OUTPUT);
    digitalWrite(TFT_LED_PIN, HIGH);
}

static void startBtJammer() {
    isBtJamming = true;

    // Draw before enabling carriers so the TFT does not steal SPI time mid-attack.
    drawBtScreen();
    beginBacklightPulse();

    if (btJam1Ok) {
        configureBtRadio(btJam1);
        btJam1.startConstCarrier(RF24_PA_MAX, hoppingChannels[0]);
    }
    if (btJam2Ok) {
        configureBtRadio(btJam2);
        btJam2.startConstCarrier(RF24_PA_MAX, hoppingChannels[totalBtChans - 1]);
    }
}

static void stopBtJammer() {
    isBtJamming = false;
    if (btJam1Ok) btJam1.stopConstCarrier();
    if (btJam2Ok) btJam2.stopConstCarrier();
    restoreBacklight();
}

static void drawBtGlyph(int x, int y, uint8_t frame) {
    tft.drawLine(x + 16, y + 4, x + 16, y + 52, TFT_CYAN);
    tft.drawLine(x + 16, y + 4, x + 36, y + 16, TFT_CYAN);
    tft.drawLine(x + 36, y + 16, x + 16, y + 28, TFT_CYAN);
    tft.drawLine(x + 16, y + 28, x + 36, y + 40, TFT_CYAN);
    tft.drawLine(x + 36, y + 40, x + 16, y + 52, TFT_CYAN);
    tft.drawLine(x + 4, y + 16, x + 48, y + 44, TFT_WHITE);
    tft.drawLine(x + 4, y + 40, x + 48, y + 12, TFT_WHITE);
    if ((frame / 4) % 2 == 0) {
        tft.drawCircle(x + 16, y + 28, 26, TFT_BLUE);
        tft.drawCircle(x + 16, y + 28, 32, TFT_CYAN);
    }
}

static void drawBtScreen() {
    uint8_t frame = btFrame++;
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    tft.fillRect(1, 1, 318, 36, isBtJamming ? TFT_BLUE : TFT_WHITE);
    drawStringBig(10, 10, "BT JAMMER", isBtJamming ? TFT_WHITE : TFT_BLACK, 1);
    drawStringRight(306, 14, isBtJamming ? "ON" : "READY",
                    isBtJamming ? TFT_WHITE : TFT_BLACK, 1);
    tft.drawFastHLine(0, 36, 320, TFT_WHITE);

    drawBtGlyph(24, 68, frame);

    drawStringBig(112, 64, isBtJamming ? "MODO MAX" : "ESPECTRO", TFT_WHITE, 1);
    drawStringCustom(114, 88, isBtJamming ? "HOPPING 2.4GHz" : "BT LISTO",
                     isBtJamming ? TFT_RED : TFT_GREEN, 2);
    drawStringCustom(114, 116, "RADIOS: " + String(activeBtRadioCount()) + "/2",
                     activeBtRadioCount() > 0 ? TFT_GREEN : TFT_RED, 1);

    if (isBtJamming) {
        tft.fillRect(18, 140, 284, 58, TFT_BLACK);
        for (int i = 0; i < 18; i++) {
            int h = 5 + ((frame + i * 3) % 45);
            uint16_t c = (i % 3 == 0) ? TFT_CYAN : TFT_BLUE;
            tft.fillRect(26 + i * 15, 192 - h, 8, h, c);
        }
    } else {
        tft.drawRect(112, 148, 152, 28, TFT_WHITE);
        drawStringCustom(126, 157, "OK: START", TFT_GREEN, 2);
    }

    tft.drawFastHLine(0, 214, 320, TFT_WHITE);
    drawStringCustom(8, 222, "OK: TOGGLE", TFT_WHITE, 1);
    drawStringRight(312, 222, "OK(HOLD): BACK", TFT_WHITE, 1);
}

void btJammerSetup() {
    pinMode(TFT_CS_PIN, OUTPUT);
    digitalWrite(TFT_CS_PIN, HIGH);
    pinMode(NRF1_CSN_PIN, OUTPUT);
    digitalWrite(NRF1_CSN_PIN, HIGH);
    pinMode(NRF2_CSN_PIN, OUTPUT);
    digitalWrite(NRF2_CSN_PIN, HIGH);
    pinMode(NRF1_CE_PIN, OUTPUT);
    digitalWrite(NRF1_CE_PIN, LOW);
    pinMode(NRF2_CE_PIN, OUTPUT);
    digitalWrite(NRF2_CE_PIN, LOW);

    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN);
    delay(20);

    btJam1Ok = btJam1.begin();
    if (btJam1Ok) configureBtRadio(btJam1);

    btJam2Ok = btJam2.begin();
    if (btJam2Ok) configureBtRadio(btJam2);

    Serial.printf("[bt_jammer] NRF1 CE:%d CSN:%d -> %s\n",
                  NRF1_CE_PIN, NRF1_CSN_PIN, btJam1Ok ? "OK" : "FAIL");
    Serial.printf("[bt_jammer] NRF2 CE:%d CSN:%d -> %s\n",
                  NRF2_CE_PIN, NRF2_CSN_PIN, btJam2Ok ? "OK" : "FAIL");
}

void btJammerLoop() {
    if (digitalRead(BTN_OK) == LOW) {
        bool held = waitOkReleaseWasLong();
        if (held) {
            stopBtJammer();
            exitRequested = true;
            return;
        }

        isBtJamming = !isBtJamming;
        if (isBtJamming) {
            startBtJammer();
        } else {
            stopBtJammer();
            drawBtScreen();
        }
        delay(250);
    }

    if (isBtJamming) {
        for (int i = 0; i < totalBtChans; i++) {
            if (!isBtJamming) break;
            if (btJam1Ok) btJam1.setChannel(hoppingChannels[i]);
            if (btJam2Ok) btJam2.setChannel(hoppingChannels[totalBtChans - 1 - i]);

            if (digitalRead(BTN_OK) == LOW) {
                stopBtJammer();
                drawBtScreen();
                while (digitalRead(BTN_OK) == LOW) delay(5);
                delay(120);
                break;
            }
        }

        updateBacklightPulse();
    }
}

void runBTJammer() {
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);

    btJammerSetup();
    exitRequested = false;
    drawBtScreen();

    if (activeBtRadioCount() == 0) {
        drawStringCentered(112, "NRF24 ERROR", TFT_RED, 2, FONT_BIG);
        drawStringCentered(150, "Revisa CE/CSN/SPI", TFT_WHITE, 1, FONT_SMALL);
        delay(2500);
        return;
    }

    while (!exitRequested) {
        btJammerLoop();
        if (isBtJamming) yield();
        else delay(5);
    }

    stopBtJammer();
    if (btJam1Ok) btJam1.powerDown();
    if (btJam2Ok) btJam2.powerDown();
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);
}
