#include "IrProtocolDecoder.h"

static bool matchUs(uint32_t value, uint32_t target, uint8_t tolerancePct = 30) {
    uint32_t tolerance = max<uint32_t>(160, (target * tolerancePct) / 100);
    return value >= target - tolerance && value <= target + tolerance;
}

static bool matchPulseDistance(const uint16_t* raw, uint16_t count,
                               uint16_t headerMark, uint16_t headerSpace,
                               uint16_t bitMark, uint16_t zeroSpace,
                               uint16_t oneSpace, uint8_t bits,
                               uint64_t* valueOut) {
    if (count < 2 + bits * 2) return false;
    if (!matchUs(raw[0], headerMark)) return false;
    if (!matchUs(raw[1], headerSpace)) return false;

    uint64_t value = 0;
    uint16_t idx = 2;
    for (uint8_t bit = 0; bit < bits; bit++) {
        if (!matchUs(raw[idx], bitMark)) return false;

        uint16_t space = raw[idx + 1];
        bool isZero = matchUs(space, zeroSpace);
        bool isOne = matchUs(space, oneSpace);
        if (!isZero && !isOne) return false;
        if (isOne) value |= (1ULL << bit);
        idx += 2;
    }

    if (valueOut) *valueOut = value;
    return true;
}

static String hexFromValue(uint64_t value, uint8_t bits) {
    if (bits == 0) return "-";

    uint8_t nibbles = (bits + 3) / 4;
    if (nibbles == 0) nibbles = 1;
    if (nibbles > 16) nibbles = 16;

    String text = "0x";
    for (int8_t i = nibbles - 1; i >= 0; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0x0F;
        text += static_cast<char>(nibble < 10 ? '0' + nibble
                                              : 'A' + (nibble - 10));
    }
    return text;
}

static bool decodeNecFamily(const uint16_t* raw, uint16_t count,
                            IrProtocolDecodeResult& result) {
    if (count >= 3 &&
        matchUs(raw[0], 9000) &&
        matchUs(raw[1], 2250) &&
        matchUs(raw[2], 560)) {
        result.protocol = "NEC REPEAT";
        result.code = "-";
        result.detail = "Repeat frame";
        result.bits = 0;
        result.confidence = 90;
        result.decoded = true;
        result.repeat = true;
        return true;
    }

    uint64_t value = 0;
    if (matchPulseDistance(raw, count, 9000, 4500, 560, 560, 1690, 32,
                           &value)) {
        result.protocol = "NEC";
        result.code = hexFromValue(value, 32);
        result.detail = "32-bit pulse distance";
        result.bits = 32;
        result.confidence = 95;
        result.decoded = true;
        return true;
    }

    if (matchPulseDistance(raw, count, 4500, 4500, 560, 560, 1690, 32,
                           &value)) {
        result.protocol = "SAMSUNG";
        result.code = hexFromValue(value, 32);
        result.detail = "32-bit NEC-like";
        result.bits = 32;
        result.confidence = 88;
        result.decoded = true;
        return true;
    }

    if (matchPulseDistance(raw, count, 9000, 4200, 560, 560, 1690, 28,
                           &value)) {
        result.protocol = "LG";
        result.code = hexFromValue(value, 28);
        result.detail = "28-bit NEC-like";
        result.bits = 28;
        result.confidence = 82;
        result.decoded = true;
        return true;
    }

    return false;
}

static bool decodeSony(const uint16_t* raw, uint16_t count,
                       IrProtocolDecodeResult& result) {
    if (count < 2 || !matchUs(raw[0], 2400) || !matchUs(raw[1], 600)) {
        return false;
    }

    const uint8_t bitOptions[] = { 20, 15, 12 };
    for (uint8_t option = 0; option < sizeof(bitOptions); option++) {
        uint8_t bits = bitOptions[option];
        if (count < 2 + bits * 2 - 1) continue;

        uint64_t value = 0;
        bool ok = true;
        uint16_t idx = 2;
        for (uint8_t bit = 0; bit < bits; bit++) {
            bool isZero = matchUs(raw[idx], 600);
            bool isOne = matchUs(raw[idx], 1200);
            if (!isZero && !isOne) {
                ok = false;
                break;
            }
            if (isOne) value |= (1ULL << bit);
            idx++;
            if (idx < count && bit < bits - 1) {
                if (!matchUs(raw[idx], 600)) {
                    ok = false;
                    break;
                }
                idx++;
            }
        }

        if (ok) {
            result.protocol = "SONY";
            result.code = hexFromValue(value, bits);
            result.detail = String(bits) + "-bit SIRC";
            result.bits = bits;
            result.confidence = 86;
            result.decoded = true;
            return true;
        }
    }

    return false;
}

static bool decodePanasonic(const uint16_t* raw, uint16_t count,
                            IrProtocolDecodeResult& result) {
    uint64_t value = 0;
    if (!matchPulseDistance(raw, count, 3500, 1750, 430, 430, 1290, 48,
                            &value)) {
        return false;
    }

    result.protocol = "PANASONIC";
    result.code = hexFromValue(value, 48);
    result.detail = "48-bit pulse distance";
    result.bits = 48;
    result.confidence = 78;
    result.decoded = true;
    return true;
}

static bool classifyRcLike(const uint16_t* raw, uint16_t count,
                           IrProtocolDecodeResult& result) {
    if (count < 12 || count > 40) return false;

    if (count >= 4 &&
        matchUs(raw[0], 2666, 25) &&
        matchUs(raw[1], 889, 30)) {
        result.protocol = "RC6";
        result.code = "-";
        result.detail = "Manchester-like header";
        result.bits = 0;
        result.confidence = 60;
        result.decoded = true;
        return true;
    }

    uint8_t rc5Like = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (matchUs(raw[i], 889, 35) || matchUs(raw[i], 1778, 35)) rc5Like++;
    }

    if (rc5Like >= count * 3 / 4) {
        result.protocol = "RC5";
        result.code = "-";
        result.detail = "Manchester-like timings";
        result.bits = 0;
        result.confidence = 55;
        result.decoded = true;
        return true;
    }

    return false;
}

IrProtocolDecodeResult decodeIrProtocol(const uint16_t* raw, uint16_t count) {
    IrProtocolDecodeResult result;

    if (!raw || count == 0) return result;
    if (decodeNecFamily(raw, count, result)) return result;
    if (decodeSony(raw, count, result)) return result;
    if (decodePanasonic(raw, count, result)) return result;
    if (classifyRcLike(raw, count, result)) return result;

    result.protocol = "RAW";
    result.code = "-";
    result.detail = "Unknown or AC/stateful";
    result.confidence = 20;
    result.bits = 0;
    result.decoded = false;
    return result;
}

String irRawPreview(const uint16_t* raw, uint16_t count, uint8_t maxItems) {
    if (!raw || count == 0) return "-";

    String text;
    uint8_t shown = min<uint16_t>(maxItems, count);
    for (uint8_t i = 0; i < shown; i++) {
        if (i) text += ",";
        text += String(raw[i]);
    }
    if (count > shown) text += ",..";
    return text;
}

uint32_t irRawTotalUs(const uint16_t* raw, uint16_t count) {
    uint32_t total = 0;
    if (!raw) return total;
    for (uint16_t i = 0; i < count; i++) total += raw[i];
    return total;
}
