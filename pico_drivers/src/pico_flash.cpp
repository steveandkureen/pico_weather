#include "wxp/pico_flash.h"

#include <cstring>

#include "hardware/flash.h"
#include "pico/flash.h"

namespace wxp {
namespace {

constexpr uint32_t kRegionBytes = PicoFlash::kSectors * wx::FlashDevice::kSectorSize;
constexpr uint32_t kRegionStart = PICO_FLASH_SIZE_BYTES - kRegionBytes;  // offset from flash start

// The firmware binary must end before our region (checked at link time is
// not possible here; checked at boot in main via __flash_binary_end).

struct EraseArgs {
  uint32_t offset;
};
struct ProgramArgs {
  uint32_t offset;
  const uint8_t* src;
  size_t len;
};

void do_erase(void* p) {
  const auto* a = static_cast<const EraseArgs*>(p);
  flash_range_erase(a->offset, wx::FlashDevice::kSectorSize);
}

void do_program(void* p) {
  const auto* a = static_cast<const ProgramArgs*>(p);
  flash_range_program(a->offset, a->src, a->len);
}

bool in_region(uint32_t offset, size_t len) { return offset + len <= kRegionBytes; }

}  // namespace

bool PicoFlash::read(uint32_t offset, void* dst, size_t len) {
  if (!in_region(offset, len)) return false;
  // Flash is memory-mapped (XIP); the SDK flushes the cache after writes.
  std::memcpy(dst, reinterpret_cast<const void*>(XIP_BASE + kRegionStart + offset), len);
  return true;
}

bool PicoFlash::erase_sector(uint32_t offset) {
  if (!in_region(offset, kSectorSize) || offset % kSectorSize != 0) return false;
  EraseArgs a{kRegionStart + offset};
  // Interrupts are off for the erase (~45 ms typical, up to ~400 ms worst
  // case). The watchdog timeout is 8 s, so this is safe.
  const bool ok = flash_safe_execute(&do_erase, &a, 1000) == PICO_OK;
  ++writes_;
  if (!ok) ++failures_;
  return ok;
}

bool PicoFlash::program(uint32_t offset, const void* src, size_t len) {
  if (!in_region(offset, len) || offset % kPageSize != 0 || len % kPageSize != 0) return false;
  ProgramArgs a{kRegionStart + offset, static_cast<const uint8_t*>(src), len};
  const bool ok = flash_safe_execute(&do_program, &a, 1000) == PICO_OK;
  if (!ok) ++failures_;
  return ok;
}

}  // namespace wxp

// Exposed for the boot-time overlap check.
extern "C" uint32_t wxp_flash_region_start() { return wxp::kRegionStart; }
