#include "CC1101Tools.h"

#include <Arduino.h>
#include <SPI.h>

#include "MenuSystem.h"
#include "PepeDraw.h"
#include "Pins.h"

static constexpr uint8_t CC_READ_SINGLE = 0x80;
static constexpr uint8_t CC_READ_BURST = 0xC0;

static constexpr uint8_t CC_IOCFG0 = 0x02;
static constexpr uint8_t CC_PKTCTRL1 = 0x07;
static constexpr uint8_t CC_PKTCTRL0 = 0x08;
static constexpr uint8_t CC_FSCTRL1 = 0x0B;
static constexpr uint8_t CC_FSCTRL0 = 0x0C;
static constexpr uint8_t CC_FREQ2 = 0x0D;
static constexpr uint8_t CC_FREQ1 = 0x0E;
static constexpr uint8_t CC_FREQ0 = 0x0F;
static constexpr uint8_t CC_MDMCFG4 = 0x10;
static constexpr uint8_t CC_MDMCFG3 = 0x11;
static constexpr uint8_t CC_MDMCFG2 = 0x12;
static constexpr uint8_t CC_MDMCFG1 = 0x13;
static constexpr uint8_t CC_MDMCFG0 = 0x14;
static constexpr uint8_t CC_DEVIATN = 0x15;
static constexpr uint8_t CC_MCSM0 = 0x18;
static constexpr uint8_t CC_FOCCFG = 0x19;
static constexpr uint8_t CC_BSCFG = 0x1A;
static constexpr uint8_t CC_AGCCTRL2 = 0x1B;
static constexpr uint8_t CC_AGCCTRL1 = 0x1C;
static constexpr uint8_t CC_AGCCTRL0 = 0x1D;
static constexpr uint8_t CC_FREND1 = 0x21;
static constexpr uint8_t CC_FREND0 = 0x22;
static constexpr uint8_t CC_PATABLE = 0x3E;
static constexpr uint8_t CC_TXFIFO = 0x3F;
static constexpr uint8_t CC_FSCAL3 = 0x23;
static constexpr uint8_t CC_FSCAL2 = 0x24;
static constexpr uint8_t CC_FSCAL1 = 0x25;
static constexpr uint8_t CC_FSCAL0 = 0x26;
static constexpr uint8_t CC_TEST2 = 0x2C;
static constexpr uint8_t CC_TEST1 = 0x2D;
static constexpr uint8_t CC_TEST0 = 0x2E;

static constexpr uint8_t CC_PARTNUM = 0x30;
static constexpr uint8_t CC_VERSION = 0x31;
static constexpr uint8_t CC_LQI = 0x33;
static constexpr uint8_t CC_RSSI = 0x34;
static constexpr uint8_t CC_MARCSTATE = 0x35;
static constexpr uint8_t CC_PKTSTATUS = 0x38;

static constexpr uint8_t CC_SRES = 0x30;
static constexpr uint8_t CC_SRX = 0x34;
static constexpr uint8_t CC_STX = 0x35;
static constexpr uint8_t CC_SIDLE = 0x36;
static constexpr uint8_t CC_SFRX = 0x3A;
static constexpr uint8_t CC_SFTX = 0x3B;
static constexpr uint8_t CC_SCAL = 0x33;

static constexpr uint8_t CC_GDO_CARRIER_SENSE = 0x0E;
static constexpr uint8_t CC_GDO_ASYNC_DATA = 0x0D;
static constexpr uint32_t CC_XTAL_HZ = 26000000UL;
static constexpr uint8_t SPECTRUM_MAX_POINTS = 48;
static constexpr int FINDER_MIN_DELTA_DB = 8;
static constexpr uint8_t FINDER_MIN_EDGES = 3;
static constexpr uint16_t FINDER_FINE_SPAN_KHZ = 350;
static constexpr uint16_t FINDER_FINE_STEP_KHZ = 25;
static constexpr uint16_t RF_RAW_MAX = 256;
static constexpr uint32_t RF_CAPTURE_TIMEOUT_MS = 8000;
static constexpr uint32_t RF_CAPTURE_GAP_US = 9000;
static constexpr uint32_t RF_CAPTURE_MAX_US = 260000;
static constexpr uint16_t RF_MIN_PULSE_US = 80;
static constexpr uint8_t RF_ARM_SAMPLES = 18;
static constexpr uint8_t RF_ARM_DELTA_DB = 12;
static constexpr uint16_t RF_ARM_TIMEOUT_MS = 10000;
static constexpr uint16_t RF_ARM_EDGE_WINDOW_US = 2500;
static constexpr uint8_t RF_ARM_MIN_EDGES = 3;
static constexpr uint16_t RF_MIN_RAW_PULSES = 24;
static constexpr uint32_t RF_MIN_CAPTURE_US = 5000;
static constexpr uint8_t RF_REPLAY_REPEATS = 6;
static constexpr uint16_t RF_REPLAY_GAP_US = 11000;
static constexpr uint8_t RF_RAW_VIEW_PULSES_PER_PAGE = 40;
static constexpr uint8_t RF_LIVE_MIN_EDGES = 2;
static constexpr int RF_LIVE_DELTA_DB = 10;
static constexpr uint8_t CC_PATABLE_0DBM = 0x60;

// Maximum-power beacon PATABLE values.
// CC1101 solo: aprox. +10 dBm max, sin amplificador externo.
// 433 MHz: 0xC0
// 915 MHz: 0xC2
static constexpr uint8_t CC_PATABLE_MAX_433 = 0xC0;
static constexpr uint8_t CC_PATABLE_MAX_915 = 0xC2;

// Beacon burst timing.
// Mas tiempo ON para que el analizador tenga mas oportunidad de detectarlo.
static constexpr uint16_t BEACON_BURST_MS  = 1000;
static constexpr uint16_t BEACON_GAP_MS    = 250;
static constexpr uint16_t BEACON_TOGGLE_US = 200; // ~2.5 kHz on TX data line

// Waterfall geometry (per-band history)
static constexpr uint8_t WF_ROWS = 96;
static constexpr uint8_t WF_COLS = SPECTRUM_MAX_POINTS;

// Brute search defaults
static constexpr uint8_t  BRUTE_MAX_BINS          = 96;
static constexpr uint16_t BRUTE_DEFAULT_SPAN_KHZ  = 2000;
static constexpr uint16_t BRUTE_DEFAULT_STEP_KHZ  = 25;
static constexpr uint8_t  BRUTE_DWELL_SAMPLES     = 4;
static constexpr uint8_t  BRUTE_TOP_HITS          = 6;

static SPISettings ccSpi(4000000, MSBFIRST, SPI_MODE0);
static uint32_t lastFoundFreqKHz = 433920;

struct CcSnapshot {
    bool ready = false;
    uint8_t partnum = 0xFF;
    uint8_t version = 0xFF;
    uint8_t marc = 0xFF;
    uint8_t pktStatus = 0xFF;
    uint8_t lqi = 0xFF;
    uint8_t rssiRaw = 0xFF;
    int rssiDbm = -127;
    int gdo0 = 0;
};

struct ScanBand {
    const char* name;
    uint32_t startKHz;
    uint16_t stepKHz;
    uint8_t points;
};

struct MonitorFreq {
    const char* name;
    uint32_t freqKHz;
};

static const ScanBand SCAN_BANDS[] = {
    { "315", 300000, 1000, 31 },
    { "433", 420000, 500, 41 },
    { "868", 860000, 1000, 21 },
    { "915", 902000, 1000, 27 },
};
static constexpr uint8_t SCAN_BAND_COUNT = sizeof(SCAN_BANDS) / sizeof(SCAN_BANDS[0]);

static const MonitorFreq MONITOR_FREQS[] = {
    { "315.00", 315000 },
    { "390.00", 390000 },
    { "433.92", 433920 },
    { "868.35", 868350 },
    { "915.00", 915000 },
};

static void initCcPins() {
    pinMode(TFT_CS_PIN, OUTPUT);
    pinMode(NRF1_CSN_PIN, OUTPUT);
    pinMode(NRF2_CSN_PIN, OUTPUT);
    pinMode(CC1101_CSN_PIN, OUTPUT);
    digitalWrite(TFT_CS_PIN, HIGH);
    digitalWrite(NRF1_CSN_PIN, HIGH);
    digitalWrite(NRF2_CSN_PIN, HIGH);
    digitalWrite(CC1101_CSN_PIN, HIGH);
    pinMode(CC1101_GDO0_PIN, INPUT);
    pinMode(CC1101_TX_DATA_PIN, INPUT);
}

static void ccSelect() {
    digitalWrite(TFT_CS_PIN, HIGH);
    digitalWrite(NRF1_CSN_PIN, HIGH);
    digitalWrite(NRF2_CSN_PIN, HIGH);
    SPI.beginTransaction(ccSpi);
    digitalWrite(CC1101_CSN_PIN, LOW);
    delayMicroseconds(5);
}

static void ccDeselect() {
    digitalWrite(CC1101_CSN_PIN, HIGH);
    SPI.endTransaction();
}

static uint8_t ccRead(uint8_t reg, bool* ready = nullptr) {
    ccSelect();
    uint8_t mode = (reg >= 0x30) ? CC_READ_BURST : CC_READ_SINGLE;
    uint8_t status = SPI.transfer(reg | mode);
    uint8_t value = SPI.transfer(0x00);
    ccDeselect();
    if (ready) *ready = ((status & 0x80) == 0) && status != 0xFF;
    return value;
}

static void ccWrite(uint8_t reg, uint8_t value) {
    ccSelect();
    SPI.transfer(reg);
    SPI.transfer(value);
    ccDeselect();
}

static void ccWriteBurst(uint8_t reg, const uint8_t* data, uint8_t len) {
    ccSelect();
    SPI.transfer(reg | 0x40);
    for (uint8_t i = 0; i < len; i++) SPI.transfer(data[i]);
    ccDeselect();
}

static uint8_t ccStrobe(uint8_t command) {
    ccSelect();
    uint8_t status = SPI.transfer(command);
    ccDeselect();
    return status;
}

static int rssiDbm(uint8_t raw) {
    int rssi = raw;
    if (rssi >= 128) rssi -= 256;
    return (rssi / 2) - 74;
}

static uint32_t freqWordFromKHz(uint32_t freqKHz) {
    uint64_t freqHz = static_cast<uint64_t>(freqKHz) * 1000ULL;
    return static_cast<uint32_t>((freqHz << 16) / CC_XTAL_HZ);
}

static void setFrequency(uint32_t freqKHz) {
    uint32_t word = freqWordFromKHz(freqKHz);
    ccWrite(CC_FREQ2, (word >> 16) & 0xFF);
    ccWrite(CC_FREQ1, (word >> 8) & 0xFF);
    ccWrite(CC_FREQ0, word & 0xFF);
}

static String formatFreq(uint32_t freqKHz) {
    return String(freqKHz / 1000) + "." + String((freqKHz % 1000) / 10) + " MHz";
}

static const char* marcName(uint8_t marcRaw) {
    switch (marcRaw & 0x1F) {
        case 0x00: return "SLEEP";
        case 0x01: return "IDLE";
        case 0x02: return "XOFF";
        case 0x05: return "MANCAL";
        case 0x08: return "STARTCAL";
        case 0x0A: return "FS_LOCK";
        case 0x0D: return "RX";
        case 0x0E: return "RX_END";
        case 0x0F: return "RX_RST";
        case 0x11: return "RX_OVER";
        case 0x12: return "FSTXON";
        case 0x13: return "TX";
        case 0x16: return "TX_UNDER";
        default: return "OTHER";
    }
}

static void resetRadio() {
    digitalWrite(CC1101_CSN_PIN, HIGH);
    delayMicroseconds(30);
    digitalWrite(CC1101_CSN_PIN, LOW);
    delayMicroseconds(30);
    digitalWrite(CC1101_CSN_PIN, HIGH);
    delayMicroseconds(45);
    ccStrobe(CC_SRES);
    delay(3);
}

