#include "sdr.h"
#include "preamble.h"
#include "decoder.h"
#include "aircraft.h"
#include "server.h"
#include "adsb.pb.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <csignal>
#include <cstdio>
#include <chrono>

static volatile bool g_running = true;
void sigHandler(int) { g_running = false; }

int main(int argc, char* argv[]) {
    signal(SIGINT, sigHandler);
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    SdrCapture sdr;
    uint16_t port = 30003;

    if (argc > 1 && std::strcmp(argv[1], "--live") == 0) {
        std::string cfg_path = (argc > 2) ? argv[2] : "../config/bladerf.conf";
        SdrConfig cfg = SdrCapture::loadConfig(cfg_path);
        if (!sdr.initLive(cfg)) return 1;
    } else {
        std::string path = (argc > 1) ? argv[1] : "samples/modes1.bin";
        IQFormat fmt = IQFormat::SC16Q11;
        if (argc > 2 && std::strcmp(argv[2], "--uc8") == 0) {
            fmt = IQFormat::UC8;
        }
        if (!sdr.initFromFile(path, fmt)) return 1;
    }

    PreambleDetector preamble;
    preamble.setThreshold(3.0f);

    ModeSDecoder decoder;
    AircraftTracker tracker;

    TcpServer server;
    if (!server.listen(port)) {
        std::cerr << "Failed to start TCP server\n";
        return 1;
    }

    constexpr size_t CHUNK = 2 * 1024 * 1024;
    std::vector<IQSample> buf(CHUNK);
    size_t total_frames = 0, valid_frames = 0;

    auto start_time = std::chrono::steady_clock::now();

    while (g_running) {
        size_t n = sdr.readSamples(buf.data(), CHUNK);
        if (n == 0) break;

        auto now = std::chrono::steady_clock::now();
        int64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_time).count();

        auto offsets = preamble.detect(buf.data(), n);

        for (size_t offset : offsets) {
            size_t frame_start = offset + 16;
            size_t remaining = n - frame_start;

            ModeSFrame frame;
            if (decoder.decode(&buf[frame_start], remaining, frame)) {
                total_frames++;
                if (frame.crc_valid) {
                    valid_frames++;
                    tracker.update(frame, ts);

                    // Broadcast updated aircraft to all TCP clients
                    if (frame.downlink_format == 17) {
                        auto* ac = tracker.getAircraft(frame.icao_address);
                        if (ac) {
                            server.broadcast(*ac, ts);
                        }
                    }
                }
            }
        }

        tracker.expireStale(ts);
    }

    // Print tracked aircraft
    auto aircraft = tracker.getAllActive();
    std::printf("\n--- Tracked Aircraft: %zu ---\n", aircraft.size());
    for (auto* ac : aircraft) {
        std::printf("  ICAO:%06X", ac->icao_address);
        if (!ac->callsign.empty()) {
            std::printf("  Callsign:%-8s", ac->callsign.c_str());
        }
        if (ac->position_valid) {
            std::printf("  Lat:%8.4f  Lon:%9.4f", ac->latitude, ac->longitude);
        }
        if (ac->altitude_ft != 0) {
            std::printf("  Alt:%5dft", ac->altitude_ft);
        }
        if (ac->ground_speed > 0) {
            std::printf("  Spd:%.0fkt  Hdg:%.0f", ac->ground_speed, ac->heading);
        }
        std::printf("  Pwr:%.1f dBFS\n", ac->signal_power);
    }

    std::printf("\nTotal frames: %zu  Valid: %zu  CRC pass rate: %.1f%%\n",
        total_frames, valid_frames,
        total_frames > 0 ? 100.0 * valid_frames / total_frames : 0.0);
    std::printf("Clients connected: %d\n", server.clientCount());

    server.stop();
    sdr.stop();
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}