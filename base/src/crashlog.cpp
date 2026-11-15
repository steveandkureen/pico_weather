#include "crashlog.h"

#include <cstdio>

#include "hardware/structs/powman.h"
#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"

// Kept in watchdog scratch registers 0 and 1, which survive a reset but not
// a power cut. The RP2350 boot ROM uses scratch 2..7 for reboot parameters.
//   scratch[0]: 0xA5 in the top byte (ours), bit 23 = fault, bit 15 =
//               settled, bits 0..14 = crumb
//   scratch[1]: faulting PC
namespace {

constexpr uint32_t kTag = 0xA5000000;
constexpr uint32_t kTagMask = 0xFF000000;
constexpr uint32_t kFaultBit = 1u << 23;
constexpr uint32_t kSettledBit = 0x8000;

crashlog::Report g_report;

}  // namespace

extern "C" {

// Called from the HardFault entry below with the exception stack frame:
// r0, r1, r2, r3, r12, lr, pc, xpsr.
__attribute__((used)) void wx_hardfault_c(const uint32_t* frame) {
  watchdog_hw->scratch[1] = frame[6];
  watchdog_hw->scratch[0] |= kTag | kFaultBit;
  watchdog_reboot(0, 0, 0);
  for (;;) {
  }
}

// Replaces the SDK's default HardFault handler (a breakpoint, which without a
// debugger just locks up). Picks the stack the fault frame was pushed to.
__attribute__((naked)) void isr_hardfault(void) {
  __asm volatile(
      "movs r0, #4\n"
      "mov r1, lr\n"
      "tst r0, r1\n"
      "beq 1f\n"
      "mrs r0, psp\n"
      "b wx_hardfault_c\n"
      "1:\n"
      "mrs r0, msp\n"
      "b wx_hardfault_c\n");
}

}  // extern "C"

namespace crashlog {

const Report& boot() {
  Report& r = g_report;
  r.chip_reset = powman_hw->chip_reset;
  r.wd_reason = watchdog_hw->reason;
  const uint32_t s0 = watchdog_hw->scratch[0];
  if ((s0 & kTagMask) == kTag) {
    r.crumb = s0 & 0xFFFF;
    r.fault = (s0 & kFaultBit) != 0;
    if (r.fault) r.pc = watchdog_hw->scratch[1];
    // A crumb without the settled bit means the previous run reset early.
    r.quick_reset = (r.crumb & 0x7FFF) != 0 && (r.crumb & kSettledBit) == 0;
  }
  watchdog_hw->scratch[0] = kTag;
  watchdog_hw->scratch[1] = 0;
  return r;
}

const Report& last() { return g_report; }

void crumb(uint32_t step) {
  watchdog_hw->scratch[0] = kTag | (watchdog_hw->scratch[0] & kSettledBit) | (step & 0x7FFF);
}

void settled() { watchdog_hw->scratch[0] |= kSettledBit; }

int to_json(char* out, unsigned cap) {
  const Report& r = g_report;
  return std::snprintf(out, cap,
                       "{\"fault\":%s,\"quick_reset\":%s,\"pc\":\"0x%08lx\",\"lr\":\"0x%08lx\","
                       "\"crumb\":\"0x%04lx\",\"chip_reset\":\"0x%08lx\",\"wd_reason\":\"0x%lx\"}",
                       r.fault ? "true" : "false", r.quick_reset ? "true" : "false",
                       (unsigned long)r.pc, (unsigned long)r.lr, (unsigned long)r.crumb,
                       (unsigned long)r.chip_reset, (unsigned long)r.wd_reason);
}

}  // namespace crashlog