static void configureOokRxBase() {
    ccWrite(CC_IOCFG0, CC_GDO_CARRIER_SENSE);
    ccWrite(CC_PKTCTRL1, 0x04);
    ccWrite(CC_PKTCTRL0, 0x32);
    ccWrite(CC_FSCTRL1, 0x06);
    ccWrite(CC_FSCTRL0, 0x00);
    ccWrite(CC_MDMCFG4, 0xF5);
    ccWrite(CC_MDMCFG3, 0x83);
    ccWrite(CC_MDMCFG2, 0x30);
    ccWrite(CC_MDMCFG1, 0x22);
    ccWrite(CC_MDMCFG0, 0xF8);
    ccWrite(CC_DEVIATN, 0x00);
    ccWrite(CC_MCSM0, 0x18);
    ccWrite(CC_FOCCFG, 0x16);
    ccWrite(CC_BSCFG, 0x6C);
    ccWrite(CC_AGCCTRL2, 0x43);
    ccWrite(CC_AGCCTRL1, 0x40);
    ccWrite(CC_AGCCTRL0, 0x91);
    ccWrite(CC_FREND1, 0x56);
    ccWrite(CC_FREND0, 0x11);
    ccWrite(CC_FSCAL3, 0xE9);
    ccWrite(CC_FSCAL2, 0x2A);
    ccWrite(CC_FSCAL1, 0x00);
    ccWrite(CC_FSCAL0, 0x1F);
    ccWrite(CC_TEST2, 0x81);
    ccWrite(CC_TEST1, 0x35);
    ccWrite(CC_TEST0, 0x09);
}

static bool prepareRx(uint32_t freqKHz) {
    initCcPins();
    resetRadio();
    configureOokRxBase();
    setFrequency(freqKHz);
    ccStrobe(CC_SIDLE);
    delay(1);
    ccStrobe(CC_SFRX);
    ccStrobe(CC_SCAL);
    delay(3);
    ccStrobe(CC_SRX);
    delay(4);
    bool ready = false;
    uint8_t part = ccRead(CC_PARTNUM, &ready);
    uint8_t ver = ccRead(CC_VERSION);
    return ready && part != 0xFF && ver != 0xFF;
}

static bool prepareAsyncTx(uint32_t freqKHz) {
    initCcPins();
    resetRadio();
    configureOokRxBase();
    setFrequency(freqKHz);
    ccWrite(CC_IOCFG0, CC_GDO_ASYNC_DATA);
    ccWrite(CC_PKTCTRL0, 0x32);
    uint8_t patable[] = { CC_PATABLE_0DBM };
    ccWriteBurst(CC_PATABLE, patable, sizeof(patable));

    pinMode(CC1101_TX_DATA_PIN, OUTPUT);
    digitalWrite(CC1101_TX_DATA_PIN, LOW);

    ccStrobe(CC_SIDLE);
    delay(1);
    ccStrobe(CC_SFTX);
    ccStrobe(CC_SCAL);
    delay(3);
    bool ready = false;
    uint8_t part = ccRead(CC_PARTNUM, &ready);
    uint8_t ver = ccRead(CC_VERSION);
    return ready && part != 0xFF && ver != 0xFF;
}

static void endAsyncTx() {
    digitalWrite(CC1101_TX_DATA_PIN, LOW);
    ccStrobe(CC_SIDLE);
    pinMode(CC1101_TX_DATA_PIN, INPUT);
}

static void retuneRx(uint32_t freqKHz) {
    ccStrobe(CC_SIDLE);
    delayMicroseconds(400);
    setFrequency(freqKHz);
    ccStrobe(CC_SCAL);
    delay(2);
    ccStrobe(CC_SRX);
    delay(4);
}

static CcSnapshot readSnapshot() {
    CcSnapshot snap;
    bool ready = false;
    snap.partnum = ccRead(CC_PARTNUM, &ready);
    snap.version = ccRead(CC_VERSION);
    snap.marc = ccRead(CC_MARCSTATE);
    snap.pktStatus = ccRead(CC_PKTSTATUS);
    snap.lqi = ccRead(CC_LQI);
    snap.rssiRaw = ccRead(CC_RSSI);
    snap.rssiDbm = rssiDbm(snap.rssiRaw);
    snap.gdo0 = digitalRead(CC1101_GDO0_PIN);
    snap.ready = ready && snap.partnum != 0xFF && snap.version != 0xFF;
    return snap;
}

static int readRssiAverage() {
    int total = 0;
    for (uint8_t i = 0; i < 3; i++) {
        total += rssiDbm(ccRead(CC_RSSI));
        delay(2);
    }
    return total / 3;
}

static int readRssiFast() {
    return rssiDbm(ccRead(CC_RSSI));
}

static uint8_t rssiBars(int dbm) {
    if (dbm <= -110) return 0;
    if (dbm >= -45) return 10;
    return static_cast<uint8_t>(((dbm + 110) * 10) / 65);
}

static uint16_t rssiColor(int dbm) {
    if (dbm > -60) return TFT_RED;
    if (dbm > -75) return TFT_YELLOW;
    if (dbm > -90) return TFT_GREEN;
    return TFT_DARKGREY;
}

static void drawFrame(const char* title, const char* footer) {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    tft.drawFastHLine(0, 34, 320, TFT_WHITE);
    tft.drawFastHLine(0, 212, 320, TFT_WHITE);
    drawStringBig(10, 8, title, TFT_WHITE, 1);
    drawStringCustom(10, 220, footer, TFT_WHITE, 1);
}

static void drawHorizontalMeter(int x, int y, int w, int h, int dbm) {
    uint8_t bars = rssiBars(dbm);
    int fillW = (w * bars) / 10;
    tft.drawRect(x, y, w, h, TFT_WHITE);
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
    if (fillW > 2) {
        tft.fillRect(x + 1, y + 1, fillW - 2, h - 2, rssiColor(dbm));
    }
}

static uint16_t finderColor(int deltaDb, uint16_t edges) {
    if (edges >= FINDER_MIN_EDGES + 4 || deltaDb >= FINDER_MIN_DELTA_DB + 18) {
        return TFT_RED;
    }
    if (edges >= FINDER_MIN_EDGES || deltaDb >= FINDER_MIN_DELTA_DB + 8) {
        return TFT_YELLOW;
    }
    if (deltaDb >= FINDER_MIN_DELTA_DB) return TFT_GREEN;
    return TFT_DARKGREY;
}

static void drawDeltaMeter(int x, int y, int w, int h, int deltaDb,
                           uint16_t edges) {
    int scaled = constrain(deltaDb, 0, 30);
    int fillW = (w * scaled) / 30;
    tft.drawRect(x, y, w, h, TFT_WHITE);
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
    if (fillW > 2) {
        tft.fillRect(x + 1, y + 1, fillW - 2, h - 2,
                     finderColor(deltaDb, edges));
    }
}

static void drawNoRadio(const char* title) {
    drawFrame(title, "OK: BACK");
    drawStringCustom(22, 82, "CC1101 NOT READY", TFT_RED, 2);
    drawStringCustom(22, 118, "Check CSN/MISO/MOSI/SCK", TFT_WHITE, 1);
    drawStringCustom(22, 136, "and 3.3V power.", TFT_WHITE, 1);
    while (digitalRead(BTN_OK) == HIGH) delay(20);
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(60);
}

static void drawDiagStatic() {
    drawFrame("CC1101 DIAG", "OK: BACK");
    drawStringCustom(12, 46, "Pins", TFT_CYAN, 1);
    drawStringCustom(18, 62, "CSN : GPIO " + String(CC1101_CSN_PIN), TFT_WHITE, 1);
    drawStringCustom(18, 78, "GDO0: GPIO " + String(CC1101_GDO0_PIN), TFT_WHITE, 1);
    drawStringCustom(18, 94, "SPI : SCK18 MOSI23 MISO19", TFT_WHITE, 1);
    drawStringCustom(12, 118, "Live", TFT_CYAN, 1);
}

static void drawDiagLive(const CcSnapshot& snap, unsigned long edges) {
    tft.fillRect(150, 46, 158, 156, TFT_BLACK);
    drawStringCustom(154, 50, snap.ready ? "SPI READY" : "SPI FAIL",
                     snap.ready ? TFT_GREEN : TFT_RED, 1);
    drawStringCustom(154, 68, "PART: 0x" + String(snap.partnum, HEX), TFT_WHITE, 1);
    drawStringCustom(154, 84, "VER : 0x" + String(snap.version, HEX), TFT_WHITE, 1);
    drawStringCustom(154, 100, "MARC: " + String(marcName(snap.marc)), TFT_WHITE, 1);
    drawStringCustom(154, 116, "RAW : 0x" + String(snap.marc, HEX), TFT_DARKGREY, 1);
    drawStringCustom(154, 132, "RSSI: " + String(snap.rssiDbm) + " dBm",
                     rssiColor(snap.rssiDbm), 1);
    drawStringCustom(154, 148, "GDO0: " + String(snap.gdo0), TFT_YELLOW, 1);
    drawStringCustom(154, 164, "EDGE: " + String(edges), TFT_WHITE, 1);
    drawStringCustom(154, 180, "PKT : 0x" + String(snap.pktStatus, HEX), TFT_DARKGREY, 1);
    drawStringCustom(154, 196, "LQI : 0x" + String(snap.lqi, HEX), TFT_DARKGREY, 1);

    tft.fillRect(18, 136, 110, 66, TFT_BLACK);
    uint8_t bars = rssiBars(snap.rssiDbm);
    for (uint8_t i = 0; i < 10; i++) {
        int x = 20 + i * 10;
        int h = 6 + i * 4;
        uint16_t color = i < bars ? rssiColor(snap.rssiDbm) : TFT_DARKGREY;
        tft.fillRect(x, 198 - h, 7, h, color);
    }
    drawStringCustom(20, 146, "433.92 RX", TFT_WHITE, 1);
}

static void runCcDiag() {
    bool ok = prepareRx(433920);
    if (!ok) {
        drawNoRadio("CC1101 DIAG");
        return;
    }

    drawDiagStatic();
    int lastGdo = digitalRead(CC1101_GDO0_PIN);
    unsigned long edges = 0;
    unsigned long lastDraw = 0;

    while (true) {
        int gdo = digitalRead(CC1101_GDO0_PIN);
        if (gdo != lastGdo) {
            edges++;
            lastGdo = gdo;
        }

        if (millis() - lastDraw >= 250) {
            drawDiagLive(readSnapshot(), edges);
            lastDraw = millis();
        }

        if (digitalRead(BTN_OK) == LOW) {
            while (digitalRead(BTN_OK) == LOW) delay(5);
            delay(60);
            ccStrobe(CC_SIDLE);
            return;
        }
        delay(5);
    }
}

static void drawSpectrumStatic(const ScanBand& band) {
    drawFrame("CC1101 SPEC", "UP/DN:BAND  OK:BACK");
    drawStringCustom(12, 42, "Band: " + String(band.name) + " MHz", TFT_CYAN, 1);
    drawStringCustom(12, 58, formatFreq(band.startKHz) + " - " +
                     formatFreq(band.startKHz + band.stepKHz * (band.points - 1)),
                     TFT_WHITE, 1);
}

static void drawSpectrumBars(const ScanBand& band, const int* values,
                             uint8_t count, uint8_t peakIndex, int peakDbm) {
    tft.fillRect(10, 78, 300, 124, TFT_BLACK);
    int graphX = 18;
    int graphY = 98;
    int graphW = 284;
    int graphH = 86;
    int barW = max(2, graphW / count);

    tft.drawRect(graphX - 2, graphY - 2, graphW + 4, graphH + 4, TFT_DARKGREY);
    for (uint8_t i = 0; i < count; i++) {
        int dbm = values[i];
        int h = map(constrain(dbm, -110, -45), -110, -45, 2, graphH);
        int x = graphX + i * barW;
        uint16_t color = (i == peakIndex) ? TFT_YELLOW : rssiColor(dbm);
        tft.fillRect(x, graphY + graphH - h, max(1, barW - 1), h, color);
    }

    uint32_t peakFreq = band.startKHz + band.stepKHz * peakIndex;
    drawStringCustom(16, 190, "PEAK " + formatFreq(peakFreq), TFT_WHITE, 1);
    drawStringCustom(178, 190, String(peakDbm) + " dBm", rssiColor(peakDbm), 1);
}

