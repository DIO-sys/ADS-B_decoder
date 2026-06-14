#include "aircraft.h"
#include <algorithm>
#include <cstring>
#include <cmath>

static const char CALLSIGN_CHARS[] =
    "#ABCDEFGHIJKLMNOPQRSTUVWXYZ#####_###############0123456789######";

// Extract 'len' bits starting at bit position 'start' from payload
// Bit 0 is the MSB of payload[0]
static uint32_t getBits(const uint8_t* payload, int start, int len) {
    uint32_t result = 0;
    for (int i = start; i < start + len; i++) {
        result = (result << 1) | ((payload[i / 8] >> (7 - (i % 8))) & 1);
    }
    return result;
}

// Take 6-bit character codes from payload and convert to callsign letters
void AircraftTracker::parseIdentification(const ModeSFrame& frame, AircraftState& ac) {
    const uint8_t* p = frame.payload.data();

    ac.callsign.clear();
    for (int i = 0; i < 8; i++) {
        uint8_t c = getBits(p, 40 + i * 6, 6);
        if (c > 0 && c < sizeof(CALLSIGN_CHARS)) {
            char ch = CALLSIGN_CHARS[c];
            if (ch != '#') ac.callsign += ch;
        }
    }
}

// Extract 12-bit altitude field and decode to feet
int32_t AircraftTracker::decodeAltitude(const uint8_t* p) {
    uint32_t alt_bits = getBits(p, 40, 12);
    bool q_bit = (alt_bits >> 4) & 1;

    if (q_bit) {
        int n = ((alt_bits >> 5) << 4) | (alt_bits & 0x0F);
        return n * 25 - 1000;
    }
    return 0;
}

// Parse airborne position: altitude, even/odd flag, CPR encoded lat/lon
void AircraftTracker::parseAirbornePosition(const ModeSFrame& frame, AircraftState& ac, int64_t ts) {
    const uint8_t* p = frame.payload.data();

    ac.altitude_ft = decodeAltitude(p);

    bool odd         = getBits(p, 53, 1);
    uint32_t raw_lat = getBits(p, 54, 17);
    uint32_t raw_lon = getBits(p, 71, 17);

    if (odd) {
        ac.odd_lat = raw_lat;
        ac.odd_lon = raw_lon;
        ac.has_odd = true;
        ac.odd_ts = ts;
    } else {
        ac.even_lat = raw_lat;
        ac.even_lon = raw_lon;
        ac.has_even = true;
        ac.even_ts = ts;
    }

    // Attempt global CPR decode if we have both frames within 10 seconds
    if (ac.has_even && ac.has_odd && std::abs(ac.even_ts - ac.odd_ts) < 10000) {
        bool most_recent_odd = ac.odd_ts > ac.even_ts;
        auto pos = CprDecoder::decodeGlobal(
            ac.even_lat, ac.even_lon,
            ac.odd_lat, ac.odd_lon,
            most_recent_odd
        );
        if (pos.valid) {
            ac.latitude = pos.latitude;
            ac.longitude = pos.longitude;
            ac.position_valid = true;
        }
    }
}

// Parse velocity: east/west + north/south components into speed, heading, vertical rate
void AircraftTracker::parseAirborneVelocity(const ModeSFrame& frame, AircraftState& ac) {
    const uint8_t* p = frame.payload.data();
    uint8_t subtype = getBits(p, 37, 3);

    if (subtype == 1 || subtype == 2) {
        bool ew_sign   = getBits(p, 45, 1);
        int16_t ew_vel = getBits(p, 46, 10);
        ew_vel = ew_sign ? -(ew_vel - 1) : (ew_vel - 1);

        bool ns_sign   = getBits(p, 56, 1);
        int16_t ns_vel = getBits(p, 57, 10);
        ns_vel = ns_sign ? -(ns_vel - 1) : (ns_vel - 1);

        ac.ground_speed = std::sqrt(ew_vel * ew_vel + ns_vel * ns_vel);
        ac.heading = std::atan2(ew_vel, ns_vel) * 180.0 / M_PI;
        if (ac.heading < 0) ac.heading += 360.0;

        bool vr_sign   = getBits(p, 68, 1);
        int16_t vr     = getBits(p, 69, 9);
        ac.vertical_rate = (vr_sign ? -(vr - 1) : (vr - 1)) * 64.0f;
    }
}

// Route incoming DF17 frame to the right parser based on type code
void AircraftTracker::update(const ModeSFrame& frame, int64_t timestamp_ms) {
    if (!frame.crc_valid || frame.downlink_format != 17) return;

    auto& ac = aircraft_[frame.icao_address];
    ac.icao_address = frame.icao_address;
    ac.signal_power = frame.signal_power;
    ac.last_seen_ms = timestamp_ms;

    uint8_t type_code = getBits(frame.payload.data(), 32, 5);

    if (type_code >= 1 && type_code <= 4) {
        parseIdentification(frame, ac);
    } else if (type_code >= 9 && type_code <= 18) {
        parseAirbornePosition(frame, ac, timestamp_ms);
    } else if (type_code == 19) {
        parseAirborneVelocity(frame, ac);
    }
}

// Lookup aircraft by ICAO address
const AircraftState* AircraftTracker::getAircraft(uint32_t icao) const {
    auto it = aircraft_.find(icao);
    return (it != aircraft_.end()) ? &it->second : nullptr;
}

// Remove aircraft not seen within timeout period
void AircraftTracker::expireStale(int64_t now_ms, int64_t timeout_ms) {
    for (auto it = aircraft_.begin(); it != aircraft_.end();) {
        if (now_ms - it->second.last_seen_ms > timeout_ms) {
            it = aircraft_.erase(it);
        } else {
            ++it;
        }
    }
}

// Return pointers to all currently tracked aircraft
std::vector<const AircraftState*> AircraftTracker::getAllActive() const {
    std::vector<const AircraftState*> result;
    result.reserve(aircraft_.size());
    for (auto& [_, ac] : aircraft_) {
        result.push_back(&ac);
    }
    return result;
}