#pragma once
#include "sdr.h"
#include <vector>

class PreambleDetector {
public:
    void setThreshold(float threshold);
    std::vector<size_t> detect(const IQSample* samples, size_t count);

private:
    float threshold_ = 0.0f;

    // 8μs preamble = 16 samples at 2 MSPS
    // Pulses at 0, 2, 7, 9 (each 0.5μs wide)
    static constexpr int PREAMBLE_SAMPLES = 16;
    static constexpr int PULSE_POSITIONS[] = {0, 2, 7, 9};

    float correlate(const IQSample* samples) const;
};