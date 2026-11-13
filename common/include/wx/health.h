// Watchdog gate: the main loop feeds the hardware watchdog only while every
// registered task keeps making progress. A task that is still being polled
// but is stuck (its progress counter stops moving) causes a reset, instead of
// hiding behind a loop that keeps spinning.
#pragma once

#include <cstdint>

namespace wx {

class HealthMonitor {
 public:
  static constexpr int kMaxTasks = 10;

  // Returns the task index. max_stall_ms = how long its counter may stand still.
  int add(const char* name, uint32_t max_stall_ms);

  // Report a task's current progress counter.
  void report(int task, uint32_t progress, uint32_t now_ms);

  // True if every task moved within its limit. Also records the loop time.
  bool all_ok(uint32_t now_ms);

  // Name of the first stalled task (for the log before a reset), or nullptr.
  const char* stalled(uint32_t now_ms) const;

  void loop_time(uint32_t ms) {
    if (ms > max_loop_ms_) max_loop_ms_ = ms;
  }
  uint32_t max_loop_ms() const { return max_loop_ms_; }
  // Max since the last call (outdoor reports it per packet).
  uint32_t take_max_loop_ms() {
    uint32_t m = window_max_;
    window_max_ = 0;
    return m;
  }
  void note_loop(uint32_t ms) {
    loop_time(ms);
    if (ms > window_max_) window_max_ = ms;
  }

 private:
  struct Task {
    const char* name;
    uint32_t max_stall_ms;
    uint32_t last_progress;
    uint32_t last_change_ms;
    bool seen;
  };
  Task tasks_[kMaxTasks] = {};
  int n_ = 0;
  uint32_t max_loop_ms_ = 0;
  uint32_t window_max_ = 0;
};

}  // namespace wx
