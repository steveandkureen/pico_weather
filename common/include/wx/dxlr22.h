// Driver for DX-SMART DX-LR22-900T22D LoRa modules (Semtech LLCC68 behind the
// module's own UART firmware, configured with AT commands).
//
// Checked against a real module (firmware V1.2.4) and the vendor's
// "DX-LR22-900T22D application guide":
//   * Power-on = transparent mode: bytes in on RX go out over the air.
//   * "+++\r\n" toggles AT mode: reply "Entry AT", or "Exit AT" then
//     "Power on" (the module restarts when leaving AT mode, which is also
//     when new settings take effect).
//   * Commands are "AT+NAME\r\n" (query → "+NAME=value") and
//     "AT+NAMEvalue\r\n" (set → "+NAME=value", "OK"); errors are "ERROR=10x".
//     The line ending must be exactly CR LF.
//   * AUX is HIGH while busy (sending, receiving, switching mode), LOW when idle.
//   * M0/M1 are outputs from the module by default (AT+SWITCH=0).
//
// The module is a black box, so the driver assumes it can misbehave:
//   * Every wait (AUX, reply, transmit) has a deadline.
//   * On every start, and on verify(), all settings are read back and
//     rewritten if they differ.
//   * Recovery escalates: re-apply settings → power-cycle the module through
//     its load switch → rest, then try again. It never reboots the Pico.
#pragma once

#include <cstddef>
#include <cstdint>

#include "wx/packet_framer.h"

namespace wx {

// Hardware the driver needs. Implemented with GPIO/UART on the Pico, faked in tests.
class RadioPort {
 public:
  virtual ~RadioPort() = default;
  virtual bool aux_busy() = 0;          // AUX high = module busy
  virtual void set_power(bool on) = 0;  // load switch (may be absent)
  virtual void write(const uint8_t* data, size_t n) = 0;
  virtual int read_byte() = 0;          // -1 when nothing is waiting
};

struct Dxlr22Config {
  uint8_t channel = 0x41;   // 850.15 MHz + 0x41 (65) × 1 MHz = 915.15 MHz (factory default)
  uint8_t level = 2;        // air rate: 2 = SF11/500 kHz, ~2.1 kbps (factory default)
  uint8_t power_dbm = 22;   // 0..22 (factory default 22)
  bool rssi_byte = true;    // append an RSSI byte to each received packet
};

class Dxlr22Radio {
 public:
  Dxlr22Radio(RadioPort& port, const Dxlr22Config& cfg) : port_(port), cfg_(cfg) {}

  void start(uint32_t now_ms);  // power up and configure
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
    uint32_t config_fixes = 0;   // settings found wrong and rewritten
    uint32_t power_cycles = 0;
    uint32_t timeouts = 0;
    uint32_t sent = 0;
    uint32_t received = 0;
  };
  const Stats& stats() const { return stats_; }
  uint32_t rejected() const { return framer_.rejected(); }
  uint32_t progress() const { return progress_; }

  // One-line status for logs: current step, the step that last failed,
  // counters, and the last line the module sent.
  int describe(char* out, size_t cap);

 private:
  enum class State : uint8_t {
    kPowerOff,  // load switch off, waiting
    kBoot,      // waiting for the module to start (AUX low)
    kEnterAt,   // sent "+++", waiting for "Entry AT"
    kQuery,     // sent "AT+NAME", waiting for "+NAME=value"
    kSet,       // sent "AT+NAMEvalue", waiting for "OK"
    kExitAt,    // sent "+++", waiting for "Exit AT" / "Power on" and AUX low
    kReady,
    kDead,
  };

  void enter(State s, uint32_t now_ms, uint32_t timeout_ms);
  void timeout(uint32_t now_ms);
  void begin_power_cycle(uint32_t now_ms);
  void begin_config(uint32_t now_ms);
  void send_line(const char* text);
  void send_query(uint32_t now_ms);
  void finish_item(uint32_t now_ms);
  bool read_line();  // true when line_ holds a complete line
  int wanted(int item) const;

  RadioPort& port_;
  Dxlr22Config cfg_;
  State state_ = State::kPowerOff;
  uint32_t entered_ms_ = 0;
  uint32_t deadline_ms_ = 0;
  char line_[48] = {};
  size_t line_len_ = 0;
  char last_line_[48] = {};
  int item_ = 0;            // which setting is being checked
  bool item_written_ = false;
  bool saw_exit_ = false;
  int enter_tries_ = 0;
  int failures_ = 0;        // consecutive failed bring-ups
  bool verify_only_ = false;
  bool tx_pending_ = false;
  bool tx_saw_busy_ = false;
  uint32_t tx_start_ms_ = 0;
  State last_fail_state_ = State::kPowerOff;
  PacketFramer framer_;
  Stats stats_;
  uint32_t progress_ = 0;
};

}  // namespace wx
