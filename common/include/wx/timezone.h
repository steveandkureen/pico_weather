// POSIX TZ strings ("MST7MDT,M3.2.0,M11.1.0") → local time offset, with DST.
//
// Our own small parser instead of newlib's tzset(): no heap, no environment
// variables, fully testable on the PC. Supports the forms every US/EU zone
// uses: std name + offset, optional dst name [+ offset], and Mm.w.d[/time]
// transition rules. Names may be alphabetic or <+0330>-style.
#pragma once

#include <cstdint>

namespace wx {

struct TzRule {
  uint8_t month;   // 1..12
  uint8_t week;    // 1..5 (5 = last)
  uint8_t wday;    // 0 = Sunday
  int32_t time_s;  // local time of day the change happens
};

struct TimeZone {
  int32_t std_offset_s = 0;  // local = utc + offset (note: POSIX strings use the opposite sign)
  int32_t dst_offset_s = 0;
  bool has_dst = false;
  TzRule start{};  // when DST starts (in standard local time)
  TzRule end{};    // when DST ends (in DST local time)
};

// Returns false if the string can't be parsed (tz is then UTC).
bool tz_parse(const char* s, TimeZone* tz);

// Offset to add to UTC seconds for the given instant.
int32_t tz_offset(const TimeZone& tz, int64_t utc_s);

struct LocalTime {
  int32_t day;     // days since 1970-01-01 (local)
  int32_t year;
  uint8_t month;
  uint8_t mday;
  uint8_t wday;    // 0 = Sunday
  int32_t minute_of_day;  // 0..1439
  int32_t second_of_day;  // 0..86399
  bool dst;
};

LocalTime to_local(const TimeZone& tz, int64_t utc_s);

// Local midnight → UTC for a given local day (handles the DST offset of that day).
int64_t local_day_start_utc(const TimeZone& tz, int32_t day);

}  // namespace wx