static void runCcSpectrum() {
    uint8_t bandIndex = 1;
    int rssiValues[SPECTRUM_MAX_POINTS];
    memset(rssiValues, -120, sizeof(rssiValues));

    bool initialized = false;
    while (true) {
        const ScanBand& band = SCAN_BANDS[bandIndex];
        if (!prepareRx(band.startKHz)) {
            drawNoRadio("CC1101 SPEC");
            return;
        }

        drawSpectrumStatic(band);
        initialized = true;

        while (initialized) {
            uint8_t peakIndex = 0;
            int peakDbm = -127;

            for (uint8_t i = 0; i < band.points && i < SPECTRUM_MAX_POINTS; i++) {
                if (digitalRead(BTN_OK) == LOW) {
                    while (digitalRead(BTN_OK) == LOW) delay(5);
                    delay(60);
                    ccStrobe(CC_SIDLE);
                    return;
                }

                if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
                    bool up = digitalRead(BTN_UP) == LOW;
                    while (digitalRead(BTN_UP) == LOW ||
                           digitalRead(BTN_DOWN) == LOW) delay(5);
                    if (up) {
                        bandIndex = (bandIndex + 1) %
                                    (sizeof(SCAN_BANDS) / sizeof(SCAN_BANDS[0]));
                    } else {
                        uint8_t total = sizeof(SCAN_BANDS) / sizeof(SCAN_BANDS[0]);
                        bandIndex = (bandIndex + total - 1) % total;
                    }
                    initialized = false;
                    break;
                }

                uint32_t freqKHz = band.startKHz + band.stepKHz * i;
                retuneRx(freqKHz);
                rssiValues[i] = readRssiAverage();
                if (rssiValues[i] > peakDbm) {
                    peakDbm = rssiValues[i];
                    peakIndex = i;
                }
            }

            if (initialized) {
                drawSpectrumBars(band, rssiValues, band.points, peakIndex, peakDbm);
            }
        }
    }
}

static void drawMonitorStatic(const MonitorFreq& freq) {
    drawFrame("CC1101 MON", "UP/DN:FREQ  OK:BACK");
    drawStringCustom(12, 42, "Freq: " + formatFreq(freq.freqKHz), TFT_CYAN, 1);
    drawStringCustom(12, 58, "Live RSSI + GDO0 activity", TFT_WHITE, 1);
}

static void drawMonitorLive(const CcSnapshot& snap, unsigned long edges,
                            int peakDbm, unsigned long lastHitMs) {
    tft.fillRect(12, 80, 296, 124, TFT_BLACK);
    drawStringCustom(18, 84, snap.ready ? "READY" : "SPI FAIL",
                     snap.ready ? TFT_GREEN : TFT_RED, 1);
    drawStringCustom(18, 104, "RSSI: " + String(snap.rssiDbm) + " dBm",
                     rssiColor(snap.rssiDbm), 2);
    drawHorizontalMeter(18, 134, 220, 18, snap.rssiDbm);
    drawStringCustom(18, 162, "PEAK: " + String(peakDbm) + " dBm",
                     rssiColor(peakDbm), 1);
    drawStringCustom(170, 84, "GDO0: " + String(snap.gdo0), TFT_YELLOW, 1);
    drawStringCustom(170, 102, "EDGES: " + String(edges), TFT_WHITE, 1);
    drawStringCustom(170, 120, "MARC: " + String(marcName(snap.marc)),
                     TFT_DARKGREY, 1);

    String last = lastHitMs == 0 ? "--" : String((millis() - lastHitMs) / 1000) + "s";
    drawStringCustom(170, 162, "LAST: " + last, TFT_DARKGREY, 1);
    drawStringFit(18, 186, "Use this after finder/spectrum chooses a freq.",
                  TFT_DARKGREY, 286, 1);
}

static void runCcFrequencyMonitor() {
    uint8_t freqIndex = 2;
    const uint8_t totalFreqs = sizeof(MONITOR_FREQS) / sizeof(MONITOR_FREQS[0]);

    while (true) {
        const MonitorFreq& freq = MONITOR_FREQS[freqIndex];
        if (!prepareRx(freq.freqKHz)) {
            drawNoRadio("CC1101 MON");
            return;
        }

        drawMonitorStatic(freq);
        int peakDbm = -127;
        int lastGdo = digitalRead(CC1101_GDO0_PIN);
        unsigned long edges = 0;
        unsigned long lastHitMs = 0;
        unsigned long lastDraw = 0;
        bool retune = false;

        while (!retune) {
            int gdo = digitalRead(CC1101_GDO0_PIN);
            if (gdo != lastGdo) {
                edges++;
                lastGdo = gdo;
                lastHitMs = millis();
            }

            if (digitalRead(BTN_OK) == LOW) {
                while (digitalRead(BTN_OK) == LOW) delay(5);
                delay(60);
                ccStrobe(CC_SIDLE);
                return;
            }

            if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
                bool up = digitalRead(BTN_UP) == LOW;
                while (digitalRead(BTN_UP) == LOW ||
                       digitalRead(BTN_DOWN) == LOW) delay(5);
                freqIndex = up ? (freqIndex + 1) % totalFreqs
                               : (freqIndex + totalFreqs - 1) % totalFreqs;
                retune = true;
                break;
            }

            if (millis() - lastDraw >= 160) {
                CcSnapshot snap = readSnapshot();
                if (snap.rssiDbm > peakDbm) peakDbm = snap.rssiDbm;
                if (snap.rssiDbm > -85) lastHitMs = millis();
                drawMonitorLive(snap, edges, peakDbm, lastHitMs);
                lastDraw = millis();
            }
            delay(3);
        }
    }
}

struct FinderHit {
    bool valid = false;
    uint32_t freqKHz = 0;
    const char* band = "--";
    int dbm = -127;
    int noiseDbm = -127;
    int deltaDb = 0;
    uint16_t edges = 0;
    int score = -127;
    unsigned long seenMs = 0;
};

struct RfCapture {
    uint16_t raw[RF_RAW_MAX];
    uint16_t count = 0;
    uint32_t durationUs = 0;
    uint32_t hash = 0;
    uint8_t startLevel = LOW;
    bool overflow = false;
};

static RfCapture lastRfCapture;
static uint32_t lastRfCaptureFreqKHz = 433920;
static bool hasLastRfCapture = false;

static uint16_t sampleGdoEdgesFast(uint16_t usec) {
    uint16_t edges = 0;
    int last = digitalRead(CC1101_GDO0_PIN);
    uint32_t start = micros();
    while ((uint32_t)(micros() - start) < usec) {
        int level = digitalRead(CC1101_GDO0_PIN);
        if (level != last) {
            edges++;
            last = level;
        }
        delayMicroseconds(120);
    }
    return edges;
}

static FinderHit refineFinderHit(const FinderHit& coarse) {
    FinderHit best = coarse;
    uint32_t startKHz = coarse.freqKHz > FINDER_FINE_SPAN_KHZ
                        ? coarse.freqKHz - FINDER_FINE_SPAN_KHZ
                        : coarse.freqKHz;
    uint32_t stopKHz = coarse.freqKHz + FINDER_FINE_SPAN_KHZ;

    for (uint32_t freqKHz = startKHz; freqKHz <= stopKHz;
         freqKHz += FINDER_FINE_STEP_KHZ) {
        retuneRx(freqKHz);
        int dbm = readRssiAverage();
        uint16_t edges = sampleGdoEdgesFast(1000);
        int deltaDb = max(0, dbm - coarse.noiseDbm);
        int score = (deltaDb * 4) + min<int>(40, edges * 6);

        if (score > best.score) {
            best.freqKHz = freqKHz;
            best.dbm = dbm;
            best.deltaDb = deltaDb;
            best.edges = edges;
            best.score = score;
            best.seenMs = millis();
        }
    }

    return best;
}

static void drawFinderStatic() {
    drawFrame("CC1101 FIND", "UP/DN:CAL  OK:BACK");
    drawStringFit(12, 42, "Press/hold remote while it scans bands.",
                  TFT_CYAN, 296, 1);
}

static void calibrateFinderNoise(int noise[SCAN_BAND_COUNT][SPECTRUM_MAX_POINTS]) {
    drawFrame("CC1101 FIND", "OK: BACK");
    drawStringCustom(22, 78, "CALIBRATING NOISE", TFT_YELLOW, 2);
    drawStringFit(22, 112, "Do not press the remote yet.",
                  TFT_WHITE, 276, 1);

    uint16_t totalPoints = 0;
    for (uint8_t b = 0; b < SCAN_BAND_COUNT; b++) totalPoints += SCAN_BANDS[b].points;

    uint16_t done = 0;
    for (uint8_t b = 0; b < SCAN_BAND_COUNT; b++) {
        const ScanBand& band = SCAN_BANDS[b];
        for (uint8_t i = 0; i < SPECTRUM_MAX_POINTS; i++) noise[b][i] = -120;

        for (uint8_t i = 0; i < band.points && i < SPECTRUM_MAX_POINTS; i++) {
            uint32_t freqKHz = band.startKHz + band.stepKHz * i;
            retuneRx(freqKHz);
            noise[b][i] = readRssiAverage();
            done++;

            int fillW = (done * 252) / totalPoints;
            tft.drawRect(34, 154, 254, 12, TFT_WHITE);
            tft.fillRect(35, 155, fillW, 10, TFT_GREEN);
        }
    }
    delay(200);
}

static void drawFinderLive(const char* currentBand, const FinderHit& last,
                           const FinderHit& best, unsigned long sweeps) {
    tft.fillRect(12, 72, 296, 132, TFT_BLACK);
    drawStringCustom(18, 76, "SCAN: " + String(currentBand) + " MHz",
                     TFT_WHITE, 1);
    drawStringCustom(192, 76, "SWP: " + String(sweeps), TFT_DARKGREY, 1);

    drawStringCustom(18, 102, "LAST PEAK", TFT_CYAN, 1);
    if (last.valid) {
        drawStringCustom(28, 120, formatFreq(last.freqKHz), TFT_WHITE, 2);
        drawStringCustom(28, 148, String(last.dbm) + " +" +
                         String(last.deltaDb) + " E:" + String(last.edges),
                         finderColor(last.deltaDb, last.edges), 1);
        drawDeltaMeter(28, 166, 112, 12, last.deltaDb, last.edges);
    } else {
        drawStringCustom(28, 124, "NO HIT YET", TFT_DARKGREY, 1);
    }

    drawStringCustom(174, 102, "BEST SEEN", TFT_YELLOW, 1);
    if (best.valid) {
        drawStringCustom(184, 120, formatFreq(best.freqKHz), TFT_WHITE, 2);
        drawStringCustom(184, 148, String(best.dbm) + " +" +
                         String(best.deltaDb) + " E:" + String(best.edges),
                         finderColor(best.deltaDb, best.edges), 1);
        drawDeltaMeter(184, 166, 112, 12, best.deltaDb, best.edges);
        String age = String((millis() - best.seenMs) / 1000) + "s ago";
        drawStringCustom(184, 184, age, TFT_DARKGREY, 1);
    } else {
        drawStringCustom(184, 124, "NO HIT YET", TFT_DARKGREY, 1);
    }
}

