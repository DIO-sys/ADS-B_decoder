# Architecture

This document covers the design decisions, tradeoffs, and implementation details for each subsystem in the ADS-B decoder. The pipeline processes raw IQ samples through six stages before delivering verified aircraft state to consumers.

---

## Signal Acquisition

The BladeRF 2.0 Micro xA4 is configured to 1090 MHz at 2 MSPS in SC16Q11 format using signed 16-bit I and Q interleaved, with 11 fractional bits. This is the minimum sample rate for Mode S: the 1 Mbps pulse position modulation requires exactly 2 samples per bit period to distinguish early from late pulse energy.

Configuration is loaded from `config/bladerf.conf` at startup. All BladeRF parameters — frequency, sample rate, bandwidth, gain, buffer geometry, timeout — are externalized so the same binary works across different RF environments without recompilation. The `SdrCapture` class abstracts hardware from file: the rest of the pipeline calls `readSamples()` and never knows whether the data comes from a live antenna or a recording. This abstraction enabled the entire decoder to be developed and validated against recorded IQ files before any antenna was connected.

UC8 format support (unsigned 8-bit IQ pairs used by RTL-SDR and dump1090 test files) is handled by converting to the internal int16 representation at read time: `(raw - 127) * 256`. The scaling factor preserves dynamic range without introducing quantization artifacts in the magnitude calculation.

---

## Preamble Detection

Mode S transmissions begin with a fixed 8-microsecond preamble: four 0.5-microsecond pulses at positions 0, 2, 7, and 9 (in sample indices at 2 MSPS), with silence in between. At 2 MSPS this maps to 16 samples with known high and low positions.

### Why Correlation Instead of Threshold

A simple magnitude threshold — "trigger when the signal exceeds X" — fails in the presence of noise and multipath. A strong noise spike looks identical to a pulse. Correlation against the known pattern gives a confidence measure: the ratio of energy at expected pulse positions to energy at expected quiet positions. Random noise has equal energy everywhere and scores near 1.0. A real preamble has concentrated energy at the four pulse positions and near-zero energy between them, scoring 3.0 or higher.

### Quick-Reject Filtering

Before computing the full correlation, four cheap comparisons eliminate the vast majority of noise positions:

```cpp
if (mag[i] < mag[i+1]) continue;     // pulse 0 must exceed gap
if (mag[i+2] < mag[i+3]) continue;   // pulse 1 must exceed gap
if (mag[i+7] < mag[i+6]) continue;   // pulse 2 must exceed gap
if (mag[i+9] < mag[i+10]) continue;  // pulse 3 must exceed gap
```

Each check costs one comparison. If any fails, the position is rejected without computing magnitudes for all 16 samples. In practice this eliminates over 95% of candidate positions before any floating-point math occurs.

### Threshold Selection

The threshold of 3.0 was chosen empirically against the dump1090 `modes1.bin` test file. At 1.5, the detector triggered on noise at nearly the same rate as real signals, producing a uniform DF distribution across all 32 downlink formats which is lowkey the signature of random data being decoded. At 3.0, the noise rate dropped significantly while retaining all real signals. The CRC-24 check downstream serves as the ultimate gatekeeper: even if a false preamble passes the threshold, the decoded payload will fail CRC with overwhelming probability (1 in 16.7 million chance of a random 24-bit match).

After a valid preamble is found, the detector skips forward by 240 samples (16 preamble + 224 payload) to avoid re-triggering on the same frame.

---

## Mode S Frame Decoding

### Bit Extraction

Each of the 112 payload bits occupies 1 microsecond = 2 samples. Mode S uses pulse position modulation: a `1` bit has its energy in the first half-microsecond (early sample), a `0` bit has its energy in the second half (late sample). Bit extraction compares the magnitude of the early sample against the late sample:

```cpp
float early = iq_magnitude(samples[bit * 2]);
float late  = iq_magnitude(samples[bit * 2 + 1]);
if (early > late) payload[byte_index] |= (1 << bit_position);
```

Bits are packed MSB-first into a 14-byte array, matching the ICAO specification's bit numbering convention.

