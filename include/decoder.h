#pragma once
#include "sdr.h"
#include <cstdint>
#include <array>

struct ModeSFrame {
    std::array<uint8_t, 14> payload;  // 112 bits = 14 bytes
    uint8_t  downlink_format;
    uint32_t icao_address;
    bool     crc_valid;
    float    signal_power;
};

class ModeSDecoder {
public:
    bool decode(const IQSample* frame_start, size_t available, ModeSFrame& out);
    uint32_t crc24(const uint8_t* data, size_t bits);

private:
    void extractBits(const IQSample* samples, uint8_t* payload);
    float estimatePower(const IQSample* samples, size_t count);
};