static void runCcFrequencyFinder() {
    if (!prepareRx(SCAN_BANDS[0].startKHz)) {
        drawNoRadio("CC1101 FIND");
        return;
    }

    drawFinderStatic();
    int noise[SCAN_BAND_COUNT][SPECTRUM_MAX_POINTS];
    calibrateFinderNoise(noise);
    drawFinderStatic();
    FinderHit best;
    FinderHit last;
    unsigned long sweeps = 0;

    while (true) {
        last = FinderHit();

        for (uint8_t b = 0; b < SCAN_BAND_COUNT; b++) {
            const ScanBand& band = SCAN_BANDS[b];
            drawFinderLive(band.name, last, best, sweeps);

            for (uint8_t i = 0; i < band.points && i < SPECTRUM_MAX_POINTS; i++) {
                if (digitalRead(BTN_OK) == LOW) {
                    while (digitalRead(BTN_OK) == LOW) delay(5);
                    delay(60);
                    ccStrobe(CC_SIDLE);
                    return;
                }

                if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
                    while (digitalRead(BTN_UP) == LOW ||
                           digitalRead(BTN_DOWN) == LOW) delay(5);
                    best = FinderHit();
                    last = FinderHit();
                    calibrateFinderNoise(noise);
                    drawFinderStatic();
                }

                uint32_t freqKHz = band.startKHz + band.stepKHz * i;
                retuneRx(freqKHz);
                int dbm = readRssiFast();
                uint16_t edges = sampleGdoEdgesFast(1400);
                int deltaDb = max(0, dbm - noise[b][i]);
                bool hit = deltaDb >= FINDER_MIN_DELTA_DB ||
                           edges >= FINDER_MIN_EDGES;
                if (!hit) continue;

                int score = (deltaDb * 4) + min<int>(40, edges * 6);
                FinderHit candidate;
                candidate.valid = true;
                candidate.freqKHz = freqKHz;
                candidate.band = band.name;
                candidate.dbm = dbm;
                candidate.noiseDbm = noise[b][i];
                candidate.deltaDb = deltaDb;
                candidate.edges = edges;
                candidate.score = score;
                candidate.seenMs = millis();

                if (!last.valid || score > last.score) {
                    last = refineFinderHit(candidate);
                }

                if (!best.valid || last.score > best.score) {
                    best = last;
                    best.seenMs = millis();
                    lastFoundFreqKHz = best.freqKHz;
                }
            }
        }

        sweeps++;
        drawFinderLive("ALL", last, best, sweeps);
    }
}

static uint32_t hashRfCapture(const RfCapture& cap) {
    uint32_t h = 2166136261UL;
    h ^= cap.count;
    h *= 16777619UL;
    h ^= cap.startLevel;
    h *= 16777619UL;

    for (uint16_t i = 0; i < cap.count; i++) {
        uint16_t bucket = min<uint16_t>(31, (cap.raw[i] + 150) / 300);
        h ^= bucket;
        h *= 16777619UL;
        h ^= (i & 0x0F);
        h *= 16777619UL;
    }

    return h;
}

static bool isRfCaptureValid(const RfCapture& cap) {
    return cap.count >= RF_MIN_RAW_PULSES &&
           cap.durationUs >= RF_MIN_CAPTURE_US &&
           !cap.overflow;
}

static bool captureRfRaw(RfCapture& cap, bool* canceled) {
    cap = RfCapture();
    if (canceled) *canceled = false;

    int level = digitalRead(CC1101_GDO0_PIN);
    uint32_t waitStartMs = millis();
    uint32_t lastChange = micros();
    uint32_t captureStart = 0;
    uint32_t lastEdge = 0;
    bool started = false;

    while (millis() - waitStartMs < RF_CAPTURE_TIMEOUT_MS) {
        if (digitalRead(BTN_OK) == LOW) {
            while (digitalRead(BTN_OK) == LOW) delay(5);
            delay(60);
            if (canceled) *canceled = true;
            return false;
        }

        int nowLevel = digitalRead(CC1101_GDO0_PIN);
        uint32_t now = micros();
        if (nowLevel != level) {
            uint32_t duration = now - lastChange;
            if (!started) {
                started = true;
                captureStart = now;
                cap.startLevel = nowLevel;
            } else if (duration >= RF_MIN_PULSE_US) {
                if (cap.count < RF_RAW_MAX) {
                    cap.raw[cap.count++] = static_cast<uint16_t>(
                        min<uint32_t>(duration, 65535));
                } else {
                    cap.overflow = true;
                }
            }
            level = nowLevel;
            lastChange = now;
            lastEdge = now;
        }

        if (started) {
            cap.durationUs = now - captureStart;
            if ((uint32_t)(now - lastEdge) > RF_CAPTURE_GAP_US ||
                cap.durationUs > RF_CAPTURE_MAX_US ||
                cap.overflow) {
                uint32_t tail = now - lastChange;
                if (tail >= RF_MIN_PULSE_US && cap.count < RF_RAW_MAX) {
                    cap.raw[cap.count++] = static_cast<uint16_t>(
                        min<uint32_t>(tail, 65535));
                }
                cap.hash = hashRfCapture(cap);
                if (isRfCaptureValid(cap)) return true;

                cap = RfCapture();
                level = digitalRead(CC1101_GDO0_PIN);
                lastChange = micros();
                captureStart = 0;
                lastEdge = 0;
                started = false;
            }
        }

        delayMicroseconds(20);
    }

    cap.hash = hashRfCapture(cap);
    return isRfCaptureValid(cap);
}

static bool armForRfCapture(int* noiseDbm, int* triggerDbm,
                            uint16_t* triggerEdges, bool* canceled) {
    if (canceled) *canceled = false;

    ccWrite(CC_IOCFG0, CC_GDO_CARRIER_SENSE);
    ccStrobe(CC_SRX);
    delay(250);

    tft.fillRect(12, 92, 296, 110, TFT_BLACK);
    drawStringCustom(22, 104, "CALIBRATING NOISE", TFT_YELLOW, 1);
    drawStringFit(22, 126, "Do not press remote yet.",
                  TFT_WHITE, 276, 1);

    int total = 0;
    int peak = -127;
    for (uint8_t i = 0; i < RF_ARM_SAMPLES; i++) {
        if (digitalRead(BTN_OK) == LOW) {
            while (digitalRead(BTN_OK) == LOW) delay(5);
            delay(60);
            if (canceled) *canceled = true;
            return false;
        }
        int dbm = readRssiAverage();
        total += dbm;
        if (dbm > peak) peak = dbm;
        int fillW = ((i + 1) * 250) / RF_ARM_SAMPLES;
        tft.drawRect(34, 166, 252, 10, TFT_WHITE);
        tft.fillRect(35, 167, fillW, 8, TFT_GREEN);
    }

    int avg = total / RF_ARM_SAMPLES;
    int threshold = max(peak + RF_ARM_DELTA_DB, avg + RF_ARM_DELTA_DB + 5);
    if (noiseDbm) *noiseDbm = peak;

    tft.fillRect(12, 92, 296, 110, TFT_BLACK);
    drawStringCustom(22, 104, "ARMED", TFT_GREEN, 2);
    drawStringCustom(22, 136, "Noise: " + String(peak) +
                     " dBm  Thr: " + String(threshold), TFT_WHITE, 1);
    drawStringFit(22, 158, "Noise calibrated. Raw capture next.",
                  TFT_CYAN, 276, 1);
    if (triggerDbm) *triggerDbm = threshold;
    if (triggerEdges) *triggerEdges = 0;
    delay(650);
    return true;
}

static void drawCodeCheckIntro(uint32_t freqKHz) {
    drawFrame("RF CODE CHECK", "OK: CANCEL");
    drawStringCustom(12, 42, "Freq: " + formatFreq(freqKHz), TFT_CYAN, 1);
    drawStringFit(12, 60, "RX only. No copy/replay transmit.",
                  TFT_YELLOW, 296, 1);
    drawStringFit(12, 78, "Press the same remote button 3 times.",
                  TFT_WHITE, 296, 1);
}

static void drawCaptureProgress(uint8_t index) {
    tft.fillRect(12, 108, 296, 82, TFT_BLACK);
    drawStringCustom(24, 118, "CAPTURE " + String(index + 1) + "/3",
                     TFT_WHITE, 2);
    drawStringCustom(24, 150, "Press remote button now.", TFT_CYAN, 1);
}

static void drawCodeCheckResult(const RfCapture caps[3], uint8_t okCount) {
    tft.fillRect(12, 98, 296, 108, TFT_BLACK);

    uint8_t unique = 0;
    for (uint8_t i = 0; i < okCount; i++) {
        bool seen = false;
        for (uint8_t j = 0; j < i; j++) {
            if (caps[i].hash == caps[j].hash) seen = true;
        }
        if (!seen) unique++;
    }

    String verdict = "NEED MORE";
    uint16_t color = TFT_YELLOW;
    if (okCount >= 2 && unique >= 2) {
        verdict = "CHANGING CODE";
        color = TFT_GREEN;
    } else if (okCount >= 2) {
        verdict = "REPEATS";
        color = TFT_YELLOW;
    }

    drawStringCustom(18, 102, verdict, color, 2);
    for (uint8_t i = 0; i < 3; i++) {
        int y = 132 + i * 18;
        if (i < okCount) {
            drawStringCustom(18, y, "C" + String(i + 1) + " P:" +
                             String(caps[i].count) + " H:" +
                             String(caps[i].hash, HEX), TFT_WHITE, 1);
        } else {
            drawStringCustom(18, y, "C" + String(i + 1) + " --",
                             TFT_DARKGREY, 1);
        }
    }

    if (verdict == "CHANGING CODE") {
        drawStringFit(18, 188, "Likely rolling/changing code. Good sign.",
                      TFT_DARKGREY, 286, 1);
    } else if (verdict == "REPEATS") {
        drawStringFit(18, 188, "May be fixed code or capture alignment.",
                      TFT_DARKGREY, 286, 1);
    } else {
        drawStringFit(18, 188, "Try again closer to antenna/module.",
                      TFT_DARKGREY, 286, 1);
    }
}

static void runCcCodeCheck() {
    uint32_t freqKHz = lastFoundFreqKHz;
    if (!prepareRx(freqKHz)) {
        drawNoRadio("RF CODE CHECK");
        return;
    }
    ccWrite(CC_IOCFG0, CC_GDO_ASYNC_DATA);
    ccStrobe(CC_SRX);
    delay(4);

    while (true) {
        drawCodeCheckIntro(freqKHz);
        RfCapture caps[3];
        uint8_t okCount = 0;
        bool canceled = false;

        for (uint8_t i = 0; i < 3; i++) {
            RfCapture cap;
            drawCaptureProgress(i);
            if (captureRfRaw(cap, &canceled)) {
                caps[okCount++] = cap;
                drawStringCustom(24, 176, "OK pulses: " +
                                 String(cap.count), TFT_GREEN, 1);
                delay(650);
            } else if (canceled) {
                ccStrobe(CC_SIDLE);
                return;
            } else {
                drawStringCustom(24, 176, "NO FRAME", TFT_RED, 1);
                delay(900);
            }
        }

        drawCodeCheckResult(caps, okCount);
        drawStringCustom(10, 220, "UP/DN: AGAIN  OK: BACK", TFT_WHITE, 1);

        while (true) {
            if (digitalRead(BTN_OK) == LOW) {
                while (digitalRead(BTN_OK) == LOW) delay(5);
                delay(60);
                ccStrobe(CC_SIDLE);
                return;
            }
            if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
                while (digitalRead(BTN_UP) == LOW ||
                       digitalRead(BTN_DOWN) == LOW) delay(5);
                break;
            }
            delay(20);
        }

        ccWrite(CC_IOCFG0, CC_GDO_ASYNC_DATA);
        ccStrobe(CC_SRX);
        delay(4);
    }
}

struct RfAnalysis {
    uint16_t minPulse = 0;
    uint16_t maxPulse = 0;
    uint16_t shortAvg = 0;
    uint16_t longAvg = 0;
    uint16_t shortCount = 0;
    uint16_t longCount = 0;
    uint8_t gapCount = 0;
    uint8_t repeatGuess = 1;
    bool ookLikely = false;
};

static bool hasPreviousAnalyzerCapture = false;
static uint32_t previousAnalyzerHash = 0;
static uint16_t previousAnalyzerCount = 0;
static uint32_t previousAnalyzerDurationUs = 0;

static void showRfMessage(const char* frameTitle, const String& title,
                          const String& line, uint16_t color) {
    drawFrame(frameTitle, "OK: BACK");
    drawStringFit(22, 82, title, color, 276, 2);
    if (line.length()) drawStringFit(22, 124, line, TFT_WHITE, 276, 1);
    while (digitalRead(BTN_OK) == HIGH) delay(20);
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(60);
}

