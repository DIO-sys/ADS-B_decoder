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
#include <thread>
#include <cmath>

static volatile bool g_running = true;
void sigHandler(int) { g_running = false; }

void runDemo(TcpServer& server) {
    struct DemoAircraft {
        uint32_t icao;
        const char* callsign;
        double lat, lon;
        int32_t alt;
        float speed, heading, vrate;
        double dlat, dlon; // movement per tick
    };

    DemoAircraft demos[] = {
        {0x076001, "CAM101", 3.8480, 11.5021, 35000, 450, 45, 0, 0.002, 0.002},
        {0x076002, "CAM202", 4.0060, 9.7000, 28000, 380, 270, -500, 0.0, -0.003},
        {0x076003, "AWA315", 3.7000, 11.0000, 12000, 250, 180, -1200, -0.002, 0.0},
        {0x076004, "AFR882", 4.2000, 11.8000, 38000, 480, 135, 0, -0.001, 0.002},
        {0x076005, "ETH504", 3.5000, 10.5000, 22000, 320, 90, 800, 0.0, 0.003},
        {0x076006, "KQA440", 4.1000, 12.0000, 31000, 420, 315, 0, 0.002, -0.002},
        {0x076007, "RAM761", 3.9500, 11.5500, 5000, 180, 200, -800, -0.001, -0.001},
        {0x076008, "SWR192", 3.6000, 11.2000, 41000, 490, 10, 0, 0.003, 0.001},
        {0x076009, "DLH583", 4.3000, 11.3000, 15000, 280, 160, -600, -0.002, 0.001},
        {0x07600A, "BAW217", 3.8000, 10.8000, 33000, 440, 60, 200, 0.001, 0.002},
        {0x07600B, "THY609", 4.0500, 11.6000, 8000, 200, 240, -1000, -0.001, -0.002},
        {0x07600C, "MSR843", 3.7500, 12.2000, 36000, 460, 350, 0, 0.003, -0.001},
    };

    int num = sizeof(demos) / sizeof(demos[0]);
    int64_t tick = 0;

    std::printf("[Demo] Broadcasting %d synthetic aircraft\n", num);

    while (g_running) {
        for (int i = 0; i < num; i++) {
            auto& d = demos[i];

            // Move aircraft
            d.lat += d.dlat;
            d.lon += d.dlon;
            d.alt += static_cast<int32_t>(d.vrate / 60.0f);

            AircraftState ac;
            ac.icao_address = d.icao;
            ac.callsign = d.callsign;
            ac.latitude = d.lat;
            ac.longitude = d.lon;
            ac.altitude_ft = d.alt;
            ac.ground_speed = d.speed;
            ac.heading = d.heading;
            ac.vertical_rate = d.vrate;
            ac.signal_power = -10.0f;
            ac.position_valid = true;

            server.broadcast(ac, tick * 1000);
        }

        tick++;
        if (server.clientCount() > 0) {
            std::printf("[Demo] Tick %lld — %d clients\n",
                (long long)tick, server.clientCount());
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, sigHandler);
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    uint16_t port = 30003;
    bool demo_mode = false;

    if (argc > 1 && std::strcmp(argv[1], "--demo") == 0) {
        demo_mode = true;
    }

    TcpServer server;
    if (!server.listen(port)) {
        std::cerr << "Failed to start TCP server\n";
        return 1;
    }

    if (demo_mode) {
        runDemo(server);
        server.stop();
        google::protobuf::ShutdownProtobufLibrary();
        return 0;
    }

    // Normal mode: SDR capture
    SdrCapture sdr;

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

    auto aircraft = tracker.getAllActive();
    std::printf("\n--- Tracked Aircraft: %zu ---\n", aircraft.size());
    for (auto* ac : aircraft) {
        std::printf("  ICAO:%06X", ac->icao_address);
        if (!ac->callsign.empty())
            std::printf("  Callsign:%-8s", ac->callsign.c_str());
        if (ac->position_valid)
            std::printf("  Lat:%8.4f  Lon:%9.4f", ac->latitude, ac->longitude);
        if (ac->altitude_ft != 0)
            std::printf("  Alt:%5dft", ac->altitude_ft);
        if (ac->ground_speed > 0)
            std::printf("  Spd:%.0fkt  Hdg:%.0f", ac->ground_speed, ac->heading);
        std::printf("  Pwr:%.1f dBFS\n", ac->signal_power);
    }

    std::printf("\nTotal frames: %zu  Valid: %zu  CRC pass rate: %.1f%%\n",
        total_frames, valid_frames,
        total_frames > 0 ? 100.0 * valid_frames / total_frames : 0.0);

    server.stop();
    sdr.stop();
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}