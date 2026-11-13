// Over-the-air packet sent from the outdoor station to the base station.
//
// The packet is serialized field by field as little-endian bytes. It is never
// memcpy'd as a struct, so the layout can't drift between compilers, boards
// or future versions.
//
// The outdoor station sends RAW measurements (pulse counts, uncalibrated vane
// angle). All calibration lives in the base station's settings, so it can be
// changed from the /config page without touching the outdoor unit.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wx {

constexpr uint8_t kPacketMagic = 0xA7;
constexpr uint8_t kPacketVersion = 1;
constexpr size_t kPacketSize = 41;

constexpr uint16_t kNoWindDir = 0xFFFF;

// Why the sender last rebooted. Reported so the base station can show it.
enum class ResetReason : uint8_t {
  kUnknown = 0,
  kPowerOn = 1,
  kWatchdog = 2,
};

// Bits in OutdoorReading::valid. If a sensor fails during a cycle, its bit is
// cleared instead of sending a stale or garbage value.
enum ValidBits : uint8_t {
  kValidTempHumidity = 1u << 0,
  kValidPressure = 1u << 1,
  kValidWind = 1u << 2,
  kValidWindDir = 1u << 3,
  kValidBattery = 1u << 4,
  kValidSolar = 1u << 5,
};

struct OutdoorReading {
  uint8_t station_id = 0;
  uint16_t boot_id = 0;   // random each boot; a change means the outdoor unit restarted
  uint16_t seq = 0;       // +1 every packet, wraps around
  uint32_t uptime_s = 0;
  ResetReason reset_reason = ResetReason::kUnknown;
  uint8_t valid = 0;      // ValidBits
  int16_t temp_c_x100 = 0;
  uint16_t humidity_x100 = 0;  // %RH * 100
  uint32_t pressure_pa = 0;    // station pressure (not sea-level corrected)
  uint16_t wind_pulses = 0;        // anemometer pulses during the interval
  uint16_t wind_interval_ms = 0;   // length of that interval
  uint16_t gust_pulses_3s = 0;     // most pulses in any 3-second window of the interval
  uint16_t wind_dir_raw_deg = kNoWindDir;  // vector-averaged vane angle, 0..359, before north offset
  uint32_t rain_tips = 0;      // running count of bucket tips since boot
  uint16_t battery_mv = 0;
  uint16_t solar_mv = 0;
  uint8_t max_loop_ms = 0;     // slowest main-loop pass since last packet (health)
  uint8_t sensor_errors = 0;   // I2C/sensor errors since last packet, saturating (health)
};

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
uint16_t crc16_ccitt(const uint8_t* data, size_t len);

// Writes exactly kPacketSize bytes. Returns kPacketSize, or 0 if cap is too small.
size_t encode(const OutdoorReading& r, uint8_t* out, size_t cap);

// Returns false (and leaves *out untouched) on wrong size, magic, version or CRC.
bool decode(const uint8_t* in, size_t len, OutdoorReading* out);

}  // namespace wx