A generic bit extraction helper `getBits(payload, start, len)` replaces manual shift-and-mask operations throughout the parsing code. Bit positions map directly to the ICAO spec tables: `getBits(p, 54, 17)` reads "17 bits starting at bit 54" that's the CPR encoded latitude, just as documented in Doc 9871.

### CRC-24

Every Mode S frame carries a 24-bit CRC computed with the ICAO generator polynomial `x^24 + x^23 + x^10 + x^3 + 1` (0x1FFF409). The CRC is computed over the first 88 bits (11 bytes) of the payload and compared against the last 24 bits (3 bytes).

The implementation uses the standard byte-oriented approach:

```cpp
for (size_t i = 0; i < bytes; i++) {
    crc ^= static_cast<uint32_t>(data[i]) << 16;
    for (int j = 0; j < 8; j++) {
        crc <<= 1;
        if (crc & 0x1000000) crc ^= 0x1FFF409;
    }
}
```

CRC validation occurs before any payload field is parsed. If CRC fails, the frame is discarded without further processing. For DF17 (ADS-B), the CRC covers the data directly. For DF11 (all-call reply), the ICAO address is XORed into the CRC remainder, so validation requires a different approach in that case the decoder extracts the address from the XOR of computed and received CRC values.

The initial bit-by-bit CRC implementation produced incorrect results due to an XOR timing error — the generator polynomial was applied simultaneously with the data bit insertion rather than after the shift. This was identified by testing against a known-good message from the dump1090 test suite (`8D479E84580FD03D66D139C1CD17`). Switching to the byte-oriented approach resolved the issue. The lesson: always validate cryptographic and integrity functions with known test vectors before trusting downstream results.

### Downlink Format Parsing

Only DF17 (extended squitter, ADS-B) and DF11 (all-call reply) are processed. DF17 messages are routed by type code:

| Type Code | Content              | Extracted Fields                                |
|-----------|----------------------|-------------------------------------------------|
| 1-4       | Aircraft ID          | 8-character callsign from 6-bit encoded chars   |
| 9-18      | Airborne Position    | Altitude (12-bit Q-encoded), CPR lat/lon (17-bit each), even/odd flag |
| 19        | Airborne Velocity    | East/west and north/south speed components, vertical rate |

---

## Compact Position Reporting (CPR)

CPR is the most technically interesting component of the decoder. The problem: transmitting a full latitude and longitude at reasonable precision would require far more bits than the 17 available in each coordinate field. ICAO's solution encodes position across alternating even and odd frames using different zone grids.

### How It Works

The earth's latitude is divided into zones — 60 for even frames, 59 for odd frames. Each frame transmits the aircraft's position as a fractional offset within its zone grid, encoded as a 17-bit integer (0 to 131,071). Because the two grids have different zone counts, there is exactly one latitude band where both encoded values are consistent. This resolves the ambiguity without either frame needing to carry full coordinates.

Longitude resolution depends on latitude through the NL (Number of Longitude zones) function, which accounts for meridian convergence toward the poles. At the equator there are 59 longitude zones; near the poles there is 1. The NL function uses a trigonometric formula from the ICAO spec to compute the zone count for any latitude.

### Global

Global decode requires both an even and odd frame from the same aircraft, received within 10 seconds. It resolves position unambiguously anywhere on earth with no prior knowledge of the aircraft's location.

### NL Consistency Check

Before computing longitude, the decoder verifies that the even and odd latitudes produce the same NL value. If they don't which can happen when the aircraft crosses a latitude zone boundary between frames suprisingly the pair is rejected and the decoder waits for a fresh pair. This prevents erroneous position jumps at zone transitions.

### Validated Result

Aircraft 4D2023 (AMC421) decoded to 36.9689 N, 13.8517 E — over the Mediterranean Sea south of Sicily. The position, altitude (20,250 ft), heading (158 deg), and speed (372 kt) are consistent with a southbound commercial route from Europe to North Africa.

---

## Aircraft State Management

The `AircraftTracker` maintains a hash map keyed by 24-bit ICAO address. Each entry accumulates state across multiple messages:

- Identification messages set the callsign
- Position messages update altitude and feed the CPR resolver
- Velocity messages update ground speed, heading, and vertical rate
- Every valid message updates the last-seen timestamp and signal power

