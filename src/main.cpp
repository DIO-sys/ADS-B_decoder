#include "sdr.h"
#include "preamble.h"
#include "adsb.pb.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <csignal>

static volatile bool g_running = true;
void sigHandler(int) { g_running = false; }

int main(int argc, char* argv[]) {
    signal(SIGINT, sigHandler);
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    SdrCapture sdr;

    if (argc > 1 && std::strcmp(argv[1], "--live") == 0) {
        std::string cfg_path = (argc > 2) ? argv[2] : "../config/bladerf.conf";
        SdrConfig cfg = SdrCapture::loadConfig(cfg_path);
        if (!sdr.initLive(cfg)) return 1;
    } else {
        std::string path = (argc > 1) ? argv[1] : "samples/iq_1090.bin";
        if (!sdr.initFromFile(path)) return 1;
    }

    PreambleDetector preamble;
    preamble.setThreshold(1.5f);

    constexpr size_t CHUNK = 2 * 1024 * 1024;
    std::vector<IQSample> buf(CHUNK);
    size_t total_preambles = 0;

    while (g_running) {
        size_t n = sdr.readSamples(buf.data(), CHUNK);
        if (n == 0) break;

        auto offsets = preamble.detect(buf.data(), n);
        total_preambles += offsets.size();

        for (size_t off : offsets) {
            std::cout << "Preamble at sample " << off << "\n";
        }
    }

    std::cout << "\nTotal preambles detected: " << total_preambles << "\n";

    sdr.stop();
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}