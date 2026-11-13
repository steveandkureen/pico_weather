#include "net/http_server.h"

#include <cstdio>
#include <cstring>

#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/time.h"

namespace wxb {
namespace http {
namespace {

enum class State : uint8_t { kFree, kReading, kWriting, kClosing };

struct Conn {
  State state = State::kFree;
  tcp_pcb* pcb = nullptr;
  uint32_t last_ms = 0;
  size_t rx_len = 0;
  char rx[kRxSize + 1];
  // Pending output: header (copied), then body.
  char header[320];
  size_t header_len = 0;
  size_t header_sent = 0;
  const uint8_t* body = nullptr;
  size_t body_len = 0;
  size_t body_sent = 0;
  bool body_copy = false;
  char tx[kTxSize];
};

Conn g_conns[kSlots];
HttpHandler* g_handler = nullptr;
tcp_pcb* g_listen = nullptr;
uint32_t g_requests = 0;
uint32_t g_rejected = 0;

uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }

void release(Conn* c) {
  c->state = State::kFree;
  c->pcb = nullptr;
  c->rx_len = 0;
}

void detach(tcp_pcb* pcb) {
  tcp_arg(pcb, nullptr);
  tcp_recv(pcb, nullptr);
  tcp_sent(pcb, nullptr);
  tcp_err(pcb, nullptr);
  tcp_poll(pcb, nullptr, 0);
}

// Graceful close; if lwIP can't (out of memory) abort instead.
void close_conn(Conn* c) {
  if (c->pcb != nullptr) {
    tcp_pcb* pcb = c->pcb;
    detach(pcb);
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
  }
  release(c);
}

void abort_conn(Conn* c) {
  if (c->pcb != nullptr) {
    tcp_pcb* pcb = c->pcb;
    detach(pcb);
    tcp_abort(pcb);
  }
  release(c);
}

const char* status_text(int s) {
  switch (s) {
    case 200: return "OK";
    case 204: return "No Content";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "OK";
  }
}

// Queues as much pending output as lwIP will take. Closes when done.
void pump(Conn* c) {
  if (c->state != State::kWriting || c->pcb == nullptr) return;
  tcp_pcb* pcb = c->pcb;
  while (c->header_sent < c->header_len) {
    const u16_t room = tcp_sndbuf(pcb);
    if (room == 0) break;
    size_t n = c->header_len - c->header_sent;
    if (n > room) n = room;
    if (tcp_write(pcb, c->header + c->header_sent, static_cast<u16_t>(n), TCP_WRITE_FLAG_COPY) != ERR_OK) break;
    c->header_sent += n;
  }
  while (c->header_sent == c->header_len && c->body_sent < c->body_len) {
    const u16_t room = tcp_sndbuf(pcb);
    if (room == 0) break;
    size_t n = c->body_len - c->body_sent;
    if (n > room) n = room;
    if (n > 1460) n = 1460;
    const u8_t flags = (c->body_copy ? TCP_WRITE_FLAG_COPY : 0) |
                       (c->body_sent + n < c->body_len ? TCP_WRITE_FLAG_MORE : 0);
    if (tcp_write(pcb, c->body + c->body_sent, static_cast<u16_t>(n), flags) != ERR_OK) break;
    c->body_sent += n;
  }
  tcp_output(pcb);
  if (c->header_sent == c->header_len && c->body_sent == c->body_len) {
    // Everything is queued. Static bodies live in flash, dynamic ones were
    // copied, so closing now is safe: lwIP finishes sending, then sends FIN.
    close_conn(c);
  }
}

void respond(Conn* c, const wx::HttpRequest& req) {
  HttpResponse r;
  r.dyn = c->tx;
  r.dyn_cap = sizeof(c->tx);
  const uint32_t local_ip = c->pcb ? ip4_addr_get_u32(ip_2_ip4(&c->pcb->local_ip)) : 0;
  g_handler->handle(req, local_ip, r);
  ++g_requests;

  const bool is_static = r.static_body != nullptr;
  const size_t len = is_static ? r.static_len : r.dyn_len;
  int n = std::snprintf(c->header, sizeof(c->header),
                        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n%s%s%s%s"
                        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                        r.status, status_text(r.status), r.content_type, static_cast<unsigned>(len),
                        r.gzip ? "Content-Encoding: gzip\r\n" : "", r.location ? "Location: " : "",
                        r.location ? r.location : "", r.location ? "\r\n" : "");
  if (n < 0 || static_cast<size_t>(n) >= sizeof(c->header)) n = 0;
  c->header_len = static_cast<size_t>(n);
  c->header_sent = 0;
  c->body = is_static ? r.static_body : reinterpret_cast<const uint8_t*>(c->tx);
  c->body_len = req.method == wx::HttpMethod::kOther ? 0 : len;
  c->body_sent = 0;
  c->body_copy = !is_static;
  c->state = State::kWriting;
  pump(c);
}

void respond_error(Conn* c, int status) {
  const char* msg = status_text(status);
  std::snprintf(c->header, sizeof(c->header),
                "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
                status, msg, static_cast<unsigned>(std::strlen(msg)), msg);
  c->header_len = std::strlen(c->header);
  c->header_sent = 0;
  c->body = nullptr;
  c->body_len = 0;
  c->body_sent = 0;
  c->state = State::kWriting;
  pump(c);
}

err_t on_recv(void* arg, tcp_pcb* pcb, pbuf* p, err_t err) {
  Conn* c = static_cast<Conn*>(arg);
  if (c == nullptr) {
    if (p) pbuf_free(p);
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  if (p == nullptr || err != ERR_OK) {  // peer closed
    if (p) pbuf_free(p);
    if (c->state == State::kWriting) return ERR_OK;  // let the response finish
    close_conn(c);
    return ERR_OK;
  }
  c->last_ms = now_ms();
  if (c->state == State::kReading) {
    const size_t room = kRxSize - c->rx_len;
    const size_t n = p->tot_len < room ? p->tot_len : room;
    pbuf_copy_partial(p, c->rx + c->rx_len, static_cast<u16_t>(n), 0);
    c->rx_len += n;
    c->rx[c->rx_len] = '\0';
  }
  tcp_recved(pcb, p->tot_len);
  pbuf_free(p);

  if (c->state != State::kReading) return ERR_OK;
  wx::HttpRequest req{};
  switch (wx::http_parse(c->rx, c->rx_len, kRxSize, &req)) {
    case wx::HttpParse::kIncomplete:
      return ERR_OK;
    case wx::HttpParse::kOk:
      respond(c, req);
      return ERR_OK;
    case wx::HttpParse::kTooBig:
      respond_error(c, 413);
      return ERR_OK;
    case wx::HttpParse::kBad:
      respond_error(c, 400);
      return ERR_OK;
  }
  return ERR_OK;
}

err_t on_sent(void* arg, tcp_pcb*, u16_t) {
  Conn* c = static_cast<Conn*>(arg);
  if (c != nullptr) {
    c->last_ms = now_ms();
    pump(c);
  }
  return ERR_OK;
}

void on_err(void* arg, err_t) {
  // lwIP has already freed the pcb.
  Conn* c = static_cast<Conn*>(arg);
  if (c != nullptr) release(c);
}

err_t on_poll(void* arg, tcp_pcb* pcb) {
  Conn* c = static_cast<Conn*>(arg);
  if (c == nullptr) {
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  if (now_ms() - c->last_ms > kTimeoutMs) {
    abort_conn(c);
    return ERR_ABRT;
  }
  pump(c);
  return ERR_OK;
}

err_t on_accept(void*, tcp_pcb* pcb, err_t err) {
  if (err != ERR_OK || pcb == nullptr) return ERR_VAL;
  Conn* c = nullptr;
  for (Conn& s : g_conns) {
    if (s.state == State::kFree) {
      c = &s;
      break;
    }
  }
  if (c == nullptr) {
    ++g_rejected;
    tcp_abort(pcb);
    return ERR_ABRT;
  }
  c->state = State::kReading;
  c->pcb = pcb;
  c->rx_len = 0;
  c->last_ms = now_ms();
  tcp_setprio(pcb, TCP_PRIO_MIN);
  tcp_arg(pcb, c);
  tcp_recv(pcb, on_recv);
  tcp_sent(pcb, on_sent);
  tcp_err(pcb, on_err);
  tcp_poll(pcb, on_poll, 2);  // every ~1 s
  return ERR_OK;
}

}  // namespace

bool start(HttpHandler* handler, uint16_t port) {
  g_handler = handler;
  tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (pcb == nullptr) return false;
  if (tcp_bind(pcb, IP_ANY_TYPE, port) != ERR_OK) {
    tcp_abort(pcb);
    return false;
  }
  g_listen = tcp_listen_with_backlog(pcb, kSlots);
  if (g_listen == nullptr) {
    tcp_abort(pcb);
    return false;
  }
  tcp_accept(g_listen, on_accept);
  return true;
}

void poll(uint32_t now) {
  // Backstop for connections whose lwIP poll callback never fires.
  for (Conn& c : g_conns) {
    if (c.state != State::kFree && now - c.last_ms > 3 * kTimeoutMs) abort_conn(&c);
  }
}

uint32_t requests() { return g_requests; }
uint32_t rejected() { return g_rejected; }
int active() {
  int n = 0;
  for (const Conn& c : g_conns) n += c.state != State::kFree;
  return n;
}

}  // namespace http
}  // namespace wxb
