#pragma once

#include <cstddef>
#include <cstdint>

namespace wx {

// Standard CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320). Bitwise: no table,
// so it costs no flash; speed is irrelevant for a few KB once in a while.
// Pass a previous result as `crc` to continue over more data.
uint32_t crc32(const void* data, size_t len, uint32_t crc = 0);

}  // namespace wx
