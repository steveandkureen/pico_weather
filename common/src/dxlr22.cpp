#include "wx/dxlr22.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wx {
namespace {

constexpr uint32_t kPowerOffMs = 500;        // load switch off long enough to fully reset
constexpr uint32_t kBootMinMs = 500;         // after power-up, before talking to it
constexpr uint32_t kBootTimeoutMs = 3000;    // AUX must be low (idle) by then
constexpr uint32_t kReplyTimeoutMs = 1000;   // measured replies arrive within ~50 ms
constexpr uint32_t kRestartTimeoutMs = 3000; // leaving AT mode restarts the module
constexpr uint32_t kRestartMinMs = 200;
constexpr uint32_t kTxTimeoutMs = 3000;      // 35 bytes at ~2.1 kbps is ~0.3 s on air
constexpr uint32_t kTxNoBusyMs = 500;        // never saw AUX go high: assume it went
constexpr uint32_t kDeadRetryMs = 5 * 60 * 1000;
constexpr int kMaxFailures = 4;              // 1 reconfig + 3 power cycles, then rest
constexpr int kMaxEnterTries = 3;

bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }

// Settings checked on every configuration pass, in this order.
struct Item {
  const char* name;
  bool hex;  // value is written and reported in hex (channel)
};
constexpr Item kItems[] = {
    {"MODE", false},     // 0 = transparent
    {"SLEEP", false},    // 2 = always listening ("high aging")
    {"SWITCH", false},   // 0 = M0/M1 ignored
    {"LEVEL", false},
    {"CHANNEL", true},
    {"POWE", false},
    {"DRSSI", false},
};
constexpr int kItemCount = sizeof(kItems) / sizeof(kItems[0]);

const char* state_name(int s) {
  static const char* const kNames[] = {"power-off", "boot", "enter-at", "query",
                                       "set",       "exit-at", "ready", "dead"};
  return s >= 0 && s < 8 ? kNames[s] : "?";
}

}  // namespace

int Dxlr22Radio::wanted(int item) const {
  switch (item) {
    case 0: return 0;
    case 1: return 2;
    case 2: return 0;
    case 3: return cfg_.level;
    case 4: return cfg_.channel;
    case 5: return cfg_.power_dbm;
    case 6: return cfg_.rssi_byte ? 1 : 0;
    default: return 0;
  }
}

void Dxlr22Radio::enter(State s, uint32_t now_ms, uint32_t timeout_ms) {
  state_ = s;
  entered_ms_ = now_ms;
  deadline_ms_ = now_ms + timeout_ms;
}

void Dxlr22Radio::start(uint32_t now_ms) {
  failures_ = 0;
  begin_power_cycle(now_ms);
}

void Dxlr22Radio::begin_power_cycle(uint32_t now_ms) {
  port_.set_power(false);
  tx_pending_ = false;
  verify_only_ = false;
  enter(State::kPowerOff, now_ms, kPowerOffMs);
}

void Dxlr22Radio::begin_config(uint32_t now_ms) {
  tx_pending_ = false;
  enter_tries_ = 0;
  // Start from "booting": waits for AUX idle, then sends "+++".
  enter(State::kBoot, now_ms - kBootMinMs, kBootTimeoutMs);
}

void Dxlr22Radio::recover(int level, uint32_t now_ms) {
  failures_ = 0;
  if (level >= 2) {
    ++stats_.power_cycles;
    begin_power_cycle(now_ms);
  } else {
    verify_only_ = false;
    begin_config(now_ms);
  }
}

void Dxlr22Radio::verify(uint32_t now_ms) {
  if (state_ != State::kReady || tx_pending_) return;
  verify_only_ = true;
  begin_config(now_ms);
}

