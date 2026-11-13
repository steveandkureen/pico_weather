// Rain history kept by the base station.
//
// The outdoor station sends a running count of bucket tips since it booted.
// The ledger turns that into new tips, then stores one total per local day for
// the last kDays days. Today / month / year totals are sums over those days.
//
// All state lives in one fixed-size struct (no heap), which the base station
// saves to flash as a single block.
#pragma once

#include <cstdint>

namespace wx {

class RainLedger {
 public:
  static constexpr int kDays = 400;  // more than a year, so "this year" always fits
  static constexpr uint32_t kStateVersion = 1;

  // Any jump bigger than this between two packets is treated as a glitch and
  // ignored (about 1 m of rain at 0.2 mm/tip).
  static constexpr uint32_t kMaxTipsPerReading = 5000;

  struct State {
    uint32_t version;
    uint8_t have_last;     // 1 once a reading has been seen
    uint8_t reserved[1];
    uint16_t last_boot_id;
    uint32_t last_tips;
    int32_t newest_day;    // day number (days since 1970-01-01, local time)
    uint16_t tips[kDays];  // tips per day; slot = day mod kDays
  };

  RainLedger() { reset(); }

  void reset();

  // Load saved state. Returns false (and resets) if it's not a valid version.
  bool load(const State& s);
  const State& state() const { return s_; }

  // Feed one packet. `today` is the current local day number.
  // Returns the number of tips added.
  uint32_t on_reading(uint16_t boot_id, uint32_t tips, int32_t today);

  // Tips on one day (0 if it's outside the kept window).
  uint32_t day_tips(int32_t day) const;

  // Sum of tips for days first..last inclusive, clipped to the kept window.
  uint32_t range_tips(int32_t first, int32_t last) const;

  uint32_t today_tips(int32_t today) const { return day_tips(today); }
  uint32_t month_tips(int32_t today) const;
  uint32_t year_tips(int32_t today) const;

 private:
  void advance_to(int32_t today);
  static int slot(int32_t day);

  State s_;
};

}  // namespace wx
