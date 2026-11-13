#include "wx/e220.h"

#include <cstring>

namespace wx {
namespace {

// ⚠ Timings from the E220 manual; verify on hardware.
constexpr uint32_t kPowerOffMs = 500;      // load switch off long enough to fully reset
constexpr uint32_t kPowerOnTimeoutMs = 2000;
constexpr uint32_t kModeTimeoutMs = 1000;  // AUX must go high after a mode change
constexpr uint32_t kSettleMs = 5;          // manual: wait ≥2 ms after AUX rises
constexpr uint32_t kReplyTimeoutMs = 500;
constexpr uint32_t kTxTimeoutMs = 3000;    // a 41-byte packet at 2.4 kbps takes ~0.3 s
constexpr uint32_t kDeadRetryMs = 5 * 60 * 1000;
constexpr int kMaxFailures = 4;            // 1 reconfig + 3 power cycles, then rest

constexpr uint8_t kRegCount = 8;

bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }

}  // namespace

void E220Config::to_regs(uint8_t r[8]) const {
  r[0] = static_cast<uint8_t>(address >> 8);
  r[1] = static_cast<uint8_t>(address);
  // REG0: UART 9600 (011) | 8N1 (00) | air data rate
  r[2] = static_cast<uint8_t>((0x3u << 5) | (air_rate & 0x7u));
  // REG1: 200-byte sub-packets (00) | ambient RSSI off | power
  r[3] = static_cast<uint8_t>(power & 0x3u);
  // REG2: channel
  r[4] = channel;
  // REG3: RSSI byte | transparent mode (0) | LBT | WOR cycle 2000 ms (011, unused)
  r[5] = static_cast<uint8_t>((rssi_byte ? 0x80u : 0u) | (lbt ? 0x10u : 0u) | 0x03u);
  r[6] = 0;  // CRYPT_H
  r[7] = 0;  // CRYPT_L
}

bool E220Config::matches_regs(const uint8_t r[8]) const {
  uint8_t want[8];
  to_regs(want);
  // Compare ADDH..REG3. Reserved bits in REG1/REG3 may read back differently,
  // so only the fields we set are compared there.
  return r[0] == want[0] && r[1] == want[1] && r[2] == want[2] && (r[3] & 0x03) == (want[3] & 0x03) &&
         r[4] == want[4] && (r[5] & 0x97) == (want[5] & 0x97);
}

size_t e220_write_cmd(const E220Config& cfg, uint8_t* out) {
  out[0] = 0xC0;
  out[1] = 0x00;
  out[2] = kRegCount;
  cfg.to_regs(out + 3);
  return 3 + kRegCount;
}

size_t e220_read_cmd(uint8_t* out) {
  out[0] = 0xC1;
  out[1] = 0x00;
  out[2] = kRegCount;
  return 3;
}

void E220Radio::enter(State s, uint32_t now_ms, uint32_t timeout_ms) {
  state_ = s;
  entered_ms_ = now_ms;
  deadline_ms_ = now_ms + timeout_ms;
  aux_seen_ = false;
  reply_len_ = 0;
}

void E220Radio::start(uint32_t now_ms) {
  failures_ = 0;
  begin_power_cycle(now_ms);
}

void E220Radio::begin_power_cycle(uint32_t now_ms) {
  port_.set_power(false);
  port_.set_mode_pins(true, true);  // come up in configuration mode
  tx_pending_ = false;
  verify_only_ = false;
  enter(State::kPowerOff, now_ms, kPowerOffMs);
}

void E220Radio::begin_config(uint32_t now_ms) {
  port_.set_mode_pins(true, true);
  tx_pending_ = false;
  wrote_ = false;
  enter(State::kConfigMode, now_ms, kModeTimeoutMs);
}

void E220Radio::recover(int level, uint32_t now_ms) {
  failures_ = 0;
  if (level >= 2) {
    ++stats_.power_cycles;
    begin_power_cycle(now_ms);
  } else {
    verify_only_ = false;
    begin_config(now_ms);
  }
}

void E220Radio::verify(uint32_t now_ms) {
  if (state_ != State::kReady || tx_pending_) return;
  verify_only_ = true;
  begin_config(now_ms);
}

void E220Radio::timeout(uint32_t now_ms) {
  ++stats_.timeouts;
  ++failures_;
  if (failures_ == 1) {
    begin_config(now_ms);
  } else if (failures_ <= kMaxFailures) {
    ++stats_.power_cycles;
    begin_power_cycle(now_ms);
  } else {
    // Stop hammering a module that won't answer; try again later. The app
    // shows a problem; it does NOT reboot, so a dead radio can't cause a
    // reboot loop that also takes down the web page.
    port_.set_power(false);
    enter(State::kDead, now_ms, kDeadRetryMs);
  }
}

