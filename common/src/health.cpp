#include "wx/health.h"

namespace wx {

int HealthMonitor::add(const char* name, uint32_t max_stall_ms) {
  if (n_ >= kMaxTasks) return -1;
  tasks_[n_] = Task{name, max_stall_ms, 0, 0, false};
  return n_++;
}

void HealthMonitor::report(int task, uint32_t progress, uint32_t now_ms) {
  if (task < 0 || task >= n_) return;
  Task& t = tasks_[task];
  if (!t.seen || progress != t.last_progress) {
    t.last_progress = progress;
    t.last_change_ms = now_ms;
    t.seen = true;
  }
}

const char* HealthMonitor::stalled(uint32_t now_ms) const {
  for (int i = 0; i < n_; ++i) {
    const Task& t = tasks_[i];
    if (!t.seen) continue;  // not started yet: the boot sequence covers that
    if (now_ms - t.last_change_ms > t.max_stall_ms) return t.name;
  }
  return nullptr;
}

bool HealthMonitor::all_ok(uint32_t now_ms) { return stalled(now_ms) == nullptr; }

}  // namespace wx
