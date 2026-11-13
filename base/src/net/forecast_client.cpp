#include "net/forecast_client.h"

#include <cstdio>
#include <cstring>

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/rand.h"

namespace wxb {
namespace {

constexpr const char* kHost = "api.open-meteo.com";
constexpr uint16_t kPort = 80;

ForecastClient* self(void* arg) { return static_cast<ForecastClient*>(arg); }

void dns_cb(const char*, const ip_addr_t* addr, void* arg) {
  self(arg)->on_dns(addr != nullptr, addr ? ip4_addr_get_u32(ip_2_ip4(addr)) : 0);
}

err_t recv_cb(void* arg, tcp_pcb* pcb, pbuf* p, err_t err) {
  if (arg == nullptr) {
    if (p) pbuf_free(p);
    return ERR_OK;
  }
  if (p == nullptr || err != ERR_OK) {
    if (p) pbuf_free(p);
    self(arg)->on_closed();
    return ERR_OK;
  }
  for (pbuf* q = p; q != nullptr; q = q->next) {
    self(arg)->on_data(static_cast<const uint8_t*>(q->payload), q->len);
  }
  tcp_recved(pcb, p->tot_len);
  pbuf_free(p);
  return ERR_OK;
}

err_t connected_cb(void* arg, tcp_pcb*, err_t err) {
  if (arg == nullptr) return ERR_OK;
  if (err != ERR_OK) {
    self(arg)->on_error();
  } else {
    self(arg)->on_connected();
  }
  return ERR_OK;
}

void err_cb(void* arg, err_t) {
  if (arg != nullptr) self(arg)->on_error();  // pcb already freed by lwIP
}

bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }

}  // namespace

void ForecastClient::cleanup() {
  if (pcb_ != nullptr) {
    tcp_pcb* pcb = static_cast<tcp_pcb*>(pcb_);
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_err(pcb, nullptr);
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    pcb_ = nullptr;
  }
}

void ForecastClient::schedule_retry(uint32_t now_ms) {
  // 5, 10, 20 min, then every 20 min; plus jitter so we never hammer the API.
  retry_backoff_ms_ = retry_backoff_ms_ == 0 ? 5 * 60 * 1000 : retry_backoff_ms_ * 2;
  if (retry_backoff_ms_ > 20 * 60 * 1000) retry_backoff_ms_ = 20 * 60 * 1000;
  due_ms_ = now_ms + retry_backoff_ms_ + get_rand_32() % 30000;
  due_ms_valid_ = true;
}

void ForecastClient::fail(const char* why, uint32_t now_ms) {
  cleanup();
  last_error_ = why;
  ++failed_;
  state_ = State::kIdle;
  schedule_retry(now_ms);
}

void ForecastClient::on_dns(bool ok, uint32_t ip) {
  if (state_ != State::kDns) return;
  if (!ok) {
    state_ = State::kFailed;
    last_error_ = "DNS lookup failed";
    return;
  }
  ip_ = ip;
  tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (pcb == nullptr) {
    state_ = State::kFailed;
    last_error_ = "out of sockets";
    return;
  }
  pcb_ = pcb;
  tcp_arg(pcb, this);
  tcp_recv(pcb, recv_cb);
  tcp_err(pcb, err_cb);
  ip_addr_t addr;
  ip_addr_set_ip4_u32(&addr, ip_);
  if (tcp_connect(pcb, &addr, kPort, connected_cb) != ERR_OK) {
    state_ = State::kFailed;
    last_error_ = "connect failed";
    return;
  }
  state_ = State::kConnecting;
}

void ForecastClient::on_connected() {
  if (state_ != State::kConnecting || pcb_ == nullptr) return;
  char req[700];
  int n = std::snprintf(req, sizeof(req),
                        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: pico-weather/1.0\r\n"
                        "Accept: application/json\r\nConnection: close\r\n\r\n",
                        path_, kHost);
  tcp_pcb* pcb = static_cast<tcp_pcb*>(pcb_);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(req) ||
      tcp_write(pcb, req, static_cast<u16_t>(n), TCP_WRITE_FLAG_COPY) != ERR_OK) {
    state_ = State::kFailed;
    last_error_ = "send failed";
    return;
  }
  tcp_output(pcb);
  len_ = 0;
  overflow_ = false;
  state_ = State::kReceiving;
}

void ForecastClient::on_data(const uint8_t* d, size_t n) {
  if (state_ != State::kReceiving) return;
  if (len_ + n > kBufSize) {
    overflow_ = true;  // keep reading so the connection closes normally
    return;
  }
  std::memcpy(buf_ + len_, d, n);
  len_ += n;
}

void ForecastClient::on_closed() {
  if (state_ == State::kReceiving) state_ = State::kDone;
}

void ForecastClient::on_error() {
  pcb_ = nullptr;  // lwIP freed it
  if (state_ == State::kReceiving) {
    state_ = State::kDone;  // a reset after the body arrived is fine
  } else if (state_ != State::kIdle) {
    state_ = State::kFailed;
    last_error_ = "connection error";
  }
}

bool ForecastClient::poll(uint32_t now_ms, bool online, bool have_location, int32_t lat_e5,
                          int32_t lon_e5, wx::Forecast* out) {
  switch (state_) {
    case State::kIdle: {
      if (!online || !have_location) return false;
      if (due_ms_valid_ && !reached(now_ms, due_ms_)) return false;
      if (wx::forecast_request_path(lat_e5, lon_e5, path_, sizeof(path_)) == 0) {
        fail("bad location", now_ms);
        return false;
      }
      state_ = State::kDns;
      step_deadline_ms_ = now_ms + kStepTimeoutMs;
      ip_addr_t addr;
      const err_t e = dns_gethostbyname(kHost, &addr, dns_cb, this);
      if (e == ERR_OK) {
        on_dns(true, ip4_addr_get_u32(ip_2_ip4(&addr)));  // cached
      } else if (e != ERR_INPROGRESS) {
        fail("DNS unavailable", now_ms);
      }
      return false;
    }
    case State::kDns:
    case State::kConnecting:
    case State::kReceiving:
      if (!online) {
        fail("WiFi went down", now_ms);
      } else if (reached(now_ms, step_deadline_ms_)) {
        fail("timed out", now_ms);
      }
      return false;
    case State::kFailed:
      fail(last_error_, now_ms);
      return false;
    case State::kDone: {
      cleanup();
      state_ = State::kIdle;
      buf_[len_] = '\0';
      if (overflow_) {
        fail("reply too large", now_ms);
        return false;
      }
      const char* body = std::strstr(buf_, "\r\n\r\n");
      if (std::strncmp(buf_, "HTTP/1.", 7) != 0 || std::strncmp(buf_ + 8, " 200", 4) != 0 || body == nullptr) {
        fail("bad HTTP status", now_ms);
        return false;
      }
      body += 4;
      wx::Forecast f;
      if (!wx::forecast_parse(body, len_ - static_cast<size_t>(body - buf_), &f)) {
        fail("could not parse reply", now_ms);
        return false;
      }
      *out = f;
      ++ok_;
      last_error_ = "";
      retry_backoff_ms_ = 0;
      due_ms_ = now_ms + kIntervalMs + get_rand_32() % 60000;
      due_ms_valid_ = true;
      return true;
    }
  }
  return false;
}

}  // namespace wxb
