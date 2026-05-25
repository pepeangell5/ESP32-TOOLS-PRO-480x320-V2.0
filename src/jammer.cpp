#include "jammer.h"

#include <RF24.h>
#include <SPI.h>

#include "DisplayTFT.h"
#include "PepeDraw.h"
#include "Pins.h"

extern DisplayTFT tft;

static RF24 jam1(NRF1_CE_PIN, NRF1_CSN_PIN);
static RF24 jam2(NRF2_CE_PIN, NRF2_CSN_PIN);
static bool jam1Ok = false;
static bool jam2Ok = false;

static int jamChannel = 1;
static bool isAttacking = false;
static bool exitRequested = false;

static const uint8_t noisePayload[32] = {
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA
};

static uint8_t wifiChannelToNrf(int channel) {
    return (uint8_t)((channel * 5) + 2);
}

static void configureRadio(RF24& radio) {
    radio.powerUp();
    radio.setAddressWidth(3);
    radio.setRetries(0, 0);
    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_MAX);
    radio.setCRCLength(RF24_CRC_DISABLED);
    radio.setAutoAck(false);
    radio.openWritingPipe((uint8_t*)"JAM");
    radio.stopListening();
}

static int activeRadioCount() {
    return (jam1Ok ? 1 : 0) + (jam2Ok ? 1 : 0);
}

static void stopAttack() {
    isAttacking = false;
    if (jam1Ok) jam1.stopConstCarrier();
    if (jam2Ok) jam2.stopConstCarrier();
}

static void drawHeader(const char* title, const String& status, uint16_t color) {
    tft.fillRect(0, 0, 320, 36, color);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    drawStringBig(10, 10, title, color == TFT_WHITE ? TFT_BLACK : TFT_WHITE, 1);
    drawStringRight(305, 14, status, color == TFT_WHITE ? TFT_BLACK : TFT_WHITE, 1);
    tft.drawFastHLine(0, 36, 320, TFT_WHITE);
}

static void drawChannelGauge() {
    tft.fillScreen(TFT_BLACK);
    drawHeader("JAMMER CANAL", isAttacking ? "ACTIVO" : "LISTO",
               isAttacking ? TFT_RED : TFT_WHITE);

    String chText = "CH " + String(jamChannel);
    drawStringCentered(56, chText, TFT_YELLOW, 3, FONT_BIG);
    drawStringCentered(100,
        String(2400 + wifiChannelToNrf(jamChannel)) + " MHz NRF",
        TFT_CYAN, 1, FONT_SMALL);

    int pct = 7 + ((jamChannel - 1) * 93) / 13;
    tft.drawRect(20, 124, 280, 14, TFT_WHITE);
    tft.fillRect(22, 126, ((276 * pct) / 100), 10,
                 isAttacking ? TFT_RED : TFT_GREEN);

    drawStringCustom(22, 154, "RADIOS: " + String(activeRadioCount()) + "/2",
                     activeRadioCount() > 0 ? TFT_GREEN : TFT_RED, 1);

    if (isAttacking) {
        uint8_t frame = (millis() / 70) & 0xFF;
        for (int i = 0; i < 24; i++) {
            int h = 4 + ((frame + i * 5) % 34);
            uint16_t c = h > 24 ? TFT_RED : TFT_YELLOW;
            tft.fillRect(22 + i * 12, 202 - h, 7, h, c);
        }
    } else {
        tft.drawRect(90, 166, 140, 26, TFT_WHITE);
        drawStringCentered(174, "OK: START", TFT_GREEN, 1, FONT_SMALL);
    }

    tft.drawFastHLine(0, 214, 320, TFT_WHITE);
    drawStringCustom(8, 222, "UP/DN: CANAL", TFT_WHITE, 1);
    drawStringRight(312, 222, "OK(HOLD): BACK", TFT_WHITE, 1);
}

static void drawChannelBars() {
    tft.fillRect(18, 164, 292, 40, TFT_BLACK);
    uint8_t frame = (millis() / 70) & 0xFF;
    for (int i = 0; i < 24; i++) {
        int h = 4 + ((frame + i * 5) % 34);
        uint16_t c = h > 24 ? TFT_RED : TFT_YELLOW;
        tft.fillRect(22 + i * 12, 202 - h, 7, h, c);
    }
}

void jammerSetup() {
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

    jam1Ok = jam1.begin();
    if (jam1Ok) configureRadio(jam1);

    jam2Ok = jam2.begin();
    if (jam2Ok) configureRadio(jam2);

    Serial.printf("[jammer] NRF1 CE:%d CSN:%d -> %s\n",
                  NRF1_CE_PIN, NRF1_CSN_PIN, jam1Ok ? "OK" : "FAIL");
    Serial.printf("[jammer] NRF2 CE:%d CSN:%d -> %s\n",
                  NRF2_CE_PIN, NRF2_CSN_PIN, jam2Ok ? "OK" : "FAIL");
}

void jammerLoop() {
    if (digitalRead(BTN_UP) == LOW) {
        jamChannel = (jamChannel == 14) ? 1 : jamChannel + 1;
        if (isAttacking && jam1Ok) {
            jam1.startConstCarrier(RF24_PA_MAX, wifiChannelToNrf(jamChannel));
        }
        drawChannelGauge();
        delay(180);
    }

    if (digitalRead(BTN_DOWN) == LOW) {
        jamChannel = (jamChannel == 1) ? 14 : jamChannel - 1;
        if (isAttacking && jam1Ok) {
            jam1.startConstCarrier(RF24_PA_MAX, wifiChannelToNrf(jamChannel));
        }
        drawChannelGauge();
        delay(180);
    }

    if (digitalRead(BTN_OK) == LOW) {
        bool held = waitOkReleaseWasLong();
        if (held) {
            stopAttack();
            exitRequested = true;
            return;
        }

        isAttacking = !isAttacking;
        uint8_t freq = wifiChannelToNrf(jamChannel);
        if (isAttacking) {
            if (jam1Ok) jam1.startConstCarrier(RF24_PA_MAX, freq);
            if (jam2Ok) jam2.setChannel(freq);
        } else {
            stopAttack();
        }
        drawChannelGauge();
        delay(220);
    }

    if (isAttacking) {
        uint8_t freq = wifiChannelToNrf(jamChannel);
        if (jam2Ok) {
            jam2.setChannel(freq);
            for (int i = 0; i < 20; i++) {
                jam2.startWrite(noisePayload, sizeof(noisePayload), true);
            }
        }

        static unsigned long lastDraw = 0;
        if (millis() - lastDraw > 220) {
            drawChannelBars();
            lastDraw = millis();
        }
    }
}

void runJammer() {
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);

    jammerSetup();
    exitRequested = false;
    drawChannelGauge();

    if (activeRadioCount() == 0) {
        drawStringCentered(112, "NRF24 ERROR", TFT_RED, 2, FONT_BIG);
        drawStringCentered(150, "Revisa CE/CSN/SPI", TFT_WHITE, 1, FONT_SMALL);
        delay(2500);
        return;
    }

    while (!exitRequested) {
        jammerLoop();
        delay(5);
    }

    stopAttack();
    if (jam1Ok) jam1.powerDown();
    if (jam2Ok) jam2.powerDown();
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(100);
}