static RfAnalysis analyzeRfCapture(const RfCapture& cap) {
    RfAnalysis analysis;
    if (cap.count == 0) return analysis;

    uint16_t minPulse = 65535;
    uint16_t maxPulse = 0;
    uint16_t usableCount = 0;

    for (uint16_t i = 0; i < cap.count; i++) {
        uint16_t pulse = cap.raw[i];
        if (pulse >= 3500) analysis.gapCount++;
        if (pulse < RF_MIN_PULSE_US || pulse > 6000) continue;

        if (pulse < minPulse) minPulse = pulse;
        if (pulse > maxPulse) maxPulse = pulse;
        usableCount++;
    }

    if (usableCount == 0) return analysis;

    analysis.minPulse = minPulse;
    analysis.maxPulse = maxPulse;
    uint16_t threshold = minPulse + ((maxPulse - minPulse) / 2);

    uint32_t shortSum = 0;
    uint32_t longSum = 0;
    for (uint16_t i = 0; i < cap.count; i++) {
        uint16_t pulse = cap.raw[i];
        if (pulse < RF_MIN_PULSE_US || pulse > 6000) continue;

        if (pulse <= threshold) {
            shortSum += pulse;
            analysis.shortCount++;
        } else {
            longSum += pulse;
            analysis.longCount++;
        }
    }

    if (analysis.shortCount > 0) {
        analysis.shortAvg = shortSum / analysis.shortCount;
    }
    if (analysis.longCount > 0) {
        analysis.longAvg = longSum / analysis.longCount;
    }

    analysis.repeatGuess = analysis.gapCount >= 2
                           ? min<uint8_t>(9, analysis.gapCount + 1)
                           : 1;
    analysis.ookLikely = cap.count >= RF_MIN_RAW_PULSES &&
                         analysis.shortCount > 0 &&
                         analysis.longCount > 0;
    return analysis;
}

static bool captureMatchesPreviousAnalyzer(const RfCapture& cap) {
    if (!hasPreviousAnalyzerCapture) return false;
    uint16_t countDelta = cap.count > previousAnalyzerCount
                          ? cap.count - previousAnalyzerCount
                          : previousAnalyzerCount - cap.count;
    uint32_t durationDelta = cap.durationUs > previousAnalyzerDurationUs
                             ? cap.durationUs - previousAnalyzerDurationUs
                             : previousAnalyzerDurationUs - cap.durationUs;
    uint32_t durationTolerance = max<uint32_t>(2000,
                                               previousAnalyzerDurationUs / 10);
    return cap.hash == previousAnalyzerHash ||
           (countDelta <= 2 && durationDelta <= durationTolerance);
}

static void rememberAnalyzerCapture(const RfCapture& cap) {
    previousAnalyzerHash = cap.hash;
    previousAnalyzerCount = cap.count;
    previousAnalyzerDurationUs = cap.durationUs;
    hasPreviousAnalyzerCapture = true;
}

static bool captureRfFrameForTool(const char* title, const String& prompt,
                                  RfCapture& cap, uint32_t* outFreqKHz) {
    uint32_t freqKHz = lastFoundFreqKHz;
    if (outFreqKHz) *outFreqKHz = freqKHz;

    if (!prepareRx(freqKHz)) {
        drawNoRadio(title);
        return false;
    }

    drawFrame(title, "OK: CANCEL");
    drawStringCustom(12, 42, "Freq: " + formatFreq(freqKHz), TFT_CYAN, 1);
    drawStringFit(12, 66, "Noise is calibrated before raw capture.",
                  TFT_WHITE, 296, 1);

    int noiseDbm = -127;
    int triggerDbm = -127;
    uint16_t triggerEdges = 0;
    bool canceled = false;
    if (!armForRfCapture(&noiseDbm, &triggerDbm, &triggerEdges, &canceled)) {
        ccStrobe(CC_SIDLE);
        if (canceled) return false;
        showRfMessage(title, "NO RF ACTIVITY",
                      "Press/hold the remote after RAW READY.", TFT_RED);
        return false;
    }

    ccWrite(CC_IOCFG0, CC_GDO_ASYNC_DATA);
    ccStrobe(CC_SRX);
    delay(250);

    tft.fillRect(12, 92, 296, 110, TFT_BLACK);
    drawStringCustom(22, 108, "RAW READY", TFT_GREEN, 2);
    drawStringCustom(22, 142, "Threshold: " + String(triggerDbm) +
                     " dBm", TFT_WHITE, 1);
    drawStringFit(22, 164, prompt, TFT_CYAN, 276, 1);

    bool ok = captureRfRaw(cap, &canceled);
    ccStrobe(CC_SIDLE);

    if (canceled) return false;
    if (!ok) {
        showRfMessage(title, "NO RF FRAME",
                      "Try closer or confirm the frequency.", TFT_RED);
        return false;
    }

    return true;
}

static void drawRfAnalysisResult(const RfCapture& cap, uint32_t freqKHz,
                                 bool sameAsPrevious) {
    RfAnalysis analysis = analyzeRfCapture(cap);

    drawFrame("RF ANALYZER", "UP/DN:AGAIN  OK:BACK");
    drawStringCustom(12, 42, "Freq : " + formatFreq(freqKHz), TFT_CYAN, 1);
    drawStringCustom(12, 60, "Pulses: " + String(cap.count), TFT_WHITE, 1);
    drawStringCustom(162, 60, "Total: " +
                     String(cap.durationUs / 1000.0f, 1) + " ms",
                     TFT_WHITE, 1);

    drawStringCustom(12, 84, "Pulse stats", TFT_CYAN, 1);
    drawStringCustom(22, 102, "Short avg: " +
                     String(analysis.shortAvg) + " us  n:" +
                     String(analysis.shortCount), TFT_WHITE, 1);
    drawStringCustom(22, 120, "Long  avg: " +
                     String(analysis.longAvg) + " us  n:" +
                     String(analysis.longCount), TFT_WHITE, 1);
    drawStringCustom(22, 138, "Min/Max  : " +
                     String(analysis.minPulse) + "/" +
                     String(analysis.maxPulse) + " us", TFT_DARKGREY, 1);

    String modText = analysis.ookLikely ? "OOK/ASK: LIKELY"
                                        : "OOK/ASK: MAYBE";
    drawStringCustom(12, 164, modText,
                     analysis.ookLikely ? TFT_GREEN : TFT_YELLOW, 1);

    String repeatText = "Repeat: single/unknown";
    uint16_t repeatColor = TFT_DARKGREY;
    if (sameAsPrevious) {
        repeatText = "Repeat: same as previous";
        repeatColor = TFT_GREEN;
    } else if (analysis.repeatGuess > 1) {
        repeatText = "Repeat: internal x" + String(analysis.repeatGuess);
        repeatColor = TFT_YELLOW;
    }
    drawStringCustom(12, 182, repeatText, repeatColor, 1);
    drawStringCustom(162, 182, "Gaps: " + String(analysis.gapCount),
                     TFT_DARKGREY, 1);

    drawStringFit(12, 198, "Hash: 0x" + String(cap.hash, HEX),
                  TFT_WHITE, 296, 1);
}

static void runRfSignalAnalyzer() {
    bool repeat = true;
    while (repeat) {
        RfCapture cap;
        uint32_t freqKHz = 0;
        if (!captureRfFrameForTool("RF ANALYZER",
                                   "Press/hold remote now.",
                                   cap, &freqKHz)) {
            return;
        }

        bool sameAsPrevious = captureMatchesPreviousAnalyzer(cap);
        lastRfCapture = cap;
        lastRfCaptureFreqKHz = freqKHz;
        hasLastRfCapture = true;
        drawRfAnalysisResult(cap, freqKHz, sameAsPrevious);
        rememberAnalyzerCapture(cap);

        repeat = false;
        while (true) {
            if (digitalRead(BTN_OK) == LOW) {
                while (digitalRead(BTN_OK) == LOW) delay(5);
                delay(60);
                return;
            }
            if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
                while (digitalRead(BTN_UP) == LOW ||
                       digitalRead(BTN_DOWN) == LOW) delay(5);
                delay(60);
                repeat = true;
                break;
            }
            delay(20);
        }
    }
}

static void drawRfRawViewerPage(const RfCapture& cap, uint32_t freqKHz,
                                uint8_t page, uint8_t pages) {
    drawFrame("RF RAW VIEW", "UP/DN:PAGE  OK:CAP  HOLD:BACK");
    drawStringCustom(12, 42, "Freq: " + formatFreq(freqKHz), TFT_CYAN, 1);
    drawStringCustom(12, 58, "P:" + String(cap.count) + "  H:0x" +
                     String(cap.hash, HEX), TFT_WHITE, 1);
    drawStringCustom(236, 58, "PG " + String(page + 1) + "/" +
                     String(pages), TFT_DARKGREY, 1);

    const int graphX = 12;
    const int graphY = 82;
    const int graphW = 296;
    const int graphH = 98;
    tft.drawRect(graphX - 1, graphY - 1, graphW + 2, graphH + 2,
                 TFT_DARKGREY);

    uint16_t offset = page * RF_RAW_VIEW_PULSES_PER_PAGE;
    uint16_t end = min<uint16_t>(cap.count,
                                 offset + RF_RAW_VIEW_PULSES_PER_PAGE);
    uint16_t pageMax = RF_MIN_PULSE_US + 1;
    for (uint16_t i = offset; i < end; i++) {
        uint16_t pulse = min<uint16_t>(cap.raw[i], 6000);
        if (pulse > pageMax) pageMax = pulse;
    }

    uint8_t level = cap.startLevel;
    for (uint16_t i = 0; i < offset; i++) level = !level;

    int barW = max(2, graphW / RF_RAW_VIEW_PULSES_PER_PAGE);
    for (uint16_t i = offset; i < end; i++) {
        uint16_t pulse = min<uint16_t>(cap.raw[i], 6000);
        int h = map(pulse, RF_MIN_PULSE_US, pageMax, 3, graphH - 4);
        int x = graphX + (i - offset) * barW;
        int y = graphY + graphH - h;
        uint16_t color = level ? TFT_GREEN : TFT_CYAN;
        tft.fillRect(x, y, max(1, barW - 1), h, color);
        level = !level;
    }

    tft.fillRect(12, 188, 296, 20, TFT_BLACK);
    String samples = "";
    for (uint16_t i = offset; i < min<uint16_t>(end, offset + 6); i++) {
        if (samples.length()) samples += ",";
        samples += String(cap.raw[i]);
    }
    drawStringFit(12, 190, "us: " + samples, TFT_DARKGREY, 296, 1);
}

static bool captureRawViewFrame() {
    RfCapture cap;
    uint32_t freqKHz = 0;
    bool ok = captureRfFrameForTool("RF RAW VIEW",
                                    "Press/hold remote now.",
                                    cap, &freqKHz);
    if (!ok) return false;

    lastRfCapture = cap;
    lastRfCaptureFreqKHz = freqKHz;
    hasLastRfCapture = true;
    return true;
}

