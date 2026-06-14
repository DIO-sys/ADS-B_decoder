#include "cpr.h"
#include <cmath>
#include <algorithm>

static constexpr double CPR_MAX = 131072.0; // 2^17

double CprDecoder::cprMod(double a, double b) {
    double result = std::fmod(a, b);
    if (result < 0) result += b;
    return result;
}

int CprDecoder::cprNL(double lat) {
    if (std::abs(lat) >= 87.0) return 1;
    return static_cast<int>(std::floor(
        2.0 * M_PI / std::acos(
            1.0 - (1.0 - std::cos(M_PI / (2.0 * NZ))) /
            (std::cos(M_PI / 180.0 * std::abs(lat)) *
             std::cos(M_PI / 180.0 * std::abs(lat)))
        )
    ));
}

CprPosition CprDecoder::decodeGlobal(
    uint32_t even_lat, uint32_t even_lon,
    uint32_t odd_lat,  uint32_t odd_lon,
    bool most_recent_is_odd)
{
    double lat_even = even_lat / CPR_MAX;
    double lon_even = even_lon / CPR_MAX;
    double lat_odd  = odd_lat  / CPR_MAX;
    double lon_odd  = odd_lon  / CPR_MAX;

    double j = std::floor(59.0 * lat_even - 60.0 * lat_odd + 0.5);

    double lat_e = D_LAT_EVEN * (cprMod(j, 60.0) + lat_even);
    double lat_o = D_LAT_ODD  * (cprMod(j, 59.0) + lat_odd);

    if (lat_e >= 270.0) lat_e -= 360.0;
    if (lat_o >= 270.0) lat_o -= 360.0;

    if (cprNL(lat_e) != cprNL(lat_o)) {
        return {0, 0, false};
    }

    double lat, lon;
    if (most_recent_is_odd) {
        lat = lat_o;
        int nl = cprNL(lat_o);
        int ni = std::max(nl - 1, 1);
        double dlon = 360.0 / ni;
        double m = std::floor(lon_even * (nl - 1) - lon_odd * nl + 0.5);
        lon = dlon * (cprMod(m, ni) + lon_odd);
    } else {
        lat = lat_e;
        int nl = cprNL(lat_e);
        int ni = std::max(nl, 1);
        double dlon = 360.0 / ni;
        double m = std::floor(lon_even * (nl - 1) - lon_odd * nl + 0.5);
        lon = dlon * (cprMod(m, ni) + lon_even);
    }

    if (lon >= 180.0) lon -= 360.0;

    return {lat, lon, true};
}

