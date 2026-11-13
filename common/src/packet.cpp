#include "wx/packet.h"

namespace wx {
namespace {

class Writer {
 public:
  explicit Writer(uint8_t* p) : p_(p) {}
  void u8(uint8_t v) { *p_++ = v; }
  void u16(uint16_t v) {
    u8(static_cast<uint8_t>(v));
    u8(static_cast<uint8_t>(v >> 8));
  }
  void u32(uint32_t v) {
    u16(static_cast<uint16_t>(v));
    u16(static_cast<uint16_t>(v >> 16));
  }

 private:
  uint8_t* p_;
};

class Reader {
 public:
  explicit Reader(const uint8_t* p) : p_(p) {}
  uint8_t u8() { return *p_++; }
  uint16_t u16() {
    uint16_t lo = u8();
    return static_cast<uint16_t>(lo | (static_cast<uint16_t>(u8()) << 8));
  }
  uint32_t u32() {
    uint32_t lo = u16();
    return lo | (static_cast<uint32_t>(u16()) << 16);
  }

 private:
  const uint8_t* p_;
};

}  // namespace

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

size_t encode(const OutdoorReading& r, uint8_t* out, size_t cap) {
  if (out == nullptr || cap < kPacketSize) return 0;
  Writer w(out);
  w.u8(kPacketMagic);
  w.u8(kPacketVersion);
  w.u8(r.station_id);
  w.u16(r.boot_id);
  w.u16(r.seq);
  w.u32(r.uptime_s);
  w.u8(static_cast<uint8_t>(r.reset_reason));
  w.u8(r.valid);
  w.u16(static_cast<uint16_t>(r.temp_c_x100));
  w.u16(r.humidity_x100);
  w.u32(r.pressure_pa);
  w.u16(r.wind_pulses);
  w.u16(r.wind_interval_ms);
  w.u16(r.gust_pulses_3s);
  w.u16(r.wind_dir_raw_deg);
  w.u32(r.rain_tips);
  w.u16(r.battery_mv);
  w.u16(r.solar_mv);
  w.u8(r.max_loop_ms);
  w.u8(r.sensor_errors);
  w.u16(crc16_ccitt(out, kPacketSize - 2));
  return kPacketSize;
}

bool decode(const uint8_t* in, size_t len, OutdoorReading* out) {
  if (in == nullptr || out == nullptr || len != kPacketSize) return false;
  if (in[0] != kPacketMagic || in[1] != kPacketVersion) return false;

  Reader rd(in);
  rd.u8();  // magic
  rd.u8();  // version
  OutdoorReading r;
  r.station_id = rd.u8();
  r.boot_id = rd.u16();
  r.seq = rd.u16();
  r.uptime_s = rd.u32();
  r.reset_reason = static_cast<ResetReason>(rd.u8());
  r.valid = rd.u8();
  r.temp_c_x100 = static_cast<int16_t>(rd.u16());
  r.humidity_x100 = rd.u16();
  r.pressure_pa = rd.u32();
  r.wind_pulses = rd.u16();
  r.wind_interval_ms = rd.u16();
  r.gust_pulses_3s = rd.u16();
  r.wind_dir_raw_deg = rd.u16();
  r.rain_tips = rd.u32();
  r.battery_mv = rd.u16();
  r.solar_mv = rd.u16();
  r.max_loop_ms = rd.u8();
  r.sensor_errors = rd.u8();
  const uint16_t sent_crc = rd.u16();
  if (sent_crc != crc16_ccitt(in, kPacketSize - 2)) return false;

  *out = r;
  return true;
}

}  // namespace wx