static void runRfRawViewer() {
    if (!hasLastRfCapture) {
        drawFrame("RF RAW VIEW", "OK:CAPTURE  HOLD:BACK");
        drawStringFit(22, 82, "NO CAPTURE", TFT_YELLOW, 276, 2);
        drawStringFit(22, 124, "Tap OK to capture a raw RF frame.",
                      TFT_WHITE, 276, 1);

        while (true) {
            if (digitalRead(BTN_OK) == LOW) {
                bool held = waitOkReleaseWasLong();
                delay(60);
                if (held) return;
                if (captureRawViewFrame()) break;

                drawFrame("RF RAW VIEW", "OK:CAPTURE  HOLD:BACK");
                drawStringFit(22, 82, "NO CAPTURE", TFT_YELLOW, 276, 2);
                drawStringFit(22, 124, "Tap OK to try capture again.",
                              TFT_WHITE, 276, 1);
            }
            delay(20);
        }
    }

    uint8_t page = 0;
    bool redraw = true;

    while (true) {
        uint8_t pages = (lastRfCapture.count + RF_RAW_VIEW_PULSES_PER_PAGE - 1) /
                        RF_RAW_VIEW_PULSES_PER_PAGE;
        if (pages == 0) pages = 1;
        if (page >= pages) page = pages - 1;
        if (redraw) {
            drawRfRawViewerPage(lastRfCapture, lastRfCaptureFreqKHz, page, pages);
            redraw = false;
        }

        if (digitalRead(BTN_OK) == LOW) {
            bool held = waitOkReleaseWasLong();
            delay(60);
            if (held) return;
            if (captureRawViewFrame()) page = 0;
            redraw = true;
            continue;
        }
        if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
            bool up = digitalRead(BTN_UP) == LOW;
            while (digitalRead(BTN_UP) == LOW ||
                   digitalRead(BTN_DOWN) == LOW) delay(5);
            page = up ? (page + 1) % pages : (page + pages - 1) % pages;
            redraw = true;
            delay(60);
        }
        delay(20);
    }
}

static uint8_t nearestMonitorIndex(uint32_t freqKHz) {
    uint8_t best = 2;
    uint32_t bestDiff = 0xFFFFFFFFUL;
    const uint8_t totalFreqs = sizeof(MONITOR_FREQS) / sizeof(MONITOR_FREQS[0]);
    for (uint8_t i = 0; i < totalFreqs; i++) {
        uint32_t current = MONITOR_FREQS[i].freqKHz;
        uint32_t diff = current > freqKHz ? current - freqKHz
                                          : freqKHz - current;
        if (diff < bestDiff) {
            bestDiff = diff;
            best = i;
        }
    }
    return best;
}

static int calibrateLiveNoise(const MonitorFreq& freq) {
    drawFrame("RF LIVE", "OK: BACK");
    drawStringCustom(12, 42, "Freq: " + formatFreq(freq.freqKHz),
                     TFT_CYAN, 1);
    drawStringCustom(24, 86, "CALIBRATING", TFT_YELLOW, 2);
    drawStringFit(24, 118, "Leave the remote quiet for a moment.",
                  TFT_WHITE, 272, 1);

    int peak = -127;
    for (uint8_t i = 0; i < 16; i++) {
        if (digitalRead(BTN_OK) == LOW) return -127;
        int dbm = readRssiAverage();
        if (dbm > peak) peak = dbm;
        int fillW = ((i + 1) * 250) / 16;
        tft.drawRect(34, 154, 252, 10, TFT_WHITE);
        tft.fillRect(35, 155, fillW, 8, TFT_GREEN);
    }
    delay(150);
    return peak;
}

static void drawRfLiveStatic(const MonitorFreq& freq, int noiseDbm) {
    drawFrame("RF LIVE", "UP/DN:FREQ  OK:BACK");
    drawStringCustom(12, 42, formatFreq(freq.freqKHz), TFT_CYAN, 2);
    drawStringCustom(214, 46, "NOISE " + String(noiseDbm),
                     TFT_DARKGREY, 1);
}

static void drawRfLiveScreen(const CcSnapshot& snap, int noiseDbm,
                             int peakDbm, unsigned long edges,
                             unsigned long presses,
                             bool active, unsigned long lastActivityMs) {
    tft.fillRect(12, 72, 296, 132, TFT_BLACK);
    drawStringCustom(18, 78, active ? "SIGNAL DETECTED" : "IDLE",
                     active ? TFT_GREEN : TFT_DARKGREY, 2);

    drawStringCustom(18, 112, "RSSI " + String(snap.rssiDbm) + " dBm",
                     rssiColor(snap.rssiDbm), 1);
    drawHorizontalMeter(18, 132, 210, 18, snap.rssiDbm);
    int delta = snap.rssiDbm - noiseDbm;
    drawStringCustom(238, 132, "+" + String(max(0, delta)),
                     delta >= RF_LIVE_DELTA_DB ? TFT_GREEN : TFT_DARKGREY, 1);

    drawStringCustom(18, 160, "PEAK: " + String(peakDbm) + " dBm",
                     rssiColor(peakDbm), 1);
    drawStringCustom(174, 160, "EDGES: " + String(edges),
                     TFT_WHITE, 1);
    drawStringCustom(18, 180, "PRESSES: " + String(presses),
                     TFT_YELLOW, 1);

    String last = lastActivityMs == 0
                  ? "--"
                  : String((millis() - lastActivityMs) / 1000) + "s";
    drawStringCustom(174, 180, "LAST: " + last, TFT_DARKGREY, 1);
}

static void runRfLiveDetector() {
    uint8_t freqIndex = nearestMonitorIndex(lastFoundFreqKHz);
    const uint8_t totalFreqs = sizeof(MONITOR_FREQS) / sizeof(MONITOR_FREQS[0]);

    while (true) {
        const MonitorFreq& freq = MONITOR_FREQS[freqIndex];
        if (!prepareRx(freq.freqKHz)) {
            drawNoRadio("RF LIVE");
            return;
        }

        int noiseDbm = calibrateLiveNoise(freq);
        if (digitalRead(BTN_OK) == LOW) {
            while (digitalRead(BTN_OK) == LOW) delay(5);
            delay(60);
            ccStrobe(CC_SIDLE);
            return;
        }

        drawRfLiveStatic(freq, noiseDbm);
        int peakDbm = -127;
        int lastGdo = digitalRead(CC1101_GDO0_PIN);
        unsigned long edges = 0;
        unsigned long windowEdges = 0;
        unsigned long presses = 0;
        unsigned long lastPressMs = 0;
        unsigned long lastActivityMs = 0;
        unsigned long lastDraw = 0;
        bool retune = false;

        while (!retune) {
            int gdo = digitalRead(CC1101_GDO0_PIN);
            if (gdo != lastGdo) {
                edges++;
                windowEdges++;
                lastGdo = gdo;
            }

            if (digitalRead(BTN_OK) == LOW) {
                while (digitalRead(BTN_OK) == LOW) delay(5);
                delay(60);
                ccStrobe(CC_SIDLE);
                return;
            }

            if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
                bool up = digitalRead(BTN_UP) == LOW;
                while (digitalRead(BTN_UP) == LOW ||
                       digitalRead(BTN_DOWN) == LOW) delay(5);
                freqIndex = up ? (freqIndex + 1) % totalFreqs
                               : (freqIndex + totalFreqs - 1) % totalFreqs;
                retune = true;
                break;
            }

            if (millis() - lastDraw >= 120) {
                CcSnapshot snap = readSnapshot();
                if (snap.rssiDbm > peakDbm) peakDbm = snap.rssiDbm;

                bool strongRssi = snap.rssiDbm >= noiseDbm + RF_LIVE_DELTA_DB;
                bool active = strongRssi ||
                              windowEdges >= RF_LIVE_MIN_EDGES ||
                              snap.gdo0 == HIGH;
                if (active) {
                    lastActivityMs = millis();
                    if (millis() - lastPressMs > 450) {
                        presses++;
                        lastPressMs = millis();
                    }
                }

                drawRfLiveScreen(snap, noiseDbm, peakDbm, edges, presses,
                                 active, lastActivityMs);
                windowEdges = 0;
                lastDraw = millis();
            }
            delay(3);
        }
    }
}

static void sendRfRaw(const RfCapture& cap, bool inverted) {
    for (uint8_t repeat = 0; repeat < RF_REPLAY_REPEATS; repeat++) {
        uint8_t level = cap.startLevel ? HIGH : LOW;
        if (inverted) level = !level;
        digitalWrite(CC1101_TX_DATA_PIN, level);

        for (uint16_t i = 0; i < cap.count; i++) {
            delayMicroseconds(cap.raw[i]);
            level = !level;
            digitalWrite(CC1101_TX_DATA_PIN, level);
        }

        digitalWrite(CC1101_TX_DATA_PIN, LOW);
        delayMicroseconds(RF_REPLAY_GAP_US);
    }
}

static void drawLabReplayFrame() {
    drawFrame("RF LAB", "OK: SELECT");
    drawStringFit(12, 42, "For your own non-security 433 devices.",
                  TFT_CYAN, 296, 1);
    drawStringFit(12, 58, "TX needs CC1101 GDO0 jumper to GPIO15.",
                  TFT_YELLOW, 296, 1);
}

static void showLabMessage(const String& title, const String& line,
                           uint16_t color) {
    drawFrame("RF LAB", "OK: BACK");
    drawStringFit(22, 86, title, color, 276, 2);
    if (line.length()) drawStringFit(22, 126, line, TFT_WHITE, 276, 1);
    while (digitalRead(BTN_OK) == HIGH) delay(20);
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(60);
}

static bool runLabCapture() {
    uint32_t freqKHz = lastFoundFreqKHz;
    if (!prepareRx(freqKHz)) {
        drawNoRadio("RF LAB");
        return false;
    }

    drawFrame("RF CAPTURE", "OK: CANCEL");
    drawStringCustom(12, 42, "Freq: " + formatFreq(freqKHz), TFT_CYAN, 1);
    drawStringFit(12, 68, "It will arm before raw capture.",
                  TFT_WHITE, 296, 1);

    int noiseDbm = -127;
    int triggerDbm = -127;
    uint16_t triggerEdges = 0;
    bool canceled = false;
    if (!armForRfCapture(&noiseDbm, &triggerDbm, &triggerEdges, &canceled)) {
        ccStrobe(CC_SIDLE);
        if (canceled) return false;
        showLabMessage("NO RF ACTIVITY", "Press/hold remote after ARMED.",
                       TFT_RED);
        return false;
    }

    ccWrite(CC_IOCFG0, CC_GDO_ASYNC_DATA);
    ccStrobe(CC_SRX);
    delay(250);

    tft.fillRect(12, 92, 296, 110, TFT_BLACK);
    drawStringCustom(22, 108, "RAW READY", TFT_GREEN, 2);
    drawStringCustom(22, 142, "Threshold: " + String(triggerDbm) +
                     " dBm", TFT_WHITE, 1);
    drawStringFit(22, 164, "Press/hold the light remote now.",
                  TFT_CYAN, 276, 1);

    RfCapture cap;
    bool ok = captureRfRaw(cap, &canceled);
    ccStrobe(CC_SIDLE);

    if (canceled) return false;
    if (!ok) {
        showLabMessage("NO RF FRAME", "Try closer or use Frequency Mon.",
                       TFT_RED);
        return false;
    }

    lastRfCapture = cap;
    lastRfCaptureFreqKHz = freqKHz;
    hasLastRfCapture = true;

    drawFrame("RF CAPTURE", "OK: BACK");
    drawStringCustom(18, 76, "CAPTURE OK", TFT_GREEN, 2);
    drawStringCustom(18, 112, "Freq : " + formatFreq(freqKHz), TFT_WHITE, 1);
    drawStringCustom(18, 130, "Pulses: " + String(cap.count), TFT_WHITE, 1);
    drawStringCustom(18, 148, "Hash : " + String(cap.hash, HEX),
                     TFT_DARKGREY, 1);
    drawStringFit(18, 176, "Now use Replay Last or Replay INV.",
                  TFT_DARKGREY, 286, 1);
    while (digitalRead(BTN_OK) == HIGH) delay(20);
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(60);
    return true;
}

static void runLabReplay(bool inverted) {
    if (!hasLastRfCapture) {
        showLabMessage("NO CAPTURE", "Capture your light remote first.",
                       TFT_YELLOW);
        return;
    }

    if (!prepareAsyncTx(lastRfCaptureFreqKHz)) {
        drawNoRadio("RF REPLAY");
        return;
    }

    drawFrame("RF REPLAY", "Please wait...");
    drawStringCustom(18, 70, inverted ? "REPLAY INV" : "REPLAY", TFT_GREEN, 2);
    drawStringCustom(18, 106, "Freq : " + formatFreq(lastRfCaptureFreqKHz),
                     TFT_WHITE, 1);
    drawStringCustom(18, 124, "Pulses: " + String(lastRfCapture.count),
                     TFT_WHITE, 1);
    drawStringCustom(18, 142, "Repeats: " + String(RF_REPLAY_REPEATS),
                     TFT_WHITE, 1);
    drawStringFit(18, 174, "TX on GPIO15 -> CC1101 GDO0.",
                  TFT_DARKGREY, 286, 1);

    ccStrobe(CC_STX);
    delayMicroseconds(800);
    sendRfRaw(lastRfCapture, inverted);
    endAsyncTx();

    drawStringCustom(18, 194, "DONE", TFT_GREEN, 1);
    delay(700);
}

