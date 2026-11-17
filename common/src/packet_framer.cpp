#include "wx/packet_framer.h"

namespace wx {

void PacketFramer::push(uint8_t b, uint32_t now_ms) {
  if (len_ > 0 && now_ms - last_ms_ > kGapMs) {
    // Previous frame was never collected by poll(); start fresh.
    len_ = 0;
    overflow_ = false;
  }
  last_ms_ = now_ms;
  if (len_ < kBufSize) {
    buf_[len_++] = b;
  } else {
    overflow_ = true;
  }
}

bool PacketFramer::poll(uint32_t now_ms, OutdoorReading* out, int16_t* rssi_dbm) {
  if (len_ == 0 || now_ms - last_ms_ <= kGapMs) return false;

  const size_t n = len_;
  len_ = 0;
  if (overflow_) {
    overflow_ = false;
    ++rejected_;
    return false;
  }

  for (size_t off = 0; off + kPacketSize <= n; ++off) {
    if (buf_[off] != kPacketMagic) continue;
    if (decode(buf_ + off, kPacketSize, out)) {
      const size_t after = off + kPacketSize;
      // One trailing byte = RSSI appended by the module (AT+DRSSI1). DX-LR22
      // application guide: dBm = -(0xFF - value), e.g. 0xAB = -84 dBm.
      *rssi_dbm = (after + 1 == n) ? static_cast<int16_t>(-(255 - buf_[after])) : 0;
      return true;
    }
  }
  ++rejected_;
  return false;
}

}  // namespace wx
