#ifndef IR_PROTOCOL_DECODER_H
#define IR_PROTOCOL_DECODER_H

#include <Arduino.h>

struct IrProtocolDecodeResult {
    const char* protocol = "RAW";
    String code = "-";
    String detail = "No protocol match";
    uint8_t bits = 0;
    uint8_t confidence = 0;
    bool decoded = false;
    bool repeat = false;
};

IrProtocolDecodeResult decodeIrProtocol(const uint16_t* raw, uint16_t count);
String irRawPreview(const uint16_t* raw, uint16_t count, uint8_t maxItems = 10);
uint32_t irRawTotalUs(const uint16_t* raw, uint16_t count);

#endif
