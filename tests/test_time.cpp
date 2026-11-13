// Timezone rules, DST transitions, clock.
#include <cstring>

#include "test_util.h"
#include "wx/calendar.h"
#include "wx/clock.h"
#include "wx/timezone.h"

namespace {

int64_t utc(int y, unsigned m, unsigned d, int hh, int mm) {
  return static_cast<int64_t>(wx::days_from_civil(y, m, d)) * 86400 + hh * 3600 + mm * 60;
}

void test_parse_simple() {
  wx::TimeZone tz;
  CHECK(wx::tz_parse("MST7", &tz));
  CHECK_EQ(tz.std_offset_s, -7 * 3600);
  CHECK(!tz.has_dst);
  CHECK(wx::tz_parse("UTC0", &tz));
  CHECK_EQ(tz.std_offset_s, 0);
  CHECK(wx::tz_parse("<+0530>-5:30", &tz));
  CHECK_EQ(tz.std_offset_s, 5 * 3600 + 30 * 60);
  CHECK(!wx::tz_parse("", &tz));
  CHECK(!wx::tz_parse("garbage!", &tz));
  CHECK(!wx::tz_parse("EST5EDT,M3.2.0", &tz));  // missing end rule
}

void test_us_dst_2026() {
  // US Eastern: DST from Sun Mar 8 2026 02:00 EST to Sun Nov 1 2026 02:00 EDT.
  wx::TimeZone tz;
  CHECK(wx::tz_parse("EST5EDT,M3.2.0,M11.1.0", &tz));
  CHECK(tz.has_dst);
  CHECK_EQ(tz.dst_offset_s, -4 * 3600);
  // 06:59 UTC Mar 8 = 01:59 EST (still standard)
  CHECK_EQ(wx::tz_offset(tz, utc(2026, 3, 8, 6, 59)), -5 * 3600);
  // 07:00 UTC Mar 8 = 02:00 EST → 03:00 EDT
  CHECK_EQ(wx::tz_offset(tz, utc(2026, 3, 8, 7, 0)), -4 * 3600);
  // 05:59 UTC Nov 1 = 01:59 EDT
  CHECK_EQ(wx::tz_offset(tz, utc(2026, 11, 1, 5, 59)), -4 * 3600);
  // 06:00 UTC Nov 1 = 02:00 EDT → 01:00 EST
  CHECK_EQ(wx::tz_offset(tz, utc(2026, 11, 1, 6, 0)), -5 * 3600);
  CHECK_EQ(wx::tz_offset(tz, utc(2026, 7, 4, 12, 0)), -4 * 3600);
  CHECK_EQ(wx::tz_offset(tz, utc(2026, 12, 25, 12, 0)), -5 * 3600);
}

void test_short_form_defaults_to_us_rules() {
  wx::TimeZone tz;
  CHECK(wx::tz_parse("PST8PDT", &tz));
  CHECK(tz.has_dst);
  CHECK_EQ(wx::tz_offset(tz, utc(2026, 7, 1, 0, 0)), -7 * 3600);
  CHECK_EQ(wx::tz_offset(tz, utc(2026, 1, 1, 0, 0)), -8 * 3600);
}

void test_southern_hemisphere() {
  // Australia/Sydney: AEST-10AEDT,M10.1.0,M4.1.0/3
  wx::TimeZone tz;
  CHECK(wx::tz_parse("AEST-10AEDT,M10.1.0,M4.1.0/3", &tz));
  CHECK_EQ(wx::tz_offset(tz, utc(2026, 1, 15, 0, 0)), 11 * 3600);
  CHECK_EQ(wx::tz_offset(tz, utc(2026, 7, 15, 0, 0)), 10 * 3600);
}

void test_to_local() {
  wx::TimeZone tz;
  wx::tz_parse("MST7MDT,M3.2.0,M11.1.0", &tz);
  // 2026-11-13 06:30 UTC = 2026-11-12 23:30 MST
  wx::LocalTime lt = wx::to_local(tz, utc(2026, 11, 13, 6, 30));
  CHECK_EQ(lt.year, 2026);
  CHECK_EQ(lt.month, 11);
  CHECK_EQ(lt.mday, 12);
  CHECK_EQ(lt.minute_of_day, 23 * 60 + 30);
  CHECK_EQ(lt.day, wx::days_from_civil(2026, 11, 12));
  CHECK_EQ(lt.wday, 4);  // Thursday
  CHECK(!lt.dst);
  // Local midnight of Nov 12 = 07:00 UTC.
  CHECK_EQ(wx::local_day_start_utc(tz, wx::days_from_civil(2026, 11, 12)), utc(2026, 11, 12, 7, 0));
  // In summer, midnight MDT = 06:00 UTC.
  CHECK_EQ(wx::local_day_start_utc(tz, wx::days_from_civil(2026, 7, 4)), utc(2026, 7, 4, 6, 0));
}

void test_clock() {
  wx::Clock c;
  CHECK(!c.is_set());
  CHECK_EQ(c.now(123), 0);
  c.sync(1800000000u, 5000000);
  CHECK(c.is_set());
  CHECK_EQ(c.now(5000000), 1800000000);
  CHECK_EQ(c.now(5000000 + 61500000), 1800000061);
}

}  // namespace

void run_time_tests() {
  test_parse_simple();
  test_us_dst_2026();
  test_short_form_defaults_to_us_rules();
  test_southern_hemisphere();
  test_to_local();
  test_clock();
}
