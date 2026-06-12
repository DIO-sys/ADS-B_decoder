#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <fstream>
#include <complex>

using IQSample = std::complex<int16_t>;

class SdrCapture {
public:
    bool initFromFile(const std::string& path);
    size_t readSamples(IQSample* buf, size_t max_samples);
    void stop();

private:
    std::ifstream file_;
};