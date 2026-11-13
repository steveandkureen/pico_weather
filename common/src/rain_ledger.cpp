#include "wx/rain_ledger.h"

#include <cstring>

#include "wx/calendar.h"

namespace wx {

namespace {
constexpr int32_t kNoDay = INT32_MIN;
}

void RainLedger::reset() {
  std::memset(&s_, 0, sizeof(s_));
  s_.version = kStateVersion;
  s_.newest_day = kNoDay;
}

bool RainLedger::load(const State& s) {
  if (s.version != kStateVersion) {
    reset();
    return false;
  }
  s_ = s;
  return true;
}

int RainLedger::slot(int32_t day) {
  int32_t m = day % kDays;
  return static_cast<int>(m < 0 ? m + kDays : m);
}

void RainLedger::advance_to(int32_t today) {
  if (s_.newest_day == kNoDay) {
    std::memset(s_.tips, 0, sizeof(s_.tips));
    s_.newest_day = today;
    return;
  }
  // If the clock moves backwards, keep counting on the newest day rather than
  // wiping history.
  if (today <= s_.newest_day) return;

  const int64_t gap = static_cast<int64_t>(today) - s_.newest_day;
  if (gap >= kDays) {
    std::memset(s_.tips, 0, sizeof(s_.tips));
  } else {
    for (int32_t d = s_.newest_day + 1; d <= today; ++d) s_.tips[slot(d)] = 0;
  }
  s_.newest_day = today;
}

uint32_t RainLedger::on_reading(uint16_t boot_id, uint32_t tips, int32_t today) {
  advance_to(today);

  uint32_t delta = 0;
  if (!s_.have_last) {
    // First reading ever: no baseline yet, so just record it.
    delta = 0;
  } else if (boot_id != s_.last_boot_id) {
    // Outdoor unit rebooted: its counter restarted from 0, so every tip it
    // reports is new.
    delta = tips;
  } else if (tips >= s_.last_tips) {
    delta = tips - s_.last_tips;
  } else {
    // Counter went down without a reboot. Shouldn't happen; resync, add nothing.
    delta = 0;
  }

  if (delta > kMaxTipsPerReading) delta = 0;

  s_.have_last = 1;
  s_.last_boot_id = boot_id;
  s_.last_tips = tips;

  if (delta > 0) {
    // Record against the newest day (equals today unless the clock went back).
    uint16_t& cell = s_.tips[slot(s_.newest_day)];
    const uint32_t sum = static_cast<uint32_t>(cell) + delta;
    cell = static_cast<uint16_t>(sum > 0xFFFF ? 0xFFFF : sum);
  }
  return delta;
}

uint32_t RainLedger::day_tips(int32_t day) const {
  if (s_.newest_day == kNoDay) return 0;
  if (day > s_.newest_day || day <= s_.newest_day - kDays) return 0;
  return s_.tips[slot(day)];
}

uint32_t RainLedger::range_tips(int32_t first, int32_t last) const {
  uint32_t total = 0;
  for (int32_t d = first; d <= last; ++d) total += day_tips(d);
  return total;
}

uint32_t RainLedger::month_tips(int32_t today) const {
  const Date t = civil_from_days(today);
  return range_tips(days_from_civil(t.year, t.month, 1), today);
}

uint32_t RainLedger::year_tips(int32_t today) const {
  const Date t = civil_from_days(today);
  return range_tips(days_from_civil(t.year, 1, 1), today);
}

}  // namespace wx
