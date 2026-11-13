// Minimal self-contained tests (no framework) for the shared code.
#include <cstdio>
#include <cstring>

#include "wx/calendar.h"
#include "wx/packet.h"
#include "wx/rain_ledger.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      ++g_failures;                                                    \
    }                                                                  \
  } while (0)

#define CHECK_EQ(a, b)                                                           \
  do {                                                                           \
    auto _a = (a);                                                               \
    auto _b = (b);                                                               \
    if (!(_a == _b)) {                                                           \
      std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__,   \
                  #a, #b, static_cast<long long>(_a), static_cast<long long>(_b)); \
      ++g_failures;                                                              \
    }                                                                            \
  } while (0)

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
  r.wind_avg_mps_x100 = 512;
  r.wind_gust_mps_x100 = 1999;
  r.wind_dir_deg = 359;
  r.rain_tips = 0xDEADBEEF;
  r.battery_mv = 4123;
  return r;
}

void test_crc_known_value() {
  // Standard check value for CRC-16/CCITT-FALSE.
  const char* s = "123456789";
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
  CHECK_EQ(out.wind_avg_mps_x100, in.wind_avg_mps_x100);
  CHECK_EQ(out.wind_gust_mps_x100, in.wind_gust_mps_x100);
  CHECK_EQ(out.wind_dir_deg, in.wind_dir_deg);
  CHECK_EQ(out.rain_tips, in.rain_tips);
  CHECK_EQ(out.battery_mv, in.battery_mv);
}

void test_packet_rejects_bad_input() {
  uint8_t buf[64];
  wx::encode(sample(), buf, sizeof(buf));
  wx::OutdoorReading out;

  CHECK(!wx::decode(buf, wx::kPacketSize - 1, &out));  // short
  CHECK(!wx::decode(buf, wx::kPacketSize + 1, &out));  // long
  CHECK_EQ(wx::encode(sample(), buf, wx::kPacketSize - 1), 0u);  // no room

  // Flipping any single bit must be caught.
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
  CHECK_EQ(l.on_reading(7, 10, day), 0u);  // first reading only sets the baseline
  CHECK_EQ(l.on_reading(7, 13, day), 3u);
  CHECK_EQ(l.on_reading(7, 13, day), 0u);  // repeated count adds nothing
  CHECK_EQ(l.on_reading(7, 20, day), 7u);  // lost packets in between still count
  CHECK_EQ(l.today_tips(day), 10u);
}

void test_rain_outdoor_reboot() {
  wx::RainLedger l;
  const int32_t day = wx::days_from_civil(2026, 11, 12);
  l.on_reading(1, 100, day);
  l.on_reading(1, 105, day);           // +5
  CHECK_EQ(l.on_reading(2, 4, day), 4u);  // new boot id: all 4 tips are new
  // Reboot where the new counter has already passed the old one.
  CHECK_EQ(l.on_reading(3, 500, day), 500u);
  CHECK_EQ(l.today_tips(day), 509u);
}

void test_rain_glitches() {
  wx::RainLedger l;
  const int32_t day = wx::days_from_civil(2026, 11, 12);
  l.on_reading(1, 100, day);
  CHECK_EQ(l.on_reading(1, 90, day), 0u);   // went down without reboot: resync
  CHECK_EQ(l.on_reading(1, 92, day), 2u);
  CHECK_EQ(l.on_reading(1, 92 + wx::RainLedger::kMaxTipsPerReading + 1, day), 0u);
  CHECK_EQ(l.today_tips(day), 2u);
}

void test_rain_day_month_year_rollover() {
  wx::RainLedger l;
  const int32_t dec31 = wx::days_from_civil(2026, 12, 31);
  l.on_reading(1, 0, wx::days_from_civil(2026, 11, 30));
  l.on_reading(1, 5, wx::days_from_civil(2026, 11, 30));   // Nov: 5
  l.on_reading(1, 8, wx::days_from_civil(2026, 12, 1));    // Dec 1: 3
  l.on_reading(1, 10, dec31);                              // Dec 31: 2
  CHECK_EQ(l.today_tips(dec31), 2u);
  CHECK_EQ(l.month_tips(dec31), 5u);
  CHECK_EQ(l.year_tips(dec31), 10u);

  const int32_t jan1 = dec31 + 1;
  l.on_reading(1, 11, jan1);
  CHECK_EQ(l.today_tips(jan1), 1u);
  CHECK_EQ(l.month_tips(jan1), 1u);
  CHECK_EQ(l.year_tips(jan1), 1u);
  CHECK_EQ(l.day_tips(dec31), 2u);  // yesterday still in history
}

void test_rain_gap_clears_old_days() {
  wx::RainLedger l;
  const int32_t d0 = wx::days_from_civil(2026, 1, 1);
  l.on_reading(1, 0, d0);
  l.on_reading(1, 7, d0);
  // Base station off for a long time: slots must not leak old totals.
  const int32_t later = d0 + wx::RainLedger::kDays;  // same slot as d0
  l.on_reading(1, 7, later);
  CHECK_EQ(l.today_tips(later), 0u);
  CHECK_EQ(l.day_tips(d0), 0u);  // fell out of the window

  // Short gap: days in between read as 0.
  l.on_reading(1, 9, later + 3);
  CHECK_EQ(l.day_tips(later + 1), 0u);
  CHECK_EQ(l.day_tips(later + 3), 2u);
}

void test_rain_clock_going_backwards() {
  wx::RainLedger l;
  const int32_t day = wx::days_from_civil(2026, 6, 1);
  l.on_reading(1, 0, day);
  l.on_reading(1, 4, day);
  CHECK_EQ(l.on_reading(1, 6, day - 5), 2u);  // counted on the newest day, nothing wiped
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
  // Base rebooted while outdoor kept counting: the saved baseline catches it.
  CHECK_EQ(b.on_reading(9, 20, day), 8u);

  wx::RainLedger::State bad = a.state();
  bad.version = 999;
  wx::RainLedger c;
  CHECK(!c.load(bad));
  CHECK_EQ(c.today_tips(day), 0u);
}

}  // namespace

int main() {
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

  if (g_failures == 0) {
    std::printf("all tests passed\n");
    return 0;
  }
  std::printf("%d failure(s)\n", g_failures);
  return 1;
}
