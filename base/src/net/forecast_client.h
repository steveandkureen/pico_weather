// Fetches the Open-Meteo forecast over plain HTTP, as a non-blocking state
// machine: DNS lookup → connect → send → read into a fixed buffer → parse.
// Every step has a deadline; a failure never affects anything else.
#pragma once

#include <cstddef>
#include <cstdint>

#include "wx/forecast.h"

namespace wxb {

class ForecastClient {
 public:
  static constexpr uint32_t kIntervalMs = 30 * 60 * 1000;
  static constexpr uint32_t kStepTimeoutMs = 15000;
  static constexpr size_t kBufSize = 4096;

  // Fetch now (e.g. location changed). Ignored if a fetch is running.
  void request_now() { due_ms_valid_ = false; }

  // Call every loop. `online` = WiFi up with an IP. Returns true once each
  // time a new forecast has been parsed into `out`.
  bool poll(uint32_t now_ms, bool online, bool have_location, int32_t lat_e5, int32_t lon_e5,
            wx::Forecast* out);

  uint32_t successes() const { return ok_; }
  uint32_t failures() const { return failed_; }
  const char* last_error() const { return last_error_; }

  // lwIP callbacks (public so the C callbacks can reach them).
  void on_dns(bool ok, uint32_t ip);
  void on_connected();
  void on_data(const uint8_t* d, size_t n);
  void on_closed();
  void on_error();

 private:
  enum class State : uint8_t { kIdle, kDns, kConnecting, kReceiving, kDone, kFailed };

  void fail(const char* why, uint32_t now_ms);
  void cleanup();
  void schedule_retry(uint32_t now_ms);

  State state_ = State::kIdle;
  uint32_t due_ms_ = 0;
  bool due_ms_valid_ = false;
  uint32_t step_deadline_ms_ = 0;
  uint32_t retry_backoff_ms_ = 0;
  void* pcb_ = nullptr;
  uint32_t ip_ = 0;
  char path_[512];
  char buf_[kBufSize + 1];
  size_t len_ = 0;
  bool overflow_ = false;
  uint32_t ok_ = 0, failed_ = 0;
  const char* last_error_ = "";
};

}  // namespace wxb
