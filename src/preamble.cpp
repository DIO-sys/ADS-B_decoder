#include "preamble.h"
#include <cmath>
#include <algorithm>

constexpr int PreambleDetector::PULSE_POSITIONS[];

static float iq_magnitude(IQSample iq) {
    float i = iq.real(), q = iq.imag();
    return std::sqrt(i * i + q * q);
}

void PreambleDetector::setThreshold(float threshold) {
    threshold_ = threshold;
}

float PreambleDetector::correlate(const IQSample* s) const {
    // Compute magnitudes for the 16 preamble samples
    float m[16];
    for (int i = 0; i < 16; i++) {
        m[i] = iq_magnitude(s[i]);
    }

    // dump1090-style checks:
    // Pulses at 0,1 and 2,3 (first pulse pair)
    // Quiet at 3,4,5,6
    // Pulses at 7,8 and 9,10
    // Quiet at 11,12,13,14,15

    // Each pulse is 0.5μs = 1 sample, but at 2MSPS the pulse
    // spreads across adjacent samples. Check pairs.

    // High samples (pulse positions and their neighbors)
    float high0 = m[0];
    float high1 = m[2];
    float high2 = m[7];
    float high3 = m[9];

    // The quiet gaps must be significantly lower
    float quiet1 = (m[1] + m[3] + m[4] + m[5] + m[6]) / 5.0f;   // between pulse pairs
    float quiet2 = (m[8]) ;                                        // between second pair
    float quiet3 = (m[10] + m[11] + m[12] + m[13] + m[14] + m[15]) / 6.0f; // after preamble

    float min_high = std::min({high0, high1, high2, high3});
    float max_quiet = std::max({quiet1, quiet2, quiet3});

    if (max_quiet < 1.0f) max_quiet = 1.0f;

    return min_high / max_quiet;
}

std::vector<size_t> PreambleDetector::detect(const IQSample* samples, size_t count) {
    std::vector<size_t> offsets;

    if (count < PREAMBLE_SAMPLES + 224) return offsets;

    // Pre-compute all magnitudes
    std::vector<float> mag(count);
    for (size_t i = 0; i < count; i++) {
        mag[i] = iq_magnitude(samples[i]);
    }

    for (size_t i = 0; i <= count - PREAMBLE_SAMPLES - 224; i++) {
        // Quick reject: pulse positions must be above average
        if (mag[i] < mag[i+1]) continue;
        if (mag[i+2] < mag[i+3]) continue;
        if (mag[i+7] < mag[i+6]) continue;
        if (mag[i+9] < mag[i+10]) continue;

        // Pulse magnitudes must exceed quiet gaps
        float high = mag[i] + mag[i+2] + mag[i+7] + mag[i+9];
        float low = mag[i+1] + mag[i+3] + mag[i+4] + mag[i+5] + mag[i+6];

        if (high < 2.0f * low) continue;

        float low2 = mag[i+8] + mag[i+10] + mag[i+11] + mag[i+12] + mag[i+13] + mag[i+14];
        if (high < 2.0f * low2) continue;

        offsets.push_back(i);
        i += PREAMBLE_SAMPLES + 224 - 1;
    }
    return offsets;
}