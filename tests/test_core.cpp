// Packet format, calendar and rain ledger.
#include <cstring>

#include "test_util.h"
#include "wx/calendar.h"
#include "wx/packet.h"
#include "wx/rain_ledger.h"

namespace {

wx::OutdoorReading sample() {
  wx::OutdoorReading r;
  r.station_id = 1;
  r.boot_id = 0xBEEF;
  r.seq = 65535;
  r.uptime_s = 123456789;
  r.reset_reason = wx::ResetReason::kWatchdog;
  r.valid = wx::kValidTempHumidity | wx::kValidWind;
  r.temp_c_x100 = -1234;
  r.humidity_x100 = 9876;
  r.pressure_pa = 101325;
  r.wind_pulses = 512;
  r.wind_interval_ms = 30000;
  r.gust_pulses_3s = 99;
  r.wind_dir_raw_deg = 359;
  r.rain_tips = 0xDEADBEEF;
  r.battery_mv = 3312;
  r.solar_mv = 6020;
  r.max_loop_ms = 7;
  r.sensor_errors = 3;
  return r;
}

void test_crc_known_value() {
  const char* s = "123456789";  // standard check value for CRC-16/CCITT-FALSE
  CHECK_EQ(wx::crc16_ccitt(reinterpret_cast<const uint8_t*>(s), 9), 0x29B1);
}

void test_packet_round_trip() {
  uint8_t buf[64];
  const wx::OutdoorReading in = sample();
  CHECK_EQ(wx::encode(in, buf, sizeof(buf)), wx::kPacketSize);
  wx::OutdoorReading out;
  CHECK(wx::decode(buf, wx::kPacketSize, &out));
  CHECK_EQ(out.station_id, in.station_id);
  CHECK_EQ(out.boot_id, in.boot_id);
  CHECK_EQ(out.seq, in.seq);
  CHECK_EQ(out.uptime_s, in.uptime_s);
  CHECK(out.reset_reason == in.reset_reason);
  CHECK_EQ(out.valid, in.valid);
  CHECK_EQ(out.temp_c_x100, in.temp_c_x100);
  CHECK_EQ(out.humidity_x100, in.humidity_x100);
  CHECK_EQ(out.pressure_pa, in.pressure_pa);
  CHECK_EQ(out.wind_pulses, in.wind_pulses);
  CHECK_EQ(out.wind_interval_ms, in.wind_interval_ms);
  CHECK_EQ(out.gust_pulses_3s, in.gust_pulses_3s);
  CHECK_EQ(out.wind_dir_raw_deg, in.wind_dir_raw_deg);
  CHECK_EQ(out.rain_tips, in.rain_tips);
  CHECK_EQ(out.battery_mv, in.battery_mv);
  CHECK_EQ(out.solar_mv, in.solar_mv);
  CHECK_EQ(out.max_loop_ms, in.max_loop_ms);
  CHECK_EQ(out.sensor_errors, in.sensor_errors);
}

void test_packet_rejects_bad_input() {
  uint8_t buf[64];
  wx::encode(sample(), buf, sizeof(buf));
  wx::OutdoorReading out;
  CHECK(!wx::decode(buf, wx::kPacketSize - 1, &out));
  CHECK(!wx::decode(buf, wx::kPacketSize + 1, &out));
  CHECK_EQ(wx::encode(sample(), buf, wx::kPacketSize - 1), 0u);
  wx::encode(sample(), buf, sizeof(buf));
  for (size_t i = 0; i < wx::kPacketSize; ++i) {
    for (int b = 0; b < 8; ++b) {
      buf[i] ^= static_cast<uint8_t>(1u << b);
      CHECK(!wx::decode(buf, wx::kPacketSize, &out));
      buf[i] ^= static_cast<uint8_t>(1u << b);
    }
  }
  CHECK(wx::decode(buf, wx::kPacketSize, &out));
}

void test_calendar() {
  CHECK_EQ(wx::days_from_civil(1970, 1, 1), 0);
  CHECK_EQ(wx::days_from_civil(2000, 3, 1), 11017);
  for (int32_t d = -1000; d < 30000; ++d) {
    const wx::Date c = wx::civil_from_days(d);
    CHECK_EQ(wx::days_from_civil(c.year, c.month, c.day), d);
  }
  const wx::Date leap = wx::civil_from_days(wx::days_from_civil(2028, 2, 29));
  CHECK_EQ(leap.month, 2);
  CHECK_EQ(leap.day, 29);
}

void test_rain_basic_counting() {
  wx::RainLedger l;
  const int32_t day = wx::days_from_civil(2026, 11, 12);
  CHECK_EQ(l.on_reading(7, 10, day), 0u);
  CHECK_EQ(l.on_reading(7, 13, day), 3u);
  CHECK_EQ(l.on_reading(7, 13, day), 0u);
  CHECK_EQ(l.on_reading(7, 20, day), 7u);
  CHECK_EQ(l.today_tips(day), 10u);
}

void test_rain_outdoor_reboot() {
  wx::RainLedger l;
  const int32_t day = wx::days_from_civil(2026, 11, 12);
  l.on_reading(1, 100, day);
  l.on_reading(1, 105, day);
  CHECK_EQ(l.on_reading(2, 4, day), 4u);
  CHECK_EQ(l.on_reading(3, 500, day), 500u);
  CHECK_EQ(l.today_tips(day), 509u);
}

void test_rain_glitches() {
  wx::RainLedger l;
  const int32_t day = wx::days_from_civil(2026, 11, 12);
  l.on_reading(1, 100, day);
  CHECK_EQ(l.on_reading(1, 90, day), 0u);
  CHECK_EQ(l.on_reading(1, 92, day), 2u);
  CHECK_EQ(l.on_reading(1, 92 + wx::RainLedger::kMaxTipsPerReading + 1, day), 0u);
  CHECK_EQ(l.today_tips(day), 2u);
}

void test_rain_day_month_year_rollover() {
  wx::RainLedger l;
  const int32_t dec31 = wx::days_from_civil(2026, 12, 31);
  l.on_reading(1, 0, wx::days_from_civil(2026, 11, 30));
  l.on_reading(1, 5, wx::days_from_civil(2026, 11, 30));
  l.on_reading(1, 8, wx::days_from_civil(2026, 12, 1));
  l.on_reading(1, 10, dec31);
  CHECK_EQ(l.today_tips(dec31), 2u);
  CHECK_EQ(l.month_tips(dec31), 5u);
  CHECK_EQ(l.year_tips(dec31), 10u);
  const int32_t jan1 = dec31 + 1;
  l.on_reading(1, 11, jan1);
  CHECK_EQ(l.today_tips(jan1), 1u);
  CHECK_EQ(l.month_tips(jan1), 1u);
  CHECK_EQ(l.year_tips(jan1), 1u);
  CHECK_EQ(l.day_tips(dec31), 2u);
}

void test_rain_gap_clears_old_days() {
  wx::RainLedger l;
  const int32_t d0 = wx::days_from_civil(2026, 1, 1);
  l.on_reading(1, 0, d0);
  l.on_reading(1, 7, d0);
  const int32_t later = d0 + wx::RainLedger::kDays;
  l.on_reading(1, 7, later);
  CHECK_EQ(l.today_tips(later), 0u);
  CHECK_EQ(l.day_tips(d0), 0u);
  l.on_reading(1, 9, later + 3);
  CHECK_EQ(l.day_tips(later + 1), 0u);
  CHECK_EQ(l.day_tips(later + 3), 2u);
}

void test_rain_clock_going_backwards() {
  wx::RainLedger l;
  const int32_t day = wx::days_from_civil(2026, 6, 1);
  l.on_reading(1, 0, day);
  l.on_reading(1, 4, day);
  CHECK_EQ(l.on_reading(1, 6, day - 5), 2u);
  CHECK_EQ(l.today_tips(day), 6u);
}

void test_rain_save_and_load() {
  wx::RainLedger a;
  const int32_t day = wx::days_from_civil(2026, 8, 15);
  a.on_reading(9, 0, day);
  a.on_reading(9, 12, day);
  wx::RainLedger b;
  CHECK(b.load(a.state()));
  CHECK_EQ(b.today_tips(day), 12u);
  CHECK_EQ(b.on_reading(9, 20, day), 8u);
  wx::RainLedger::State bad = a.state();
  bad.version = 999;
  wx::RainLedger c;
  CHECK(!c.load(bad));
  CHECK_EQ(c.today_tips(day), 0u);
}

void test_rain_take_delta_then_credit() {
  wx::RainLedger l;
  const int32_t day = wx::days_from_civil(2026, 3, 3);
  CHECK(!l.has_day());
  CHECK_EQ(l.take_delta(5, 0), 0u);
  CHECK_EQ(l.take_delta(5, 4), 4u);  // clock not set yet: held by the caller
  CHECK(!l.has_day());
  l.credit(day, 4);
  CHECK(l.has_day());
  CHECK_EQ(l.today_tips(day), 4u);
}

}  // namespace

void run_core_tests() {
  test_crc_known_value();
  test_packet_round_trip();
  test_packet_rejects_bad_input();
  test_calendar();
  test_rain_basic_counting();
  test_rain_outdoor_reboot();
  test_rain_glitches();
  test_rain_day_month_year_rollover();
  test_rain_gap_clears_old_days();
  test_rain_clock_going_backwards();
  test_rain_save_and_load();
  test_rain_take_delta_then_credit();
}