static void runCcLabReplay() {
    static const char* items[] = {
        "Capture 433",
        "Replay Last",
        "Replay INV"
    };

    bool exitSub = false;
    while (!exitSub) {
        drawLabReplayFrame();
        int choice = runSubMenu("RF LAB", items, sizeof(items) / sizeof(char*));
        switch (choice) {
            case -1: exitSub = true; break;
            case 0: runLabCapture(); break;
            case 1: runLabReplay(false); break;
            case 2: runLabReplay(true); break;
        }
    }
    endAsyncTx();
}

// ═══════════════════════════════════════════════════════════════════════
//  WATERFALL — barrido continuo por banda con historial deslizante.
//  Misma filosofía que el waterfall del RadioScanner nRF24.
// ═══════════════════════════════════════════════════════════════════════
static uint8_t  wfBuffer[WF_ROWS][WF_COLS];
static uint8_t  wfWriteRow = 0;

static uint16_t wfColor(uint8_t v) {
    if (v < 32)  return tft.color565(0, 0, v * 4);
    if (v < 96)  return tft.color565(0, (v - 32) * 4, 128 + (v - 32));
    if (v < 160) return tft.color565((v - 96) * 4, 255, 255 - (v - 96) * 4);
    if (v < 224) return tft.color565(255, 255 - (v - 160) * 4, 0);
    return tft.color565(255, 0, 0);
}

static void resetWaterfall() {
    memset(wfBuffer, 0, sizeof(wfBuffer));
    wfWriteRow = 0;
}

static void drawWaterfallStatic(const ScanBand& band) {
    drawFrame("CC1101 WATERFALL", "UP/DN:BAND  OK:BACK");
    drawStringCustom(12, 42, "Band: " + String(band.name) + " MHz", TFT_CYAN, 1);
    drawStringCustom(160, 42, formatFreq(band.startKHz) + "+", TFT_DARKGREY, 1);
    drawStringCustom(12, 58,
        "Step " + String(band.stepKHz) + " kHz  pts " + String(band.points),
        TFT_DARKGREY, 1);
    // Legend bar at the bottom
    for (int i = 0; i < 256; i++) {
        int x = 32 + (i * 240) / 256;
        tft.drawFastVLine(x, 220, 8, wfColor(i));
    }
    drawStringCustom(12, 220, "-110", TFT_WHITE, 1);
    drawStringCustom(278, 220, "-45", TFT_WHITE, 1);
}

static void renderWaterfallFrame(const ScanBand& band) {
    int graphX = 12;
    int graphY = 76;
    int graphW = 296;
    int graphH = WF_ROWS;
    int colW = max(1, graphW / band.points);

    for (int r = 0; r < graphH; r++) {
        int bufRow = (wfWriteRow + WF_ROWS - 1 - r) % WF_ROWS;
        for (uint8_t c = 0; c < band.points && c < WF_COLS; c++) {
            tft.fillRect(graphX + c * colW, graphY + r, colW, 1,
                         wfColor(wfBuffer[bufRow][c]));
        }
    }
}

static void runCcWaterfall() {
    uint8_t bandIndex = 1; // start at 433

    while (true) {
        const ScanBand& band = SCAN_BANDS[bandIndex];
        if (!prepareRx(band.startKHz)) {
            drawNoRadio("CC1101 WATERFALL");
            return;
        }
        drawWaterfallStatic(band);
        resetWaterfall();

        bool changeBand = false;
        unsigned long lastDraw = 0;

        while (!changeBand) {
            uint8_t row = wfWriteRow;
            int peakDbm = -127;
            uint8_t peakIdx = 0;

            for (uint8_t i = 0; i < band.points && i < WF_COLS; i++) {
                if (digitalRead(BTN_OK) == LOW) {
                    while (digitalRead(BTN_OK) == LOW) delay(5);
                    delay(60);
                    ccStrobe(CC_SIDLE);
                    return;
                }
                if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW) {
                    bool up = digitalRead(BTN_UP) == LOW;
                    while (digitalRead(BTN_UP) == LOW ||
                           digitalRead(BTN_DOWN) == LOW) delay(5);
                    bandIndex = up
                        ? (bandIndex + 1) % SCAN_BAND_COUNT
                        : (bandIndex + SCAN_BAND_COUNT - 1) % SCAN_BAND_COUNT;
                    changeBand = true;
                    break;
                }

                uint32_t freqKHz = band.startKHz + band.stepKHz * i;
                retuneRx(freqKHz);
                int dbm = readRssiFast();
                if (dbm > peakDbm) { peakDbm = dbm; peakIdx = i; }
                wfBuffer[row][i] =
                    (uint8_t)map(constrain(dbm, -110, -45), -110, -45, 0, 255);
            }

            if (!changeBand) {
                wfWriteRow = (wfWriteRow + 1) % WF_ROWS;
                if (millis() - lastDraw >= 80) {
                    renderWaterfallFrame(band);
                    tft.fillRect(12, 196, 296, 16, TFT_BLACK);
                    uint32_t peakFreq = band.startKHz + band.stepKHz * peakIdx;
                    drawStringCustom(12, 198, "Peak " + formatFreq(peakFreq),
                                     TFT_WHITE, 1);
                    drawStringCustom(200, 198, String(peakDbm) + " dBm",
                                     rssiColor(peakDbm), 1);
                    lastDraw = millis();
                }
            }
        }
    }
}


// ═══════════════════════════════════════════════════════════════════════
//  BRUTE SEARCH — centro + span + step. Acumula stats por bin sobre
//  múltiples barridos y muestra los top hits ordenados por actividad.
// ═══════════════════════════════════════════════════════════════════════
struct BruteBin {
    uint32_t freqKHz;
    int      peakDbm;
    int      sumDbm;
    uint16_t edges;
    uint16_t hits;
    uint16_t samples;
};
static BruteBin bruteBins[BRUTE_MAX_BINS];

static void resetBrute(uint32_t centerKHz, uint16_t spanKHz, uint16_t stepKHz,
                       uint8_t& binCount) {
    binCount = constrain(spanKHz / stepKHz, (uint16_t)1,
                         (uint16_t)BRUTE_MAX_BINS);
    uint32_t startKHz = centerKHz - (binCount / 2) * stepKHz;
    for (uint8_t i = 0; i < binCount; i++) {
        bruteBins[i].freqKHz = startKHz + (uint32_t)stepKHz * i;
        bruteBins[i].peakDbm = -127;
        bruteBins[i].sumDbm  = 0;
        bruteBins[i].edges   = 0;
        bruteBins[i].hits    = 0;
        bruteBins[i].samples = 0;
    }
}

static void drawBruteSetup(uint32_t centerKHz, uint16_t spanKHz,
                           uint16_t stepKHz, uint8_t binCount, uint8_t field) {
    drawFrame("BRUTE SEARCH", "UP/DN:VAL  OK:NEXT");
    drawStringCustom(12, 50, "Setup the sweep window.", TFT_CYAN, 1);

    uint16_t c0 = (field == 0) ? TFT_YELLOW : TFT_WHITE;
    uint16_t c1 = (field == 1) ? TFT_YELLOW : TFT_WHITE;
    uint16_t c2 = (field == 2) ? TFT_YELLOW : TFT_WHITE;

    drawStringCustom(20,  82, "Center:", c0, 2);
    drawStringCustom(140, 82, formatFreq(centerKHz), c0, 2);
    drawStringCustom(20, 114, "Span  :", c1, 2);
    drawStringCustom(140, 114, String(spanKHz) + " kHz", c1, 2);
    drawStringCustom(20, 146, "Step  :", c2, 2);
    drawStringCustom(140, 146, String(stepKHz) + " kHz", c2, 2);
    drawStringCustom(20, 178, "Bins  :", TFT_DARKGREY, 2);
    drawStringCustom(140, 178, String(binCount), TFT_DARKGREY, 2);

    drawStringFit(12, 212, "OK long-press: start sweep", TFT_GREEN, 296, 1);
}

static bool bruteSetupScreen(uint32_t& centerKHz, uint16_t& spanKHz,
                             uint16_t& stepKHz) {
    static const uint32_t presets[] = { 315000, 390000, 433920, 868350, 915000 };
    static const uint16_t stepOpts[] = { 10, 25, 50, 100, 200 };
    uint8_t presetIdx = 2;
    uint8_t stepIdx   = 1;
    centerKHz = presets[presetIdx];
    spanKHz   = BRUTE_DEFAULT_SPAN_KHZ;
    stepKHz   = stepOpts[stepIdx];

    uint8_t field = 0;
    while (true) {
        uint8_t binCount = constrain(spanKHz / stepKHz, (uint16_t)1,
                                     (uint16_t)BRUTE_MAX_BINS);
        drawBruteSetup(centerKHz, spanKHz, stepKHz, binCount, field);

        while (digitalRead(BTN_UP) == HIGH &&
               digitalRead(BTN_DOWN) == HIGH &&
               digitalRead(BTN_OK) == HIGH) delay(10);

        if (digitalRead(BTN_OK) == LOW) {
            bool wasLong = waitOkReleaseWasLong();
            delay(60);
            if (wasLong) return true;
            field = (field + 1) % 3;
            continue;
        }

        bool up = digitalRead(BTN_UP) == LOW;
        while (digitalRead(BTN_UP) == LOW ||
               digitalRead(BTN_DOWN) == LOW) delay(10);
        delay(40);

        switch (field) {
            case 0:
                presetIdx = up ? (presetIdx + 1) % 5 : (presetIdx + 4) % 5;
                centerKHz = presets[presetIdx];
                break;
            case 1:
                if (up)  spanKHz = min<uint16_t>(spanKHz + 500, 8000);
                else     spanKHz = max<uint16_t>(spanKHz - 500, 500);
                break;
            case 2:
                stepIdx = up ? (stepIdx + 1) % 5 : (stepIdx + 4) % 5;
                stepKHz = stepOpts[stepIdx];
                break;
        }
    }
}

static void drawBruteSweep(uint32_t centerKHz, uint16_t spanKHz,
                           uint16_t stepKHz, uint8_t binCount,
                           uint32_t curFreqKHz, unsigned long sweeps) {
    drawFrame("BRUTE SWEEP", "OK:STOP & RESULTS");
    drawStringCustom(12, 42,
        "Ctr " + formatFreq(centerKHz) + "  span " + String(spanKHz) + " kHz",
        TFT_CYAN, 1);
    drawStringCustom(12, 58,
        "Step " + String(stepKHz) + " kHz  bins " + String(binCount) +
        "  sweep " + String(sweeps),
        TFT_DARKGREY, 1);
    drawStringCustom(12, 78, "Now: " + formatFreq(curFreqKHz), TFT_WHITE, 1);

    int graphX = 12, graphY = 100, graphW = 296, graphH = 96;
    tft.fillRect(graphX, graphY, graphW, graphH, TFT_BLACK);
    tft.drawRect(graphX, graphY, graphW, graphH, TFT_DARKGREY);
    int barW = max(1, graphW / binCount);
    for (uint8_t i = 0; i < binCount; i++) {
        int dbm = bruteBins[i].peakDbm;
        int h = map(constrain(dbm, -110, -45), -110, -45, 2, graphH - 4);
        int x = graphX + i * barW;
        uint16_t color = bruteBins[i].hits > 0 ? TFT_GREEN : rssiColor(dbm);
        tft.fillRect(x, graphY + graphH - 2 - h, max(1, barW - 1), h, color);
    }
}

