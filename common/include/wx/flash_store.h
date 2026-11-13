// Crash-safe storage of one record in flash, using two sectors (A/B).
//
// Each save goes to the sector NOT holding the newest valid copy, so the
// previous copy is untouched until the new one is fully written. Each copy
// carries a sequence number and a CRC; on load, the valid copy with the
// highest sequence wins. A power cut at any moment leaves at least one good
// copy (the old one, or the new one if it finished).
#pragma once

#include <cstddef>
#include <cstdint>

namespace wx {

// Minimal flash interface, implemented by the Pico flash driver and by a fake
// in the host tests. Offsets are relative to the start of the region.
class FlashDevice {
 public:
  static constexpr size_t kSectorSize = 4096;
  static constexpr size_t kPageSize = 256;

  virtual ~FlashDevice() = default;
  virtual bool read(uint32_t offset, void* dst, size_t len) = 0;
  virtual bool erase_sector(uint32_t offset) = 0;                      // offset sector-aligned
  virtual bool program(uint32_t offset, const void* src, size_t len) = 0;  // page-aligned, len multiple of kPageSize
};

class RecordStore {
 public:
  static constexpr size_t kHeaderSize = 20;
  static constexpr size_t kMaxPayload = FlashDevice::kSectorSize - kHeaderSize;

  // `kind` tags what the record holds (e.g. 'SETG'); a sector holding another
  // kind is treated as empty. `scratch` must be FlashDevice::kSectorSize bytes.
  RecordStore(FlashDevice& dev, uint32_t sector_a, uint32_t sector_b, uint32_t kind,
              uint8_t* scratch);

  // Loads the newest valid copy into dst (up to cap bytes) and returns its
  // length, or -1 if there is no valid copy. A record shorter than cap is
  // returned as-is, so callers can fill newer fields with defaults.
  int load(void* dst, size_t cap);

  // Saves a new copy. Returns false if len is too big or the flash write fails.
  bool save(const void* src, size_t len);

  // Erases both copies.
  bool wipe();

  uint32_t seq() const { return seq_; }

 private:
  struct Slot {
    bool valid;
    uint32_t seq;
    uint32_t len;
  };
  Slot check(uint32_t sector);

  FlashDevice& dev_;
  uint32_t sectors_[2];
  uint32_t kind_;
  uint8_t* scratch_;
  uint32_t seq_ = 0;
  int newest_ = -1;  // index of newest valid slot, -1 if none / unknown
};

}  // namespace wx
