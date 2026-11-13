// Driver for Ebyte E220-900T22x-style LoRa modules (Semtech LLCC68 + the
// module's own UART firmware).
//
// ⚠ Written from the E220 manual before the modules arrived. Register values
// and timings are marked; verify against the module's datasheet on first use.
//
// The module is a black box, so the driver assumes it can misbehave:
//   * Every wait (AUX pin, command reply, transmit) has a deadline.
//   * On every (re)start the settings are read back and rewritten if they
//     differ (e.g. after a brown-out corrupted them).
//   * Recovery escalates: re-apply settings → power-cycle the module through
//     its load switch → report dead (the app then reboots the Pico).
#pragma once

#include <cstddef>
#include <cstdint>

#include "wx/packet_framer.h"

namespace wx {

// Hardware the driver needs. Implemented with GPIO/UART on the Pico, faked in tests.
class E220Port {
 public:
  virtual ~E220Port() = default;
  virtual void set_mode_pins(bool m0, bool m1) = 0;
  virtual bool aux_high() = 0;          // high = module idle
  virtual void set_power(bool on) = 0;  // load switch
  virtual void write(const uint8_t* data, size_t n) = 0;
  virtual int read_byte() = 0;          // -1 when nothing is waiting
};

struct E220Config {
  uint16_t address = 0x0000;
  uint8_t channel = 65;    // 900 MHz model: 850.125 + 65 = 915.125 MHz (US ISM band)
  uint8_t air_rate = 2;    // 0b010 = 2.4 kbps (slowest = longest range)
  uint8_t power = 2;       // 0b10 = 13 dBm (0 = 22, 1 = 17, 2 = 13, 3 = 10)
  bool rssi_byte = true;   // append RSSI byte to each received packet
  bool lbt = false;        // listen before talk

  // The 8 register bytes (ADDH, ADDL, REG0..REG3, CRYPT_H, CRYPT_L).
  void to_regs(uint8_t regs[8]) const;
  // Compares only what the module reads back (crypt key is write-only).
  bool matches_regs(const uint8_t regs[8]) const;
};

// Command frames (manual: "C0 = set, C1 = read").
size_t e220_write_cmd(const E220Config& cfg, uint8_t* out);  // 11 bytes
size_t e220_read_cmd(uint8_t* out);                          // 3 bytes

class E220Radio {
 public:
  explicit E220Radio(E220Port& port, const E220Config& cfg) : port_(port), cfg_(cfg) {}

  void start(uint32_t now_ms);   // power up and configure
  void poll(uint32_t now_ms);

  bool ready() const { return state_ == State::kReady; }
  bool dead() const { return state_ == State::kDead; }

  // Queue one packet for sending (outdoor). Returns false if not ready/busy.
  bool send(const uint8_t* data, size_t n, uint32_t now_ms);
  bool tx_busy() const { return tx_pending_; }

  // Received packets (base).
  bool receive(uint32_t now_ms, OutdoorReading* out, int16_t* rssi_dbm);

  // Ask for recovery (e.g. base heard nothing for a long time).
  // level 1 = re-apply settings, level 2 = power-cycle.
  void recover(int level, uint32_t now_ms);

  // Periodic self-check: re-read the settings while idle.
  void verify(uint32_t now_ms);

  struct Stats {
    uint32_t configs = 0;        // successful configurations
    uint32_t config_fixes = 0;   // times the settings had drifted and were rewritten
    uint32_t power_cycles = 0;
    uint32_t timeouts = 0;
    uint32_t sent = 0;
    uint32_t received = 0;
  };
  const Stats& stats() const { return stats_; }
  uint32_t rejected() const { return framer_.rejected(); }
  uint32_t progress() const { return progress_; }

 private:
  enum class State : uint8_t {
    kPowerOff,      // load switch off, waiting
    kPowerOnWait,   // waiting for AUX high after power-up
    kConfigMode,    // M0=M1=1, waiting for AUX
    kReadCmd,       // waiting for read reply
    kWriteCmd,      // waiting for write reply
    kNormalMode,    // M0=M1=0, waiting for AUX
    kReady,
    kDead,
  };

  void enter(State s, uint32_t now_ms, uint32_t timeout_ms);
  void timeout(uint32_t now_ms);
  void begin_power_cycle(uint32_t now_ms);
  void begin_config(uint32_t now_ms);
  bool collect_reply(uint8_t* regs);

  E220Port& port_;
  E220Config cfg_;
  State state_ = State::kPowerOff;
  uint32_t entered_ms_ = 0;
  uint32_t deadline_ms_ = 0;
  uint32_t settle_until_ms_ = 0;
  bool aux_seen_ = false;
  uint8_t reply_[16];
  size_t reply_len_ = 0;
  int failures_ = 0;          // consecutive failed bring-ups
  bool verify_only_ = false;  // config pass triggered by verify()
  bool wrote_ = false;
  bool tx_pending_ = false;
  bool tx_saw_busy_ = false;
  uint32_t tx_deadline_ms_ = 0;
  PacketFramer framer_;
  Stats stats_;
  uint32_t progress_ = 0;
};

}  // namespace wx
