// Web pages compiled into the firmware (text gzipped, images as-is). The .cpp is generated at
// build time from web/ by tools/embed_web.py.
#pragma once

#include <cstring>

namespace web {

struct Asset {
  const char* name;  // file name, e.g. "layout_a.html"
  const char* type;  // Content-Type
  const unsigned char* data;
  unsigned len;
  bool gzip;  // body is gzip-compressed
};

extern const Asset kAssets[];
extern const int kAssetCount;

inline const Asset* find(const char* name) {
  for (int i = 0; i < kAssetCount; ++i) {
    if (std::strcmp(kAssets[i].name, name) == 0) return &kAssets[i];
  }
  return nullptr;
}

}  // namespace web
