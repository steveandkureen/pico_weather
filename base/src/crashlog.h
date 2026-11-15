// Crash recorder: breadcrumbs and fault info kept in watchdog scratch
// registers, which survive a reset (but not a power cut).
//
// - crashlog::crumb(n) marks the step the firmware is in.
// - A HardFault records the faulting PC/LR and the current crumb, then reboots.
// - At boot, crashlog::boot() reads what the previous run left behind.
#pragma once

#include <cstdint>

namespace crashlog {

struct Report {
  bool fault = false;          // previous run hit a HardFault
  bool quick_reset = false;    // previous run reset before reaching "settled"
  uint32_t pc = 0, lr = 0;     // fault address and return address
  uint32_t crumb = 0;          // last step reached by the previous run
  uint32_t chip_reset = 0;     // POWMAN_CHIP_RESET at boot (reset cause bits)
  uint32_t wd_reason = 0;      // WATCHDOG_REASON at boot
};

// Read and clear what the previous run recorded. Call first thing in main().
const Report& boot();
const Report& last();

// Mark progress. Values < 0x8000: steps; settled() sets the top bit.
void crumb(uint32_t step);
// Call once the firmware has run normally for a while.
void settled();

// Formats the report as JSON (no braces around the outer object omitted).
int to_json(char* out, unsigned cap);

}  // namespace crashlog