bool E220Radio::collect_reply(uint8_t* regs) {
  int b;
  while ((b = port_.read_byte()) >= 0) {
    if (reply_len_ < sizeof(reply_)) reply_[reply_len_++] = static_cast<uint8_t>(b);
  }
  if (reply_len_ < 3 + kRegCount) return false;
  if (reply_[0] != 0xC1 || reply_[1] != 0x00 || reply_[2] != kRegCount) {
    reply_len_ = 0;
    return false;
  }
  std::memcpy(regs, reply_ + 3, kRegCount);
  reply_len_ = 0;
  return true;
}

void E220Radio::poll(uint32_t now_ms) {
  ++progress_;
  switch (state_) {
    case State::kPowerOff:
      if (!reached(now_ms, deadline_ms_)) return;
      port_.set_power(true);
      enter(State::kPowerOnWait, now_ms, kPowerOnTimeoutMs);
      return;

    case State::kPowerOnWait:
      // AUX stays low while the module boots, then goes high.
      if (port_.aux_high() && reached(now_ms, entered_ms_ + 50)) {
        wrote_ = false;
        enter(State::kConfigMode, now_ms, kModeTimeoutMs);
        return;
      }
      if (reached(now_ms, deadline_ms_)) timeout(now_ms);
      return;

    case State::kConfigMode:
    case State::kNormalMode: {
      if (!aux_seen_) {
        if (port_.aux_high()) {
          aux_seen_ = true;
          settle_until_ms_ = now_ms + kSettleMs;
        } else if (reached(now_ms, deadline_ms_)) {
          timeout(now_ms);
        }
        return;
      }
      if (!reached(now_ms, settle_until_ms_)) return;
      if (state_ == State::kNormalMode) {
        failures_ = 0;
        if (!verify_only_) ++stats_.configs;
        verify_only_ = false;
        state_ = State::kReady;
        return;
      }
      while (port_.read_byte() >= 0) {
      }
      uint8_t cmd[3];
      port_.write(cmd, e220_read_cmd(cmd));
      enter(State::kReadCmd, now_ms, kReplyTimeoutMs);
      return;
    }

    case State::kReadCmd: {
      uint8_t regs[kRegCount];
      if (collect_reply(regs)) {
        if (cfg_.matches_regs(regs)) {
          port_.set_mode_pins(false, false);
          enter(State::kNormalMode, now_ms, kModeTimeoutMs);
        } else if (!wrote_) {
          ++stats_.config_fixes;
          uint8_t cmd[11];
          port_.write(cmd, e220_write_cmd(cfg_, cmd));
          wrote_ = true;
          enter(State::kWriteCmd, now_ms, kReplyTimeoutMs);
        } else {
          timeout(now_ms);  // wrote the settings but they didn't stick
        }
        return;
      }
      if (reached(now_ms, deadline_ms_)) timeout(now_ms);
      return;
    }

    case State::kWriteCmd: {
      uint8_t regs[kRegCount];
      if (collect_reply(regs)) {
        // Read back to confirm the module really stored them.
        uint8_t cmd[3];
        port_.write(cmd, e220_read_cmd(cmd));
        enter(State::kReadCmd, now_ms, kReplyTimeoutMs);
        return;
      }
      if (reached(now_ms, deadline_ms_)) timeout(now_ms);
      return;
    }

    case State::kReady: {
      int b;
      while ((b = port_.read_byte()) >= 0) framer_.push(static_cast<uint8_t>(b), now_ms);
      if (tx_pending_) {
        const bool aux = port_.aux_high();
        if (!aux) tx_saw_busy_ = true;
        if (aux && (tx_saw_busy_ || reached(now_ms, tx_deadline_ms_ - kTxTimeoutMs + 500))) {
          tx_pending_ = false;
          ++stats_.sent;
        } else if (reached(now_ms, tx_deadline_ms_)) {
          tx_pending_ = false;
          timeout(now_ms);  // stuck busy
        }
      }
      return;
    }

    case State::kDead:
      if (reached(now_ms, deadline_ms_)) {
        failures_ = 0;
        ++stats_.power_cycles;
        begin_power_cycle(now_ms);
      }
      return;
  }
}

bool E220Radio::send(const uint8_t* data, size_t n, uint32_t now_ms) {
  if (state_ != State::kReady || tx_pending_ || !port_.aux_high()) return false;
  port_.write(data, n);
  tx_pending_ = true;
  tx_saw_busy_ = false;
  tx_deadline_ms_ = now_ms + kTxTimeoutMs;
  return true;
}

bool E220Radio::receive(uint32_t now_ms, OutdoorReading* out, int16_t* rssi_dbm) {
  if (state_ != State::kReady) return false;
  if (framer_.poll(now_ms, out, rssi_dbm)) {
    ++stats_.received;
    return true;
  }
  return false;
}

}  // namespace wx
