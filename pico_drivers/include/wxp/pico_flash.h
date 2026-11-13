// FlashDevice over the last few sectors of the Pico's on-board flash.
//
// Layout (from the end of flash):
//   [-4 sectors] settings A, settings B, history A, history B
// The firmware image starts at the beginning of flash and is far smaller.
#pragma once

#include "wx/flash_store.h"

namespace wxp {

class PicoFlash : public wx::FlashDevice {
 public:
  static constexpr uint32_t kSectors = 4;

  // Offsets inside the region.
  static constexpr uint32_t kSettingsA = 0 * kSectorSize;
  static constexpr uint32_t kSettingsB = 1 * kSectorSize;
  static constexpr uint32_t kHistoryA = 2 * kSectorSize;
  static constexpr uint32_t kHistoryB = 3 * kSectorSize;

  bool read(uint32_t offset, void* dst, size_t len) override;
  bool erase_sector(uint32_t offset) override;
  bool program(uint32_t offset, const void* src, size_t len) override;

  uint32_t writes() const { return writes_; }
  uint32_t failures() const { return failures_; }

 private:
  uint32_t writes_ = 0;
  uint32_t failures_ = 0;
};

}  // namespace wxp
