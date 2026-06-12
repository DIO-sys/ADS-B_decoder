#include "sdr.h"
#include <iostream>

bool SdrCapture::initFromFile(const std::string& path) {
    file_.open(path, std::ios::binary);
    if (!file_.is_open()) {
        std::cerr << "[SdrCapture] Failed to open: " << path << "\n";
        return false;
    }
    std::cout << "[SdrCapture] Opened IQ file: " << path << "\n";
    return true;
}

size_t SdrCapture::readSamples(IQSample* buf, size_t max_samples) {
    file_.read(reinterpret_cast<char*>(buf), max_samples * sizeof(IQSample));
    return file_.gcount() / sizeof(IQSample);
}

void SdrCapture::stop() {
    if (file_.is_open()) file_.close();
}