#pragma once
#include "decoder.h"
#include "cpr.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

struct AircraftState {
    uint32_t icao_address = 0;
    std::string callsign;
    double   latitude  = 0.0;
    double   longitude = 0.0;
    int32_t  altitude_ft = 0;
    float    ground_speed = 0.0f;
    float    heading = 0.0f;
    float    vertical_rate = 0.0f;
    float    signal_power = 0.0f;
    bool     position_valid = false;

    uint32_t even_lat = 0, even_lon = 0;
    uint32_t odd_lat  = 0, odd_lon  = 0;
    bool     has_even = false, has_odd = false;
    int64_t  even_ts  = 0, odd_ts = 0;

    int64_t  last_seen_ms = 0;
};

class AircraftTracker {
public:
    void update(const ModeSFrame& frame, int64_t timestamp_ms);
    const AircraftState* getAircraft(uint32_t icao) const;
    void expireStale(int64_t now_ms, int64_t timeout_ms = 60000);
    std::vector<const AircraftState*> getAllActive() const;

private:
    std::unordered_map<uint32_t, AircraftState> aircraft_;

    void parseIdentification(const ModeSFrame& frame, AircraftState& ac);
    void parseAirbornePosition(const ModeSFrame& frame, AircraftState& ac, int64_t ts);
    void parseAirborneVelocity(const ModeSFrame& frame, AircraftState& ac);
    int32_t decodeAltitude(const uint8_t* payload);
};