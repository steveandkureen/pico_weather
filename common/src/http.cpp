#include "wx/http.h"

#include <cstring>

namespace wx {
namespace {

bool ieq_prefix(const char* s, const char* prefix) {
  for (; *prefix; ++s, ++prefix) {
    char a = *s, b = *prefix;
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

const char* find_crlfcrlf(const char* s, size_t n) {
  for (size_t i = 0; i + 3 < n; ++i) {
    if (s[i] == '\r' && s[i + 1] == '\n' && s[i + 2] == '\r' && s[i + 3] == '\n') return s + i;
  }
  return nullptr;
}

}  // namespace

HttpParse http_parse(char* buf, size_t len, size_t cap, HttpRequest* req) {
  const char* hdr_end = find_crlfcrlf(buf, len);
  if (hdr_end == nullptr) return len >= cap ? HttpParse::kTooBig : HttpParse::kIncomplete;
  const size_t header_len = static_cast<size_t>(hdr_end - buf) + 4;

  // Content-Length (scan before modifying anything).
  size_t content_length = 0;
  for (const char* p = buf; p < hdr_end; ++p) {
    if ((p == buf || p[-1] == '\n') && ieq_prefix(p, "content-length:")) {
      const char* q = p + 15;
      while (*q == ' ') ++q;
      size_t v = 0;
      while (*q >= '0' && *q <= '9') {
        v = v * 10 + static_cast<size_t>(*q - '0');
        if (v > 65536) return HttpParse::kTooBig;
        ++q;
      }
      content_length = v;
    }
  }
  if (header_len + content_length > cap) return HttpParse::kTooBig;
  if (len < header_len + content_length) return HttpParse::kIncomplete;

  // Request line: METHOD SP PATH SP VERSION
  char* sp1 = static_cast<char*>(std::memchr(buf, ' ', header_len));
  if (sp1 == nullptr) return HttpParse::kBad;
  char* path = sp1 + 1;
  char* sp2 = static_cast<char*>(std::memchr(path, ' ', static_cast<size_t>(hdr_end - path)));
  if (sp2 == nullptr || path[0] != '/') return HttpParse::kBad;

  const size_t mlen = static_cast<size_t>(sp1 - buf);
  if (mlen == 3 && std::memcmp(buf, "GET", 3) == 0) {
    req->method = HttpMethod::kGet;
  } else if (mlen == 4 && std::memcmp(buf, "POST", 4) == 0) {
    req->method = HttpMethod::kPost;
  } else {
    req->method = HttpMethod::kOther;
  }

  // Host header.
  req->host = "";
  for (char* p = sp2; p < hdr_end; ++p) {
    if (p[-1] == '\n' && ieq_prefix(p, "host:")) {
      char* v = p + 5;
      while (*v == ' ') ++v;
      char* e = v;
      while (e < hdr_end && *e != '\r') ++e;
      *e = '\0';  // overwrite '\r'
      req->host = v;
      break;
    }
  }

  *sp2 = '\0';
  char* q = std::strchr(path, '?');
  if (q != nullptr) {
    *q = '\0';
    req->query = q + 1;
  } else {
    req->query = "";
  }
  req->path = path;
  req->body = buf + header_len;
  req->body_len = content_length;
  // NUL-terminate the body when there's room (forms are parsed as strings too).
  if (header_len + content_length < cap) buf[header_len + content_length] = '\0';
  return HttpParse::kOk;
}

bool form_get(const char* form, size_t len, const char* key, char* out, size_t cap) {
  const size_t klen = std::strlen(key);
  size_t i = 0;
  while (i < len) {
    size_t start = i;
    while (i < len && form[i] != '&') ++i;
    const size_t end = i;
    ++i;  // skip '&'
    const char* eq = static_cast<const char*>(std::memchr(form + start, '=', end - start));
    const size_t name_len = eq ? static_cast<size_t>(eq - (form + start)) : end - start;
    if (name_len != klen || std::memcmp(form + start, key, klen) != 0) continue;
    size_t o = 0;
    if (eq != nullptr) {
      for (const char* p = eq + 1; p < form + end; ++p) {
        char c = *p;
        if (c == '+') {
          c = ' ';
        } else if (c == '%' && p + 2 < form + end && hexval(p[1]) >= 0 && hexval(p[2]) >= 0) {
          c = static_cast<char>(hexval(p[1]) * 16 + hexval(p[2]));
          p += 2;
        }
        if (o + 1 < cap) out[o++] = c;
      }
    }
    if (cap > 0) out[o] = '\0';
    return true;
  }
  return false;
}

bool parse_fixed(const char* s, int decimals, int32_t* out) {
  bool neg = false;
  if (*s == '-') {
    neg = true;
    ++s;
  } else if (*s == '+') {
    ++s;
  }
  if (*s < '0' || *s > '9') {
    if (!(*s == '.' && s[1] >= '0' && s[1] <= '9')) return false;
  }
  int64_t v = 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    if (v > 2000000000LL) return false;
    ++s;
  }
  int d = 0;
  if (*s == '.') {
    ++s;
    while (*s >= '0' && *s <= '9') {
      if (d < decimals) {
        v = v * 10 + (*s - '0');
        ++d;
      }
      ++s;
    }
  }
  while (*s == ' ') ++s;
  if (*s != '\0') return false;
  for (; d < decimals; ++d) v *= 10;
  if (v > 2147483647LL) return false;
  *out = static_cast<int32_t>(neg ? -v : v);
  return true;
}

bool form_get_int(const char* form, size_t len, const char* key, int32_t* out) {
  char tmp[16];
  if (!form_get(form, len, key, tmp, sizeof(tmp))) return false;
  return parse_fixed(tmp, 0, out);
}

}  // namespace wx