void Dxlr22Radio::timeout(uint32_t now_ms) {
  last_fail_state_ = state_;
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

void Dxlr22Radio::send_line(const char* text) {
  port_.write(reinterpret_cast<const uint8_t*>(text), std::strlen(text));
  static const uint8_t kCrLf[2] = {'\r', '\n'};
  port_.write(kCrLf, 2);
}

bool Dxlr22Radio::read_line() {
  int b;
  while ((b = port_.read_byte()) >= 0) {
    if (b == '\n') {
      line_[line_len_] = '\0';
      line_len_ = 0;
      if (line_[0] == '\0') continue;  // blank line
      std::memcpy(last_line_, line_, sizeof(last_line_));
      return true;
    }
    if (b == '\r') continue;
    if (line_len_ + 1 < sizeof(line_)) {
      line_[line_len_++] = static_cast<char>(b);
    } else {
      line_len_ = 0;  // garbage: start over
    }
  }
  return false;
}

void Dxlr22Radio::send_query(uint32_t now_ms) {
  char cmd[24];
  std::snprintf(cmd, sizeof(cmd), "AT+%s", kItems[item_].name);
  send_line(cmd);
  enter(State::kQuery, now_ms, kReplyTimeoutMs);
}

void Dxlr22Radio::finish_item(uint32_t now_ms) {
  ++item_;
  item_written_ = false;
  if (item_ < kItemCount) {
    send_query(now_ms);
    return;
  }
  // All checked: leave AT mode. The module restarts with the settings.
  saw_exit_ = false;
  send_line("+++");
  enter(State::kExitAt, now_ms, kRestartTimeoutMs);
}

void Dxlr22Radio::poll(uint32_t now_ms) {
  ++progress_;
  switch (state_) {
    case State::kPowerOff:
      if (!reached(now_ms, deadline_ms_)) return;
      port_.set_power(true);
      enter(State::kBoot, now_ms, kBootTimeoutMs);
      return;

    case State::kBoot:
      while (read_line()) {
      }  // discard "Power on" and anything else
      if (reached(now_ms, entered_ms_ + kBootMinMs) && !port_.aux_busy()) {
        line_len_ = 0;
        send_line("+++");
        enter(State::kEnterAt, now_ms, kReplyTimeoutMs);
        return;
      }
      if (reached(now_ms, deadline_ms_)) timeout(now_ms);
      return;

    case State::kEnterAt:
      while (read_line()) {
        if (std::strcmp(line_, "Entry AT") == 0) {
          item_ = -1;
          finish_item(now_ms);  // → first query
          return;
        }
        if (std::strcmp(line_, "Exit AT") == 0) {
          // It was already in AT mode (e.g. the Pico restarted mid-config)
          // and has now left it and is restarting. Try again once it's up.
          if (++enter_tries_ >= kMaxEnterTries) break;
          enter(State::kBoot, now_ms, kBootTimeoutMs);
          return;
        }
      }
      if (enter_tries_ >= kMaxEnterTries || reached(now_ms, deadline_ms_)) timeout(now_ms);
      return;

    case State::kQuery: {
      char prefix[24];
      const int plen = std::snprintf(prefix, sizeof(prefix), "+%s=", kItems[item_].name);
      while (read_line()) {
        if (std::strncmp(line_, "ERROR", 5) == 0) {
          timeout(now_ms);
          return;
        }
        if (std::strncmp(line_, prefix, static_cast<size_t>(plen)) != 0) continue;  // e.g. a stray "OK"
        char* end = nullptr;
        const long v = std::strtol(line_ + plen, &end, kItems[item_].hex ? 16 : 10);
        if (end == line_ + plen) {  // no number
          timeout(now_ms);
          return;
        }
        if (v == wanted(item_)) {
          finish_item(now_ms);
          return;
        }
        if (item_written_) {  // wrote it, but it didn't stick
          timeout(now_ms);
          return;
        }
        char cmd[32];
        std::snprintf(cmd, sizeof(cmd), kItems[item_].hex ? "AT+%s%02X" : "AT+%s%d", kItems[item_].name,
                      wanted(item_));
        send_line(cmd);
        ++stats_.config_fixes;
        item_written_ = true;
        enter(State::kSet, now_ms, kReplyTimeoutMs);
        return;
      }
      if (reached(now_ms, deadline_ms_)) timeout(now_ms);
      return;
    }

    case State::kSet:
      while (read_line()) {
        if (std::strcmp(line_, "OK") == 0) {
          send_query(now_ms);  // read it back
          return;
        }
        if (std::strncmp(line_, "ERROR", 5) == 0) {
          timeout(now_ms);
          return;
        }
      }
      if (reached(now_ms, deadline_ms_)) timeout(now_ms);
      return;

    case State::kExitAt:
      while (read_line()) {
        if (std::strcmp(line_, "Exit AT") == 0 || std::strcmp(line_, "Power on") == 0) saw_exit_ = true;
      }
      if (saw_exit_ && reached(now_ms, entered_ms_ + kRestartMinMs) && !port_.aux_busy()) {
        failures_ = 0;
        if (!verify_only_) ++stats_.configs;
        verify_only_ = false;
        state_ = State::kReady;
        return;
      }
      if (reached(now_ms, deadline_ms_)) timeout(now_ms);
      return;

    case State::kReady: {
      int b;
      while ((b = port_.read_byte()) >= 0) framer_.push(static_cast<uint8_t>(b), now_ms);
      if (tx_pending_) {
        const bool busy = port_.aux_busy();
        if (busy) tx_saw_busy_ = true;
        if (!busy && (tx_saw_busy_ || reached(now_ms, tx_start_ms_ + kTxNoBusyMs))) {
          tx_pending_ = false;
          ++stats_.sent;
        } else if (reached(now_ms, tx_start_ms_ + kTxTimeoutMs)) {
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

bool Dxlr22Radio::send(const uint8_t* data, size_t n, uint32_t now_ms) {
  if (state_ != State::kReady || tx_pending_ || port_.aux_busy()) return false;
  port_.write(data, n);
  tx_pending_ = true;
  tx_saw_busy_ = false;
  tx_start_ms_ = now_ms;
  return true;
}

bool Dxlr22Radio::receive(uint32_t now_ms, OutdoorReading* out, int16_t* rssi_dbm) {
  if (state_ != State::kReady) return false;
  if (framer_.poll(now_ms, out, rssi_dbm)) {
    ++stats_.received;
    return true;
  }
  return false;
}

int Dxlr22Radio::describe(char* out, size_t cap) {
  return std::snprintf(out, cap,
                       "radio: %s, aux=%s, timeouts=%lu (last in %s), configs=%lu fixes=%lu "
                       "power_cycles=%lu sent=%lu last line: \"%s\"",
                       state_name(static_cast<int>(state_)), port_.aux_busy() ? "busy" : "idle",
                       (unsigned long)stats_.timeouts, state_name(static_cast<int>(last_fail_state_)),
                       (unsigned long)stats_.configs, (unsigned long)stats_.config_fixes,
                       (unsigned long)stats_.power_cycles, (unsigned long)stats_.sent, last_line_);
}

}  // namespace wx
