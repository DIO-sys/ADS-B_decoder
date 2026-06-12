#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <fstream>
#include <complex>
#include <unordered_map>
#include <libbladeRF.h>

using IQSample = std::complex<int16_t>;

struct SdrConfig {
    uint64_t frequency_hz  = 1090000000;
    uint32_t sample_rate   = 2000000;
    uint32_t bandwidth     = 2000000;
    uint32_t num_buffers   = 16;
    uint32_t buffer_size   = 16384;
    uint32_t num_transfers = 8;
    uint32_t timeout_ms    = 3500;
};

class SdrCapture {
public:
    ~SdrCapture();

    bool initFromFile(const std::string& path);
    bool initLive(const SdrConfig& cfg = SdrConfig{});
    static SdrConfig loadConfig(const std::string& path);
    size_t readSamples(IQSample* buf, size_t max_samples);
    void stop();
    bool isLive() const { return live_; }

private:
    std::ifstream file_;
    struct bladerf* dev_ = nullptr;
    bool live_ = false;
};