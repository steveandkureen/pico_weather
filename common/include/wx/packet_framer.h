// Rebuilds packets from the LoRa module's UART byte stream.
//
// The module outputs each received packet as one burst of bytes, optionally
// followed by one RSSI byte. A gap of more than kGapMs between bytes ends a
// frame. Inside a frame we look for a packet that passes decode() (magic,
// version, CRC), so stray or partial bytes are simply discarded.
#pragma once

#include <cstddef>
#include <cstdint>

#include "wx/packet.h"

namespace wx {

class PacketFramer {
 public:
  static constexpr uint32_t kGapMs = 20;  // ~20 byte times at 9600 baud
  static constexpr size_t kBufSize = 128;

  void push(uint8_t b, uint32_t now_ms);

  // Call often. Returns true when a complete, valid packet is available;
  // `out` gets it and `rssi_dbm` the signal strength (0 if not reported).
  bool poll(uint32_t now_ms, OutdoorReading* out, int16_t* rssi_dbm);

  uint32_t rejected() const { return rejected_; }

 private:
  uint8_t buf_[kBufSize];
  size_t len_ = 0;
  bool overflow_ = false;
  uint32_t last_ms_ = 0;
  uint32_t rejected_ = 0;
};

}  // namespace wx
