#include "wx/timezone.h"

#include "wx/calendar.h"

namespace wx {
namespace {

bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Skips a zone name: letters (3+) or <...>.
bool skip_name(const char*& p) {
  if (*p == '<') {
    ++p;
    while (*p && *p != '>') ++p;
    if (*p != '>') return false;
    ++p;
    return true;
  }
  const char* start = p;
  while (is_alpha(*p)) ++p;
  return p - start >= 3;
}

bool parse_uint(const char*& p, int32_t* out, int32_t max) {
  if (!is_digit(*p)) return false;
  int32_t v = 0;
  while (is_digit(*p)) {
    v = v * 10 + (*p - '0');
    if (v > max) return false;
    ++p;
  }
  *out = v;
  return true;
}

// [+-]hh[:mm[:ss]] → seconds (sign as written).
bool parse_hms(const char*& p, int32_t* out) {
  int sign = 1;
  if (*p == '+') {
    ++p;
  } else if (*p == '-') {
    sign = -1;
    ++p;
  }
  int32_t h = 0, m = 0, s = 0;
  if (!parse_uint(p, &h, 167)) return false;
  if (*p == ':') {
    ++p;
    if (!parse_uint(p, &m, 59)) return false;
    if (*p == ':') {
      ++p;
      if (!parse_uint(p, &s, 59)) return false;
    }
  }
  *out = sign * (h * 3600 + m * 60 + s);
  return true;
}

bool parse_rule(const char*& p, TzRule* r) {
  if (*p != 'M') return false;  // only the Mm.w.d form (all real-world zones use it)
  ++p;
  int32_t m = 0, w = 0, d = 0;
  if (!parse_uint(p, &m, 12) || *p != '.') return false;
  ++p;
  if (!parse_uint(p, &w, 5) || *p != '.') return false;
  ++p;
  if (!parse_uint(p, &d, 6)) return false;
  if (m < 1 || w < 1) return false;
  r->month = static_cast<uint8_t>(m);
  r->week = static_cast<uint8_t>(w);
  r->wday = static_cast<uint8_t>(d);
  r->time_s = 7200;  // default 02:00
  if (*p == '/') {
    ++p;
    if (!parse_hms(p, &r->time_s)) return false;
  }
  return true;
}

int days_in_month(int32_t y, unsigned m) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2) {
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return leap ? 29 : 28;
  }
  return kDays[m - 1];
}

int weekday(int32_t day) {
  int32_t w = (day + 4) % 7;  // 1970-01-01 was a Thursday
  return static_cast<int>(w < 0 ? w + 7 : w);
}

// Local day number of rule r in year y.
int32_t rule_day(const TzRule& r, int32_t y) {
  const int32_t first = days_from_civil(y, r.month, 1);
  int dom = 1 + (r.wday - weekday(first) + 7) % 7 + (r.week - 1) * 7;
  while (dom > days_in_month(y, r.month)) dom -= 7;
  return first + dom - 1;
}

int64_t floor_div(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}

}  // namespace

bool tz_parse(const char* s, TimeZone* tz) {
  *tz = TimeZone{};
  if (s == nullptr) return false;
  const char* p = s;
  int32_t off = 0;
  if (!skip_name(p) || !parse_hms(p, &off)) return false;
  tz->std_offset_s = -off;  // POSIX: "MST7" means UTC-7
  if (*p == '\0') return true;

  if (!skip_name(p)) {
    *tz = TimeZone{};
    return false;
  }
  tz->has_dst = true;
  tz->dst_offset_s = tz->std_offset_s + 3600;
  if (*p != ',' && *p != '\0') {
    if (!parse_hms(p, &off)) {
      *tz = TimeZone{};
      return false;
    }
    tz->dst_offset_s = -off;
  }
  if (*p == '\0') {
    // No rules given: use the current US rules.
    tz->start = TzRule{3, 2, 0, 7200};
    tz->end = TzRule{11, 1, 0, 7200};
    return true;
  }
  if (*p != ',') {
    *tz = TimeZone{};
    return false;
  }
  ++p;
  if (!parse_rule(p, &tz->start) || *p != ',') {
    *tz = TimeZone{};
    return false;
  }
  ++p;
  if (!parse_rule(p, &tz->end) || *p != '\0') {
    *tz = TimeZone{};
    return false;
  }
  return true;
}

int32_t tz_offset(const TimeZone& tz, int64_t utc_s) {
  if (!tz.has_dst) return tz.std_offset_s;
  const int32_t local_day = static_cast<int32_t>(floor_div(utc_s + tz.std_offset_s, 86400));
  const int32_t year = civil_from_days(local_day).year;
  const int64_t start_utc =
      static_cast<int64_t>(rule_day(tz.start, year)) * 86400 + tz.start.time_s - tz.std_offset_s;
  const int64_t end_utc =
      static_cast<int64_t>(rule_day(tz.end, year)) * 86400 + tz.end.time_s - tz.dst_offset_s;
  bool dst;
  if (start_utc < end_utc) {
    dst = utc_s >= start_utc && utc_s < end_utc;  // northern hemisphere
  } else {
    dst = !(utc_s >= end_utc && utc_s < start_utc);  // southern hemisphere
  }
  return dst ? tz.dst_offset_s : tz.std_offset_s;
}

LocalTime to_local(const TimeZone& tz, int64_t utc_s) {
  const int32_t off = tz_offset(tz, utc_s);
  const int64_t local = utc_s + off;
  LocalTime lt{};
  lt.day = static_cast<int32_t>(floor_div(local, 86400));
  lt.second_of_day = static_cast<int32_t>(local - static_cast<int64_t>(lt.day) * 86400);
  lt.minute_of_day = lt.second_of_day / 60;
  const Date d = civil_from_days(lt.day);
  lt.year = d.year;
  lt.month = d.month;
  lt.mday = d.day;
  lt.wday = static_cast<uint8_t>(weekday(lt.day));
  lt.dst = tz.has_dst && off == tz.dst_offset_s && off != tz.std_offset_s;
  return lt;
}

int64_t local_day_start_utc(const TimeZone& tz, int32_t day) {
  // Guess with the standard offset, then correct once with the real offset.
  int64_t guess = static_cast<int64_t>(day) * 86400 - tz.std_offset_s;
  return static_cast<int64_t>(day) * 86400 - tz_offset(tz, guess);
}

}  // namespace wx
