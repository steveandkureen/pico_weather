#include "wx/json.h"

#include <cstring>

namespace wx {

// ---------------------------------------------------------------- writer

void JsonWriter::raw(const char* s, size_t n) {
  if (overflow_) return;
  if (len_ + n + 1 > cap_) {
    overflow_ = true;
    return;
  }
  std::memcpy(buf_ + len_, s, n);
  len_ += n;
  buf_[len_] = '\0';
}

void JsonWriter::raw(const char* s) { raw(s, std::strlen(s)); }

void JsonWriter::sep() {
  if (need_comma_) ch(',');
  need_comma_ = true;
}

JsonWriter& JsonWriter::begin_object() {
  sep();
  ch('{');
  need_comma_ = false;
  return *this;
}

JsonWriter& JsonWriter::end_object() {
  ch('}');
  need_comma_ = true;
  return *this;
}

JsonWriter& JsonWriter::begin_array() {
  sep();
  ch('[');
  need_comma_ = false;
  return *this;
}

JsonWriter& JsonWriter::end_array() {
  ch(']');
  need_comma_ = true;
  return *this;
}

JsonWriter& JsonWriter::key(const char* k) {
  value(k);  // writes the separator and the quoted key
  ch(':');
  need_comma_ = false;
  return *this;
}

JsonWriter& JsonWriter::value(int64_t v) {
  sep();
  char tmp[24];
  int i = 0;
  bool neg = v < 0;
  uint64_t u = neg ? static_cast<uint64_t>(-(v + 1)) + 1 : static_cast<uint64_t>(v);
  do {
    tmp[i++] = static_cast<char>('0' + u % 10);
    u /= 10;
  } while (u > 0);
  if (neg) tmp[i++] = '-';
  char out[24];
  for (int j = 0; j < i; ++j) out[j] = tmp[i - 1 - j];
  raw(out, static_cast<size_t>(i));
  return *this;
}

JsonWriter& JsonWriter::fixed(int64_t v, int decimals) {
  if (decimals <= 0) return value(v);
  sep();
  int64_t scale = 1;
  for (int i = 0; i < decimals; ++i) scale *= 10;
  bool neg = v < 0;
  uint64_t u = neg ? static_cast<uint64_t>(-(v + 1)) + 1 : static_cast<uint64_t>(v);
  uint64_t whole = u / static_cast<uint64_t>(scale);
  uint64_t frac = u % static_cast<uint64_t>(scale);
  char tmp[32];
  int i = 0;
  for (int d = 0; d < decimals; ++d) {
    tmp[i++] = static_cast<char>('0' + frac % 10);
    frac /= 10;
  }
  tmp[i++] = '.';
  do {
    tmp[i++] = static_cast<char>('0' + whole % 10);
    whole /= 10;
  } while (whole > 0);
  if (neg) tmp[i++] = '-';
  char out[32];
  for (int j = 0; j < i; ++j) out[j] = tmp[i - 1 - j];
  raw(out, static_cast<size_t>(i));
  return *this;
}

JsonWriter& JsonWriter::value(const char* s) {
  sep();
  ch('"');
  for (const char* p = s; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '"' || c == '\\') {
      ch('\\');
      ch(static_cast<char>(c));
    } else if (c < 0x20) {
      static const char kHex[] = "0123456789abcdef";
      char esc[6] = {'\\', 'u', '0', '0', kHex[c >> 4], kHex[c & 15]};
      raw(esc, 6);
    } else {
      ch(static_cast<char>(c));
    }
  }
  ch('"');
  return *this;
}

JsonWriter& JsonWriter::boolean(bool b) {
  sep();
  raw(b ? "true" : "false");
  return *this;
}

JsonWriter& JsonWriter::null() {
  sep();
  raw("null");
  return *this;
}

// ---------------------------------------------------------------- reader

bool JsonSlice::equals(const char* s) const {
  size_t len = std::strlen(s);
  return len == n && std::memcmp(p, s, n) == 0;
}

namespace {

struct Walker {
  const char* s;
  const char* end;
  JsonCallback cb;
  void* user;
  JsonSlice path[kJsonMaxDepth];
  int depth = 0;  // object levels (path length)
  int nest = 0;   // objects + arrays, limited to kJsonMaxDepth

  void ws() {
    while (s < end && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) ++s;
  }

