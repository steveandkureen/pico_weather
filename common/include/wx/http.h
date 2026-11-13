// Minimal HTTP/1.x request parsing for the base station's web server.
// Works on a fixed receive buffer; nothing is allocated.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wx {

enum class HttpMethod : uint8_t { kGet, kPost, kOther };

struct HttpRequest {
  HttpMethod method;
  const char* path;     // NUL-terminated inside the caller's buffer
  const char* query;    // after '?', or "" if none
  const char* host;     // Host header value, or ""
  const char* body;
  size_t body_len;
};

enum class HttpParse : uint8_t { kIncomplete, kOk, kBad, kTooBig };

// Parses the request in buf[0..len). Writes NULs into buf to terminate the
// strings it points at, so buf must be writable and only parsed once it's
// complete (call again with more data while it returns kIncomplete;
// kIncomplete never modifies buf).
HttpParse http_parse(char* buf, size_t len, size_t cap, HttpRequest* req);

// Finds `key` in an application/x-www-form-urlencoded string (or a query
// string) and URL-decodes its value into out. Returns false if absent.
bool form_get(const char* form, size_t len, const char* key, char* out, size_t cap);

// Convenience: parse an integer field. Returns false if absent or not a number.
bool form_get_int(const char* form, size_t len, const char* key, int32_t* out);

// Parses a decimal like "-104.98765" into value × 10^decimals.
bool parse_fixed(const char* s, int decimals, int32_t* out);

}  // namespace wx
