#include "sdr.h"
#include "adsb.pb.h"
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    std::string iq_file = (argc > 1) ? argv[1] : "samples/iq_1090.bin";

    SdrCapture sdr;
    if (!sdr.initFromFile(iq_file)) return 1;

    // Read one chunk to prove the pipeline works
    std::vector<IQSample> buf(16384);
    size_t n = sdr.readSamples(buf.data(), buf.size());
    std::cout << "Read " << n << " IQ samples\n";

    // Prove protobuf links
    adsb::AircraftRecord record;
    record.set_icao_address(0xABCDEF);
    record.set_callsign("TEST1234");
    std::string serialized;
    record.SerializeToString(&serialized);
    std::cout << "Protobuf serialized: " << serialized.size() << " bytes\n";

    sdr.stop();
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}