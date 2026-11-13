// Wall clock built from a monotonic microsecond counter plus NTP syncs.
// The Pico has no battery-backed clock, so until the first NTP sync the time
// is unknown and is_set() is false.
#pragma once

#include <cstdint>

namespace wx {

class Clock {
 public:
  // Record that `unix_s` is the current time at monotonic time `mono_us`.
  void sync(uint32_t unix_s, uint64_t mono_us) {
    base_unix_s_ = unix_s;
    base_mono_us_ = mono_us;
    set_ = true;
    ++syncs_;
    last_sync_mono_us_ = mono_us;
  }

  bool is_set() const { return set_; }
  uint32_t syncs() const { return syncs_; }
  uint64_t last_sync_mono_us() const { return last_sync_mono_us_; }

  // Current UTC seconds; 0 if the clock isn't set.
  int64_t now(uint64_t mono_us) const {
    if (!set_) return 0;
    return static_cast<int64_t>(base_unix_s_) +
           static_cast<int64_t>((mono_us - base_mono_us_) / 1000000u);
  }

 private:
  bool set_ = false;
  uint32_t base_unix_s_ = 0;
  uint64_t base_mono_us_ = 0;
  uint32_t syncs_ = 0;
  uint64_t last_sync_mono_us_ = 0;
};

}  // namespace wx
