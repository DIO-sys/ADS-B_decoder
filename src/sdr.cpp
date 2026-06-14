#include "sdr.h"
#include <iostream>
#include <sstream>

SdrCapture::~SdrCapture() { stop(); }

SdrConfig SdrCapture::loadConfig(const std::string& path) {
    SdrConfig cfg;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[SdrCapture] No config file, using defaults\n";
        return cfg;
    }

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if      (key == "frequency_hz")  cfg.frequency_hz  = std::stoull(val);
        else if (key == "sample_rate")   cfg.sample_rate   = std::stoul(val);
        else if (key == "bandwidth")     cfg.bandwidth     = std::stoul(val);
        else if (key == "num_buffers")   cfg.num_buffers   = std::stoul(val);
        else if (key == "buffer_size")   cfg.buffer_size   = std::stoul(val);
        else if (key == "num_transfers") cfg.num_transfers = std::stoul(val);
        else if (key == "timeout_ms")    cfg.timeout_ms    = std::stoul(val);
    }

    std::cout << "[SdrCapture] Loaded config from " << path << "\n";
    return cfg;
}

bool SdrCapture::initFromFile(const std::string& path, IQFormat fmt) {
    file_.open(path, std::ios::binary);
    if (!file_.is_open()) {
        std::cerr << "[SdrCapture] Failed to open: " << path << "\n";
        return false;
    }
    format_ = fmt;
    live_ = false;
    std::cout << "[SdrCapture] Opened IQ file: " << path
              << " (format: " << (fmt == IQFormat::UC8 ? "UC8" : "SC16Q11") << ")\n";
    return true;
}

bool SdrCapture::initLive(const SdrConfig& cfg) {
    int status;

    status = bladerf_open(&dev_, nullptr);
    if (status != 0) {
        std::cerr << "[SdrCapture] Failed to open BladeRF: "
                  << bladerf_strerror(status) << "\n";
        return false;
    }

    status = bladerf_set_frequency(dev_, BLADERF_CHANNEL_RX(0), cfg.frequency_hz);
    if (status != 0) {
        std::cerr << "[SdrCapture] Failed to set frequency: "
                  << bladerf_strerror(status) << "\n";
        return false;
    }

    status = bladerf_set_sample_rate(dev_, BLADERF_CHANNEL_RX(0), cfg.sample_rate, nullptr);
    if (status != 0) {
        std::cerr << "[SdrCapture] Failed to set sample rate: "
                  << bladerf_strerror(status) << "\n";
        return false;
    }

    status = bladerf_set_bandwidth(dev_, BLADERF_CHANNEL_RX(0), cfg.bandwidth, nullptr);
    if (status != 0) {
        std::cerr << "[SdrCapture] Warning: could not set bandwidth: "
                  << bladerf_strerror(status) << "\n";
    }

    status = bladerf_set_gain_mode(dev_, BLADERF_CHANNEL_RX(0), BLADERF_GAIN_DEFAULT);
    if (status != 0) {
        std::cerr << "[SdrCapture] Warning: could not set gain mode: "
                  << bladerf_strerror(status) << "\n";
    }

    status = bladerf_sync_config(dev_, BLADERF_RX_X1, BLADERF_FORMAT_SC16_Q11,
                                  cfg.num_buffers, cfg.buffer_size,
                                  cfg.num_transfers, cfg.timeout_ms);
    if (status != 0) {
        std::cerr << "[SdrCapture] Failed to configure sync: "
                  << bladerf_strerror(status) << "\n";
        return false;
    }

    status = bladerf_enable_module(dev_, BLADERF_CHANNEL_RX(0), true);
    if (status != 0) {
        std::cerr << "[SdrCapture] Failed to enable RX: "
                  << bladerf_strerror(status) << "\n";
        return false;
    }

    live_ = true;
    std::cout << "[SdrCapture] BladeRF live at " << cfg.frequency_hz / 1e6
              << " MHz, " << cfg.sample_rate / 1e6 << " MSPS\n";
    return true;
}

size_t SdrCapture::readSamples(IQSample* buf, size_t max_samples) {
    if (live_) {
        int status = bladerf_sync_rx(dev_, buf, max_samples, nullptr, 3500);
        if (status != 0) {
            std::cerr << "[SdrCapture] RX error: "
                      << bladerf_strerror(status) << "\n";
            return 0;
        }
        return max_samples;
    }

    if (format_ == IQFormat::UC8) {
        std::vector<uint8_t> raw(max_samples * 2);
        file_.read(reinterpret_cast<char*>(raw.data()), max_samples * 2);
        size_t n = file_.gcount() / 2;
        for (size_t i = 0; i < n; i++) {
            int16_t I = (static_cast<int16_t>(raw[i * 2])     - 127) * 256;
            int16_t Q = (static_cast<int16_t>(raw[i * 2 + 1]) - 127) * 256;
            buf[i] = IQSample(I, Q);
        }
        return n;
    }

    file_.read(reinterpret_cast<char*>(buf), max_samples * sizeof(IQSample));
    return file_.gcount() / sizeof(IQSample);
}

void SdrCapture::stop() {
    if (file_.is_open()) file_.close();
    if (dev_) {
        bladerf_enable_module(dev_, BLADERF_CHANNEL_RX(0), false);
        bladerf_close(dev_);
        dev_ = nullptr;
    }
    live_ = false;
}