#include "decoder.h"
#include <cmath>
#include <cstring>

static float iq_magnitude(IQSample s) {
    float i = s.real(), q = s.imag();
    return std::sqrt(i * i + q * q);
}

void ModeSDecoder::extractBits(const IQSample* samples, uint8_t* payload) {
    std::memset(payload, 0, 14);

    for (int bit = 0; bit < 112; bit++) {
        float early = iq_magnitude(samples[bit * 2]);
        float late  = iq_magnitude(samples[bit * 2 + 1]);

        if (early > late) {
            int byte_index = bit / 8;
            int bit_position = 7 - (bit % 8);
            payload[byte_index] |= (1 << bit_position);
        }
    }
}

uint32_t ModeSDecoder::crc24(const uint8_t* data, size_t bits) {
    uint32_t crc = 0;
    size_t bytes = bits / 8;

    for (size_t i = 0; i < bytes; i++) {
        crc ^= static_cast<uint32_t>(data[i]) << 16;
        for (int j = 0; j < 8; j++) {
            crc <<= 1;
            if (crc & 0x1000000) {
                crc ^= 0x1FFF409;
            }
        }
    }

    return crc & 0xFFFFFF;
}

float ModeSDecoder::estimatePower(const IQSample* samples, size_t count) {
    float sum = 0.0f;
    for (size_t i = 0; i < count; i++) {
        float m = iq_magnitude(samples[i]);
        sum += m * m;
    }
    float rms = std::sqrt(sum / count);
    return 20.0f * std::log10(rms / 32768.0f + 1e-10f);
}

bool ModeSDecoder::decode(const IQSample* frame_start, size_t available, ModeSFrame& out) {
    // 112 bits * 2 samples/bit = 224 samples needed
    if (available < 224) return false;

    extractBits(frame_start, out.payload.data());

    out.downlink_format = (out.payload[0] >> 3) & 0x1F;
    out.signal_power = estimatePower(frame_start, 224);

    // CRC-24: compute over first 88 bits, compare to last 24 bits
    uint32_t computed = crc24(out.payload.data(), 88);
    uint32_t received = (out.payload[11] << 16) | (out.payload[12] << 8) | out.payload[13];

    if (out.downlink_format == 17) {
        out.crc_valid = (computed == received);
        out.icao_address = (out.payload[1] << 16) | (out.payload[2] << 8) | out.payload[3];
    } else if (out.downlink_format == 11) {
        // DF11: ICAO address XORed into CRC remainder
        out.icao_address = computed ^ received;
        out.crc_valid = true;
    } else {
        out.icao_address = 0;
        out.crc_valid = false;
    }

    return true;
}