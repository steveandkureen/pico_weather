// Small HTTP/1.0-style server on lwIP's raw TCP API.
//
// * Fixed pool of connection slots, fixed buffers: nothing is allocated.
// * One request per connection ("Connection: close").
// * Static assets are sent straight from flash (no copy); dynamic bodies are
//   copied into lwIP as they go out.
// * Idle or stuck connections are closed after a timeout.
// Everything runs from the main loop (lwIP poll mode). One server per program.
#pragma once

#include <cstddef>
#include <cstdint>

#include "wx/http.h"

namespace wxb {

struct HttpResponse {
  int status = 200;
  const char* content_type = "text/plain; charset=utf-8";
  const char* location = nullptr;   // for redirects
  bool gzip = false;
  // Body: either static (points into flash, sent without copying) ...
  const uint8_t* static_body = nullptr;
  size_t static_len = 0;
  // ... or dynamic, written by the handler into `dyn` (kTxSize bytes).
  char* dyn = nullptr;
  size_t dyn_cap = 0;
  size_t dyn_len = 0;
};

class HttpHandler {
 public:
  virtual ~HttpHandler() = default;
  // `local_ip` is the address (network byte order) the request came in on,
  // which tells the home network from the setup network.
  virtual void handle(const wx::HttpRequest& req, uint32_t local_ip, HttpResponse& resp) = 0;
};

namespace http {

constexpr int kSlots = 4;
constexpr size_t kRxSize = 2048;
constexpr size_t kTxSize = 6144;
constexpr uint32_t kTimeoutMs = 10000;

bool start(HttpHandler* handler, uint16_t port);
void poll(uint32_t now_ms);

uint32_t requests();
uint32_t rejected();
int active();

}  // namespace http
}  // namespace wxb
