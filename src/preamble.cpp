#include "preamble.h"
#include <cmath>

constexpr int PreambleDetector::PULSE_POSITIONS[];

static float iq_magnitude(IQSample iq) {
    float i = iq.real(), q = iq.imag();
    return std::sqrt(i * i + q * q);
}

void PreambleDetector::setThreshold(float threshold) {
    threshold_ = threshold;
}

float PreambleDetector::correlate(const IQSample* s) const {
    float pulse_energy = 0.0f;
    for (int pos : PULSE_POSITIONS) {
        pulse_energy += iq_magnitude(s[pos]);
    }

    float quiet_energy = 0.0f;
    int quiet_count = 0;
    for (int i = 0; i < PREAMBLE_SAMPLES; i++) {
        bool is_pulse = false;
        for (int p : PULSE_POSITIONS) {
            if (i == p) { is_pulse = true; break; }
        }
        if (!is_pulse) {
            quiet_energy += iq_magnitude(s[i]);
            quiet_count++;
        }
    }

    if (quiet_count == 0) return 0.0f;
    float avg_quiet = quiet_energy / quiet_count;
    if (avg_quiet < 1.0f) avg_quiet = 1.0f;

    return pulse_energy / (4.0f * avg_quiet);
}

std::vector<size_t> PreambleDetector::detect(const IQSample* samples, size_t count) {
    std::vector<size_t> offsets;

    if (count < PREAMBLE_SAMPLES + 224) return offsets;

    for (size_t i = 0; i <= count - PREAMBLE_SAMPLES - 224; i++) {
        float score = correlate(&samples[i]);
        if (score > threshold_) {
            offsets.push_back(i);
            i += PREAMBLE_SAMPLES + 224 - 1;
        }
    }
    return offsets;
}