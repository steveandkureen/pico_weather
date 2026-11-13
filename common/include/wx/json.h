// Tiny JSON tools with no heap:
//  * JsonWriter writes into a fixed buffer and remembers if it ran out of room.
//  * json_walk() reads a document and calls back for every scalar value with
//    its key path, so a parser can pick out the fields it wants regardless of
//    order. Numbers are parsed to fixed point (×1000) without strtod (which
//    allocates in newlib).
#pragma once

#include <cstddef>
#include <cstdint>

namespace wx {

class JsonWriter {
 public:
  JsonWriter(char* buf, size_t cap) : buf_(buf), cap_(cap) {
    if (cap_ > 0) buf_[0] = '\0';
  }

  JsonWriter& begin_object();
  JsonWriter& end_object();
  JsonWriter& begin_array();
  JsonWriter& end_array();
  JsonWriter& key(const char* k);
  JsonWriter& value(int64_t v);
  // Fixed-point: value(1234, 2) writes 12.34.
  JsonWriter& fixed(int64_t v, int decimals);
  JsonWriter& value(const char* s);
  JsonWriter& boolean(bool b);
  JsonWriter& null();

  bool ok() const { return !overflow_; }
  size_t size() const { return len_; }
  const char* c_str() const { return buf_; }

 private:
  void sep();
  void raw(const char* s, size_t n);
  void raw(const char* s);
  void ch(char c) { raw(&c, 1); }

  char* buf_;
  size_t cap_;
  size_t len_ = 0;
  bool overflow_ = false;
  bool need_comma_ = false;
};

struct JsonSlice {
  const char* p;
  uint16_t n;
  bool equals(const char* s) const;
};

enum class JsonType : uint8_t { kNumber, kString, kBool, kNull };

struct JsonValue {
  JsonType type;
  int64_t milli;    // numbers: value × 1000 (truncated)
  bool b;           // booleans
  JsonSlice str;    // strings: raw contents between the quotes (escapes not decoded)
};

constexpr int kJsonMaxDepth = 8;

// path[0..depth-1] = object keys from the root (array levels have an empty key).
// index = position within the innermost array, or -1 if not in an array.
using JsonCallback = void (*)(void* user, const JsonSlice* path, int depth, int index,
                              const JsonValue& v);

// Returns false on malformed input or nesting deeper than kJsonMaxDepth.
bool json_walk(const char* s, size_t n, JsonCallback cb, void* user);

}  // namespace wx