Aircraft that have not been seen for 60 seconds are expired from the tracker. This prevents stale entries from accumulating during long captures or live operation.

The tracker only processes CRC-valid DF17 messages. DF11 messages are accepted for ICAO address discovery but do not update aircraft state, since they carry no position, velocity, or identification data.

---

## Protobuf Serialization

The `AircraftRecord` protobuf message carries all decoded fields. Protocol Buffers were chosen over raw JSON for three reasons. First, binary efficiency because a serialized AircraftRecord is typically 30-50 bytes vs 200+ bytes for equivalent JSON. At high message rates this matters for TCP throughput. Second, schema evolution so that fields can be added without breaking existing clients. Third, cross-language support — the same `.proto` file generates both the C++ server serializer and the Python client deserializer, guaranteeing wire compatibility.

Messages are framed with a 4-byte big-endian length prefix. The receiver reads 4 bytes to learn the payload size, then reads exactly that many bytes and deserializes. This solves TCP's stream-oriented nature and without framing, message boundaries would be lost in the byte stream.

---

## TCP Streaming Server

The server uses POSIX sockets with a background accept thread. The main decode loop calls `broadcast()` after each aircraft state update, which serializes the record and sends it to every connected client.

### Threading Model

The accept loop runs in a dedicated thread, blocking on `select()` with a 1-second timeout. This allows clean shutdown: when `stop()` sets the `running_` flag to false, the thread exits within 1 second rather than hanging in a blocking `accept()` call.

The client list is protected by a mutex shared between the accept thread (which adds clients) and the main thread (which broadcasts to them). The mutex is held briefly — only during the list modification or iteration, not during I/O.

### Client Disconnect Handling

During broadcast, if `send()` fails on any client, that client is removed from the list and its socket is closed. The iteration runs in reverse to allow safe removal during traversal. `MSG_NOSIGNAL` prevents SIGPIPE from killing the process when writing to a disconnected socket.

### Why Not Epoll

For the expected client count (1-5 clients: web map, Python logger, occasional debug tools), `select()` and linear iteration are simpler and sufficient. Epoll would add complexity with no measurable benefit below hundreds of concurrent connections.

---

## WebSocket Bridge

Browsers cannot open raw TCP sockets. The Node.js bridge connects to the TCP server as a client, deserializes each length-prefixed protobuf message using the same `.proto` schema, converts to JSON, and forwards to all connected WebSocket clients. It auto-reconnects to the TCP server on disconnection.

The bridge is intentionally thin — no buffering, no transformation, no state. It exists solely to cross the TCP-to-WebSocket boundary. In a production system this could be replaced by a WebSocket endpoint built directly into the C++ server, but the Node.js approach was faster to implement and easier to modify during development.

---

## Leaflet Map Client

The browser client connects to the WebSocket bridge and maintains an in-memory aircraft table. Each incoming message creates or updates a marker on the Leaflet map with an SVG aircraft icon rotated to match heading, and a persistent tooltip showing callsign, altitude, and speed.

Aircraft markers expire after 60 seconds without updates. The map auto-centers on the data — with one aircraft it zooms to its position, with many it adjusts to show all.

### Demo Mode

The decoder supports a `--demo` flag that bypasses the SDR pipeline entirely and broadcasts 12 synthetic aircraft moving along realistic trajectories around Yaounde. This enables full-stack testing of the TCP server, WebSocket bridge, and Leaflet client without requiring hardware or a signal environment. Demo aircraft have real airline callsigns, plausible altitudes and speeds, and move continuously to verify position update rendering.

---

## Python Analysis Client

The Python logger demonstrates dual-client consumption of the same TCP stream. While the Leaflet map shows real-time state, the logger persists every record to SQLite for offline analysis. The analysis script generates four plots:

- Message rate over time — shows traffic density and capture stability
- Altitude distribution — histogram of observed flight levels
- Unique aircraft count — cumulative curve showing discovery rate
- Signal power distribution — characterizes the RF environment and receiver sensitivity

Both the logger and the Leaflet client connect to the same TCP server simultaneously, each receiving identical data. This validates the multi-client broadcast architecture.
