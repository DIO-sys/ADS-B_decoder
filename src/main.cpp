#include "sdr.h"
#include "preamble.h"
#include "decoder.h"
#include "adsb.pb.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <csignal>
#include <cstdio>

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
    // CRC test with known-good frame from dump1090 README
    // *8d479e84580fd03d66d139c1cd17;
    uint8_t test_msg[] = {0x8D, 0x47, 0x9E, 0x84, 0x58, 0x0F, 0xD0, 0x3D,
                          0x66, 0xD1, 0x39, 0xC1, 0xCD, 0x17};
    uint32_t test_crc = decoder.crc24(test_msg, 88);
    uint32_t test_recv = (test_msg[11] << 16) | (test_msg[12] << 8) | test_msg[13];
    std::printf("[CRC TEST] computed=%06X received=%06X match=%s\n",
        test_crc, test_recv, test_crc == test_recv ? "YES" : "NO");

    constexpr size_t CHUNK = 2 * 1024 * 1024;
    std::vector<IQSample> buf(CHUNK);
    size_t total_frames = 0, valid_frames = 0;
    size_t df_counts[32] = {};

    while (g_running) {
        size_t n = sdr.readSamples(buf.data(), CHUNK);
        if (n == 0) break;

        auto offsets = preamble.detect(buf.data(), n);

        for (size_t offset : offsets) {
            size_t frame_start = offset + 16;
            size_t remaining = n - frame_start;

            ModeSFrame frame;
            if (decoder.decode(&buf[frame_start], remaining, frame)) {
                total_frames++;
                df_counts[frame.downlink_format & 0x1F]++;

                if (frame.crc_valid) {
                    valid_frames++;
                }

                if (frame.downlink_format == 17) {
                    std::printf("DF17 ICAO:%06X CRC:%s Power:%.1f dBFS  HEX:",
                        frame.icao_address,
                        frame.crc_valid ? "OK  " : "FAIL",
                        frame.signal_power);
                    for (int b = 0; b < 14; b++) {
                        std::printf("%02X", frame.payload[b]);
                    }
                    std::printf("\n");
                } else if (frame.crc_valid) {
                    std::printf("DF%02d ICAO:%06X CRC:OK   Power:%.1f dBFS\n",
                        frame.downlink_format, frame.icao_address, frame.signal_power);
                }
            }
        }
    }

    std::printf("\n--- Summary ---\n");
    std::printf("Total frames: %zu  Valid: %zu  CRC pass rate: %.1f%%\n",
        total_frames, valid_frames,
        total_frames > 0 ? 100.0 * valid_frames / total_frames : 0.0);
    std::printf("\nDF distribution:\n");
    for (int i = 0; i < 32; i++) {
        if (df_counts[i] > 0) {
            std::printf("  DF%02d: %zu\n", i, df_counts[i]);
        }
    }

    sdr.stop();
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}