#pragma once
#include <cstdint>

struct CprPosition {
    double latitude;
    double longitude;
    bool   valid;
};

class CprDecoder {
public:
    static CprPosition decodeGlobal(
        uint32_t even_lat, uint32_t even_lon,
        uint32_t odd_lat,  uint32_t odd_lon,
        bool most_recent_is_odd
    );

private:
    static constexpr double NZ = 15.0;
    static constexpr double D_LAT_EVEN = 360.0 / (4.0 * NZ);
    static constexpr double D_LAT_ODD  = 360.0 / (4.0 * NZ - 1.0);

    static int cprNL(double lat);
    static double cprMod(double a, double b);
};