  bool string(JsonSlice* out) {
    if (s >= end || *s != '"') return false;
    ++s;
    const char* start = s;
    while (s < end && *s != '"') {
      if (*s == '\\') {
        ++s;
        if (s >= end) return false;
      }
      ++s;
    }
    if (s >= end) return false;
    const size_t n = static_cast<size_t>(s - start);
    if (n > 0xFFFF) return false;
    out->p = start;
    out->n = static_cast<uint16_t>(n);
    ++s;
    return true;
  }

  bool number(int64_t* milli) {
    bool neg = false;
    if (s < end && *s == '-') {
      neg = true;
      ++s;
    }
    if (s >= end || *s < '0' || *s > '9') return false;
    int64_t whole = 0;
    while (s < end && *s >= '0' && *s <= '9') {
      if (whole < 100000000000000LL) whole = whole * 10 + (*s - '0');
      ++s;
    }
    int64_t frac = 0;
    int fd = 0;
    if (s < end && *s == '.') {
      ++s;
      while (s < end && *s >= '0' && *s <= '9') {
        if (fd < 3) {
          frac = frac * 10 + (*s - '0');
          ++fd;
        }
        ++s;
      }
    }
    while (fd < 3) {
      frac *= 10;
      ++fd;
    }
    int exp = 0;
    if (s < end && (*s == 'e' || *s == 'E')) {
      ++s;
      bool eneg = false;
      if (s < end && (*s == '+' || *s == '-')) {
        eneg = *s == '-';
        ++s;
      }
      while (s < end && *s >= '0' && *s <= '9') {
        if (exp < 100) exp = exp * 10 + (*s - '0');
        ++s;
      }
      if (eneg) exp = -exp;
    }
    int64_t v = whole * 1000 + frac;
    for (; exp > 0; --exp) v = v < 1000000000000000LL ? v * 10 : v;
    for (; exp < 0; ++exp) v /= 10;
    *milli = neg ? -v : v;
    return true;
  }

  bool value(int index) {
    ws();
    if (s >= end) return false;
    JsonValue v{};
    if (*s == '{') return object();
    if (*s == '[') return array();
    if (*s == '"') {
      v.type = JsonType::kString;
      if (!string(&v.str)) return false;
    } else if (end - s >= 4 && std::memcmp(s, "true", 4) == 0) {
      v.type = JsonType::kBool;
      v.b = true;
      s += 4;
    } else if (end - s >= 5 && std::memcmp(s, "false", 5) == 0) {
      v.type = JsonType::kBool;
      s += 5;
    } else if (end - s >= 4 && std::memcmp(s, "null", 4) == 0) {
      v.type = JsonType::kNull;
      s += 4;
    } else {
      v.type = JsonType::kNumber;
      if (!number(&v.milli)) return false;
    }
    if (cb) cb(user, path, depth, index, v);
    return true;
  }

  bool object() {
    ++s;  // '{'
    if (depth >= kJsonMaxDepth || nest >= kJsonMaxDepth) return false;
    ++depth;
    ++nest;
    ws();
    if (s < end && *s == '}') {
      ++s;
      --depth;
      --nest;
      return true;
    }
    for (;;) {
      ws();
      JsonSlice k;
      if (!string(&k)) return false;
      ws();
      if (s >= end || *s != ':') return false;
      ++s;
      path[depth - 1] = k;
      if (!value(-1)) return false;
      ws();
      if (s >= end) return false;
      if (*s == ',') {
        ++s;
        continue;
      }
      if (*s == '}') {
        ++s;
        --depth;
        --nest;
        return true;
      }
      return false;
    }
  }

  bool array() {
    ++s;  // '['
    if (nest >= kJsonMaxDepth) return false;
    ++nest;
    // Arrays don't add a path level of their own: the values inside report
    // the array's key and their index.
    ws();
    if (s < end && *s == ']') {
      ++s;
      --nest;
      return true;
    }
    for (int i = 0;; ++i) {
      ws();
      if (s < end && *s == '{') {
        if (!object()) return false;
      } else if (s < end && *s == '[') {
        if (!array()) return false;
      } else if (!value(i)) {
        return false;
      }
      ws();
      if (s >= end) return false;
      if (*s == ',') {
        ++s;
        continue;
      }
      if (*s == ']') {
        ++s;
        --nest;
        return true;
      }
      return false;
    }
  }
};

}  // namespace

bool json_walk(const char* s, size_t n, JsonCallback cb, void* user) {
  Walker w{s, s + n, cb, user, {}, 0};
  if (!w.value(-1)) return false;
  w.ws();
  return w.s == w.end;
}

}  // namespace wx
