// Over-the-air packet sent from the outdoor station to the base station.
//
// The packet is serialized field by field as little-endian bytes. It is never
// memcpy'd as a struct, so the layout can't drift between compilers, boards
// or future versions.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wx {

constexpr uint8_t kPacketMagic = 0xA7;
constexpr uint8_t kPacketVersion = 1;
constexpr size_t kPacketSize = 35;

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
  uint32_t pressure_pa = 0;
  uint16_t wind_avg_mps_x100 = 0;
  uint16_t wind_gust_mps_x100 = 0;
  uint16_t wind_dir_deg = 0;   // 0..359, direction the wind comes FROM
  uint32_t rain_tips = 0;      // running count of bucket tips since boot
  uint16_t battery_mv = 0;
};

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
uint16_t crc16_ccitt(const uint8_t* data, size_t len);

// Writes exactly kPacketSize bytes. Returns kPacketSize, or 0 if cap is too small.
size_t encode(const OutdoorReading& r, uint8_t* out, size_t cap);

// Returns false (and leaves *out untouched) on wrong size, magic, version or CRC.
bool decode(const uint8_t* in, size_t len, OutdoorReading* out);

}  // namespace wx
