#pragma once

#include <Arduino.h>

// Shared SPI bus: TFT + nRF24 #1 + nRF24 #2
#define SCK_PIN   18
#define MOSI_PIN  23
#define MISO_PIN  19

// nRF24 #1
#define NRF1_CE_PIN   27
#define NRF1_CSN_PIN  14

// nRF24 #2
#define NRF2_CE_PIN   17
#define NRF2_CSN_PIN  16

// Backwards-compatible aliases. Existing scanner code uses CE_PIN/CSN_PIN.
#define CE_PIN   NRF1_CE_PIN
#define CSN_PIN  NRF1_CSN_PIN

// TFT SPI display
#define TFT_CS_PIN   5
#define TFT_RST_PIN  4
#define TFT_DC_PIN   22
#define TFT_LED_PIN  13

// Buttons, wired to GND when pressed
#define BTN_UP    32
#define BTN_OK    33
#define BTN_DOWN  25

// M5Stack IR Unit, verified with this module:
// module OUT is driven from ESP32 for TX, module IN is read by ESP32 for RX.
#define IR_TX_PIN 26
#define IR_RX_PIN 34

// CC1101 sub-GHz radio, sharing the SPI bus with TFT and nRF24 modules.
#define CC1101_CSN_PIN  21
#define CC1101_GDO0_PIN 35
#define CC1101_GDO2_PIN -1
// Optional lab TX data line for CC1101 async OOK replay.
// Wire CC1101 GDO0 to this GPIO as an extra jumper; keep GPIO35 for RX.
#define CC1101_TX_DATA_PIN 15

#define OK_LONGPRESS_MS 650

static inline bool waitOkReleaseWasLong(unsigned long holdMs = OK_LONGPRESS_MS) {
    unsigned long start = millis();
    bool wasLong = false;
    while (digitalRead(BTN_OK) == LOW) {
        if (millis() - start >= holdMs) wasLong = true;
        delay(5);
    }
    return wasLong;
}

// No buzzer pin was assigned in this wiring. GPIO 22 is TFT DC and GPIO 13 is
// TFT LED, so sound is disabled unless a free GPIO is assigned here later.
#define BUZZER_PIN -1
