#include "wx/flash_store.h"

#include <cstring>

#include "wx/crc32.h"

namespace wx {
namespace {

constexpr uint32_t kMagic = 0x57585231;  // "WXR1"

void put32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

uint32_t get32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Header: magic, kind, seq, len, crc (over kind..len and payload).
uint32_t record_crc(const uint8_t* header, const uint8_t* payload, uint32_t len) {
  uint32_t c = crc32(header + 4, 12);
  return crc32(payload, len, c);
}

// Sequence comparison that survives wrap-around.
bool newer(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) > 0; }

}  // namespace

RecordStore::RecordStore(FlashDevice& dev, uint32_t sector_a, uint32_t sector_b, uint32_t kind,
                         uint8_t* scratch)
    : dev_(dev), sectors_{sector_a, sector_b}, kind_(kind), scratch_(scratch) {}

RecordStore::Slot RecordStore::check(uint32_t sector) {
  Slot s{false, 0, 0};
  if (!dev_.read(sector, scratch_, FlashDevice::kSectorSize)) return s;
  const uint8_t* h = scratch_;
  if (get32(h) != kMagic || get32(h + 4) != kind_) return s;
  const uint32_t len = get32(h + 12);
  if (len > kMaxPayload) return s;
  if (get32(h + 16) != record_crc(h, h + kHeaderSize, len)) return s;
  s.valid = true;
  s.seq = get32(h + 8);
  s.len = len;
  return s;
}

int RecordStore::load(void* dst, size_t cap) {
  Slot a = check(sectors_[0]);
  Slot b = check(sectors_[1]);
  newest_ = -1;
  if (a.valid && (!b.valid || !newer(b.seq, a.seq))) newest_ = 0;
  if (b.valid && (!a.valid || newer(b.seq, a.seq))) newest_ = 1;
  if (newest_ < 0) {
    seq_ = 0;
    return -1;
  }
  const Slot& s = newest_ == 0 ? a : b;
  seq_ = s.seq;
  // scratch_ holds whichever sector was read last; re-read the winner.
  if (!dev_.read(sectors_[newest_], scratch_, FlashDevice::kSectorSize)) return -1;
  const size_t n = s.len < cap ? s.len : cap;
  std::memcpy(dst, scratch_ + kHeaderSize, n);
  return static_cast<int>(s.len);
}

bool RecordStore::save(const void* src, size_t len) {
  if (len > kMaxPayload) return false;
  if (newest_ < 0) {
    // Unknown state (never loaded): find out which slot is current first.
    Slot a = check(sectors_[0]);
    Slot b = check(sectors_[1]);
    if (a.valid && (!b.valid || !newer(b.seq, a.seq))) {
      newest_ = 0;
      seq_ = a.seq;
    } else if (b.valid) {
      newest_ = 1;
      seq_ = b.seq;
    }
  }
  const int target = newest_ == 0 ? 1 : 0;
  const uint32_t next_seq = seq_ + 1;

  std::memset(scratch_, 0xFF, FlashDevice::kSectorSize);
  uint8_t* h = scratch_;
  put32(h, kMagic);
  put32(h + 4, kind_);
  put32(h + 8, next_seq);
  put32(h + 12, static_cast<uint32_t>(len));
  std::memcpy(h + kHeaderSize, src, len);
  put32(h + 16, record_crc(h, h + kHeaderSize, static_cast<uint32_t>(len)));

  size_t total = kHeaderSize + len;
  total = (total + FlashDevice::kPageSize - 1) / FlashDevice::kPageSize * FlashDevice::kPageSize;

  if (!dev_.erase_sector(sectors_[target])) return false;
  if (!dev_.program(sectors_[target], scratch_, total)) return false;

  // Verify what actually landed in flash.
  Slot v = check(sectors_[target]);
  if (!v.valid || v.seq != next_seq) return false;

  newest_ = target;
  seq_ = next_seq;
  return true;
}

bool RecordStore::wipe() {
  bool ok = dev_.erase_sector(sectors_[0]);
  ok = dev_.erase_sector(sectors_[1]) && ok;
  newest_ = -1;
  seq_ = 0;
  return ok;
}

}  // namespace wx
