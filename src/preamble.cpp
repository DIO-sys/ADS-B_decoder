#include "preamble.h"
#include <cmath>

constexpr int PreambleDetector::PULSE_POSITIONS[];

void PreambleDetector::setThreshold(float threshold) {
    threshold_ = threshold;
}

float PreambleDetector::correlate(const IQSample* s) const {
    auto mag = [](IQSample iq) -> float {
        float i = iq.real(), q = iq.imag();
        return std::sqrt(i * i + q * q);
    };

    // Energy at expected pulse positions
    float pulse_energy = 0.0f;
    for (int pos : PULSE_POSITIONS) {
        pulse_energy += mag(s[pos]);
    }

    // Energy at expected quiet positions
    float quiet_energy = 0.0f;
    int quiet_count = 0;
    for (int i = 0; i < PREAMBLE_SAMPLES; i++) {
        bool is_pulse = false;
        for (int p : PULSE_POSITIONS) {
            if (i == p) { is_pulse = true; break; }
        }
        if (!is_pulse) {
            quiet_energy += mag(s[i]);
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

    // Need preamble (16 samples) + payload (112 bits * 2 samples = 224)
    if (count < PREAMBLE_SAMPLES + 224) return offsets;

    for (size_t i = 0; i <= count - PREAMBLE_SAMPLES - 224; i++) {
        float score = correlate(&samples[i]);
        if (score > threshold_) {
            offsets.push_back(i);
            i += PREAMBLE_SAMPLES + 224 - 1; // skip past frame
        }
    }
    return offsets;
}