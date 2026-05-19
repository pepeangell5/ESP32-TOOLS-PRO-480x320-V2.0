#include <Arduino.h>
#include <SPI.h>
#include "DisplayTFT.h"
#include "PepeDraw.h"
#include "MenuSystem.h"
#include "Pins.h"
#include "Settings.h"
#include "NVSStore.h"
#include "SplashScreen.h"

// ═══════════════════════════════════════════════════════════════════════════
//  ESP32-TOOLS · Firmware principal
//  El main.cpp solo inicializa hardware y entrega el control al menú.
// ═══════════════════════════════════════════════════════════════════════════

DisplayTFT tft;

// ── Carga todas las preferencias desde NVS a las variables globales ──────
static void loadPreferences() {
    soundEnabled = nvsGetBool("sound_on",  true);
    soundVolume  = nvsGetInt ("sound_vol", 3);
    if (soundVolume < 1) soundVolume = 1;
    if (soundVolume > 5) soundVolume = 5;
}

// ── Incrementa contador de arranques (útil para System Info después) ─────
static void bumpBootCount() {
    unsigned long bc = nvsGetULong("boot_cnt", 0);
    bc++;
    nvsSetULong("boot_cnt", bc);
    Serial.printf("[NVS] Boot count: %lu\n", bc);
}

void setup() {
    Serial.begin(115200);

    // ── Botones ─────────────────────────────────────────────────────────
    pinMode(BTN_UP,   INPUT_PULLUP);
    pinMode(BTN_OK,   INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);

    // ── SPI shared devices ──────────────────────────────────────────────
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

    pinMode(IR_TX_PIN, OUTPUT);
    digitalWrite(IR_TX_PIN, HIGH);
    pinMode(IR_RX_PIN, INPUT);
    pinMode(CC1101_CSN_PIN, OUTPUT);
    digitalWrite(CC1101_CSN_PIN, HIGH);
    pinMode(CC1101_GDO0_PIN, INPUT);

    pinMode(TFT_LED_PIN, OUTPUT);
    digitalWrite(TFT_LED_PIN, HIGH);

#if BUZZER_PIN >= 0
    ledcSetup(0, 2000, 8);
    ledcAttachPin(BUZZER_PIN, 0);
    ledcWriteTone(0, 0);
#endif

    // ── NVS: cargar configuración guardada ──────────────────────────────
    nvsBegin();
    loadPreferences();
    bumpBootCount();

    // ── Reset pantalla ──────────────────────────────────────────────────
    pinMode(TFT_RST_PIN, OUTPUT);
    digitalWrite(TFT_RST_PIN, LOW);  delay(100);
    digitalWrite(TFT_RST_PIN, HIGH); delay(100);

    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN);
    tft.begin();
    tft.setRotation(3);

    tft.fillScreen(TFT_BLACK);

    // ── Splash screen (espera a que usuario presione OK) ────────────────
    runSplashScreen();

    // ── Menú principal (bucle infinito, nunca regresa) ──────────────────
    runMainMenu();

    // ── Menú principal (bucle infinito, nunca regresa) ──────────────────
}

void loop() {
    delay(1000);
}