static void drawBruteResults(uint8_t binCount) {
    uint8_t order[BRUTE_MAX_BINS];
    for (uint8_t i = 0; i < binCount; i++) order[i] = i;
    for (uint8_t i = 1; i < binCount; i++) {
        uint8_t v = order[i];
        int j = i - 1;
        auto score = [](uint8_t idx) {
            return (long)bruteBins[idx].hits * 1000L +
                   (bruteBins[idx].peakDbm + 128);
        };
        while (j >= 0 && score(order[j]) < score(v)) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }

    drawFrame("BRUTE RESULTS", "OK:BACK");
    drawStringCustom(12, 42, "Top hits ordered by activity:", TFT_CYAN, 1);

    uint8_t count = min<uint8_t>(BRUTE_TOP_HITS, binCount);
    for (uint8_t i = 0; i < count; i++) {
        uint8_t idx = order[i];
        const BruteBin& b = bruteBins[idx];
        int y = 62 + i * 24;
        drawStringCustom(12, y,
            String(i + 1) + ". " + formatFreq(b.freqKHz), TFT_WHITE, 1);
        String stats = String(b.peakDbm) + " dBm  E:" + String(b.edges) +
                       "  H:" + String(b.hits) + "/" + String(b.samples);
        uint16_t col = (b.hits > 0) ? TFT_GREEN : TFT_DARKGREY;
        drawStringCustom(140, y, stats, col, 1);
    }
}

static void runCcBruteSearch() {
    uint32_t centerKHz;
    uint16_t spanKHz, stepKHz;
    if (!bruteSetupScreen(centerKHz, spanKHz, stepKHz)) return;

    uint8_t binCount;
    resetBrute(centerKHz, spanKHz, stepKHz, binCount);

    if (!prepareRx(bruteBins[0].freqKHz)) {
        drawNoRadio("BRUTE SEARCH");
        return;
    }

    int noise[BRUTE_MAX_BINS];
    drawFrame("CALIBRATING", "");
    drawStringCustom(20, 100, "Sampling noise floor...", TFT_YELLOW, 2);
    for (uint8_t i = 0; i < binCount; i++) {
        retuneRx(bruteBins[i].freqKHz);
        noise[i] = readRssiAverage();
    }

    unsigned long sweeps = 0;
    unsigned long lastDraw = 0;

    while (true) {
        for (uint8_t i = 0; i < binCount; i++) {
            if (digitalRead(BTN_OK) == LOW) {
                while (digitalRead(BTN_OK) == LOW) delay(5);
                delay(60);
                ccStrobe(CC_SIDLE);
                drawBruteResults(binCount);
                while (digitalRead(BTN_OK) == HIGH) delay(10);
                while (digitalRead(BTN_OK) == LOW) delay(5);
                return;
            }

            retuneRx(bruteBins[i].freqKHz);

            int peak = -127;
            int total = 0;
            for (uint8_t s = 0; s < BRUTE_DWELL_SAMPLES; s++) {
                int dbm = readRssiFast();
                total += dbm;
                if (dbm > peak) peak = dbm;
                delayMicroseconds(300);
            }
            uint16_t edges = sampleGdoEdgesFast(800);

            BruteBin& b = bruteBins[i];
            b.samples++;
            b.sumDbm += peak;
            if (peak > b.peakDbm) b.peakDbm = peak;
            b.edges += edges;
            if (peak >= noise[i] + 8 || edges >= 3) b.hits++;

            if (millis() - lastDraw >= 250) {
                drawBruteSweep(centerKHz, spanKHz, stepKHz, binCount,
                               b.freqKHz, sweeps);
                lastDraw = millis();
            }
        }
        sweeps++;
    }
}



// =============================================================================
// TEST BEACON - maximum-power test signal.
// Uses +10 dBm CC1101 PATABLE values for 433/915 MHz.
// Longer burst timing makes the signal easier to see on an analyzer.
// =============================================================================
struct BeaconPreset { const char* label; uint32_t freqKHz; };

static const BeaconPreset BEACON_PRESETS[] = {
    { "433.92 ISM", 433920 },
    { "915.00 ISM", 915000 },
};

static constexpr uint8_t BEACON_PRESET_COUNT =
    sizeof(BEACON_PRESETS) / sizeof(BEACON_PRESETS[0]);

struct BeaconPower {
    const char* label;
    uint8_t pa433;
    uint8_t pa915;
};

static const BeaconPower BEACON_POWERS[] = {
    { "+10 dBm MAX", CC_PATABLE_MAX_433, CC_PATABLE_MAX_915 },
};

static constexpr uint8_t BEACON_POWER_COUNT =
    sizeof(BEACON_POWERS) / sizeof(BEACON_POWERS[0]);

static uint8_t getBeaconPaValue(uint32_t freqKHz, uint8_t powerIdx) {
    return (freqKHz < 600000)
        ? BEACON_POWERS[powerIdx].pa433
        : BEACON_POWERS[powerIdx].pa915;
}

static void drawBeaconSetup(uint8_t freqIdx, uint8_t powerIdx, uint8_t field) {
    drawFrame("TEST BEACON", "UP/DN:VAL  OK:NEXT");
    drawStringCustom(12, 44, "Maximum-power test signal.", TFT_CYAN, 1);
    drawStringCustom(12, 58, "ISM only. Analyzer validation / demo.",
                     TFT_DARKGREY, 1);

    uint16_t c0 = (field == 0) ? TFT_YELLOW : TFT_WHITE;
    uint16_t c1 = (field == 1) ? TFT_YELLOW : TFT_WHITE;

    drawStringCustom(20,  88, "Freq :", c0, 2);
    drawStringCustom(120, 88, BEACON_PRESETS[freqIdx].label, c0, 2);

    drawStringCustom(20, 124, "Power:", c1, 2);
    drawStringCustom(120, 124, BEACON_POWERS[powerIdx].label, c1, 2);

    drawStringCustom(20, 160, "Burst:", TFT_DARKGREY, 2);
    drawStringCustom(120, 160,
        String(BEACON_BURST_MS) + " / " + String(BEACON_GAP_MS) + " ms",
        TFT_DARKGREY, 2);

    drawStringFit(12, 200, "OK long-press: start beacon", TFT_GREEN, 296, 1);
    drawStringFit(12, 216, "OK short-press: cycle field", TFT_DARKGREY, 296, 1);
}

static bool beaconSetupScreen(uint8_t& freqIdx, uint8_t& powerIdx) {
    freqIdx = 0;
    powerIdx = 0;
    uint8_t field = 0;

    while (true) {
        drawBeaconSetup(freqIdx, powerIdx, field);

        while (digitalRead(BTN_UP) == HIGH &&
               digitalRead(BTN_DOWN) == HIGH &&
               digitalRead(BTN_OK) == HIGH) delay(10);

        if (digitalRead(BTN_OK) == LOW) {
            bool wasLong = waitOkReleaseWasLong();
            delay(60);
            if (wasLong) return true;
            field = (field + 1) % 2;
            continue;
        }

        bool up = digitalRead(BTN_UP) == LOW;

        while (digitalRead(BTN_UP) == LOW ||
               digitalRead(BTN_DOWN) == LOW) delay(10);

        delay(40);

        if (field == 0) {
            freqIdx = up
                ? (freqIdx + 1) % BEACON_PRESET_COUNT
                : (freqIdx + BEACON_PRESET_COUNT - 1) % BEACON_PRESET_COUNT;
        } else {
            powerIdx = up
                ? (powerIdx + 1) % BEACON_POWER_COUNT
                : (powerIdx + BEACON_POWER_COUNT - 1) % BEACON_POWER_COUNT;
        }
    }
}

static void drawBeaconLive(uint32_t freqKHz, const char* powerLabel,
                           uint32_t bursts, bool transmitting) {
    drawFrame("BEACON ACTIVE", "OK:STOP");

    drawStringCustom(12, 50, "Freq :", TFT_WHITE, 2);
    drawStringCustom(120, 50, formatFreq(freqKHz), TFT_CYAN, 2);

    drawStringCustom(12, 84, "Power:", TFT_WHITE, 2);
    drawStringCustom(120, 84, powerLabel, TFT_CYAN, 2);

    drawStringCustom(12, 118, "Burst:", TFT_WHITE, 2);
    drawStringCustom(120, 118,
        String(BEACON_BURST_MS) + "ms ON / " + String(BEACON_GAP_MS) + "ms",
        TFT_CYAN, 1);

    drawStringCustom(12, 146, "Count:", TFT_WHITE, 2);
    drawStringCustom(120, 146, String(bursts), TFT_YELLOW, 2);

    uint16_t color = transmitting ? TFT_RED : TFT_DARKGREY;
    const char* label = transmitting ? "TX" : "IDLE";

    tft.fillRect(220, 180, 88, 36, color);
    tft.drawRect(220, 180, 88, 36, TFT_WHITE);
    drawStringCustom(244, 192, label, TFT_BLACK, 2);

    drawStringFit(12, 198, "Watch your analyzer for blips.",
                  TFT_DARKGREY, 200, 1);
}

static void runCcTestBeacon() {
    uint8_t freqIdx, powerIdx;

    if (!beaconSetupScreen(freqIdx, powerIdx)) return;

    uint32_t freqKHz = BEACON_PRESETS[freqIdx].freqKHz;
    uint8_t paValue = getBeaconPaValue(freqKHz, powerIdx);
    const char* powerLabel = BEACON_POWERS[powerIdx].label;

    if (!prepareAsyncTx(freqKHz)) {
        drawNoRadio("TEST BEACON");
        return;
    }

    // Potencia maxima del CC1101: aprox. +10 dBm.
    uint8_t patable[] = { paValue };
    ccWriteBurst(CC_PATABLE, patable, sizeof(patable));

    uint32_t bursts = 0;
    drawBeaconLive(freqKHz, powerLabel, bursts, false);

    while (true) {
        if (digitalRead(BTN_OK) == LOW) {
            while (digitalRead(BTN_OK) == LOW) delay(5);
            delay(60);
            endAsyncTx();
            return;
        }

        drawBeaconLive(freqKHz, powerLabel, bursts, true);

        ccStrobe(CC_STX);
        delayMicroseconds(800);

        uint32_t burstStart = millis();

        while (millis() - burstStart < BEACON_BURST_MS) {
            digitalWrite(CC1101_TX_DATA_PIN, HIGH);
            delayMicroseconds(BEACON_TOGGLE_US);

            digitalWrite(CC1101_TX_DATA_PIN, LOW);
            delayMicroseconds(BEACON_TOGGLE_US);

            if (digitalRead(BTN_OK) == LOW) break;
        }

        digitalWrite(CC1101_TX_DATA_PIN, LOW);
        ccStrobe(CC_SIDLE);

        bursts++;

        drawBeaconLive(freqKHz, powerLabel, bursts, false);

        uint32_t gapStart = millis();

        while (millis() - gapStart < BEACON_GAP_MS) {
            if (digitalRead(BTN_OK) == LOW) break;
            delay(10);
        }
    }
}



void runCC1101Tools() {
    static const char* items[] = {
        "Hardware Diag",
        "Spectrum Scan",
        "Waterfall",
        "Frequency Mon",
        "Freq Finder",
        "Brute Search",
        "Code Check",
        "RF Analyzer",
        "RF Raw View",
        "RF Live",
        "Lab Replay",
        "Test Beacon"
    };

    bool exitSub = false;
    while (!exitSub) {
        int choice = runSubMenu("CC1101", items, sizeof(items) / sizeof(char*));
        switch (choice) {
            case -1: exitSub = true; break;
            case  0: runCcDiag();           break;
            case  1: runCcSpectrum();       break;
            case  2: runCcWaterfall();      break;
            case  3: runCcFrequencyMonitor(); break;
            case  4: runCcFrequencyFinder(); break;
            case  5: runCcBruteSearch();    break;
            case  6: runCcCodeCheck();      break;
            case  7: runRfSignalAnalyzer(); break;
            case  8: runRfRawViewer();      break;
            case  9: runRfLiveDetector();   break;
            case 10: runCcLabReplay();      break;
            case 11: runCcTestBeacon();     break;
        }
    }
}
