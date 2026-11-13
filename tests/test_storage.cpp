// Flash record store (including power cuts mid-write) and settings.
#include <cstring>
#include <vector>

#include "test_util.h"
#include "wx/crc32.h"
#include "wx/flash_store.h"
#include "wx/settings.h"

namespace {

// NOR flash model: erase sets 0xFF, program can only clear bits.
// `budget` simulates a power cut: after that many bytes are touched, nothing
// more is written (the rest of the operation is lost).
class FakeFlash : public wx::FlashDevice {
 public:
  explicit FakeFlash(size_t sectors) : mem_(sectors * kSectorSize, 0xFF) {}

  bool read(uint32_t off, void* dst, size_t len) override {
    if (off + len > mem_.size()) return false;
    std::memcpy(dst, mem_.data() + off, len);
    return true;
  }
  bool erase_sector(uint32_t off) override {
    if (off % kSectorSize != 0 || off + kSectorSize > mem_.size()) return false;
    for (size_t i = 0; i < kSectorSize; ++i) {
      if (!spend()) return true;  // power died: partial erase
      mem_[off + i] = 0xFF;
    }
    ++erases;
    return true;
  }
  bool program(uint32_t off, const void* src, size_t len) override {
    if (off % kPageSize != 0 || len % kPageSize != 0 || off + len > mem_.size()) return false;
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < len; ++i) {
      if (!spend()) return true;
      mem_[off + i] &= s[i];
    }
    return true;
  }

  long budget = -1;  // -1 = unlimited
  int erases = 0;

 private:
  bool spend() {
    if (budget < 0) return true;
    if (budget == 0) return false;
    --budget;
    return true;
  }
  std::vector<uint8_t> mem_;
};

struct Rec {
  uint32_t a;
  uint32_t b;
  char text[40];
};

void test_crc32_known_value() {
  CHECK_EQ(wx::crc32("123456789", 9), 0xCBF43926u);
  // Continuing over two halves gives the same result.
  CHECK_EQ(wx::crc32("6789", 4, wx::crc32("12345", 5)), 0xCBF43926u);
}

void test_store_empty_then_save_load() {
  FakeFlash f(4);
  uint8_t scratch[wx::FlashDevice::kSectorSize];
  wx::RecordStore st(f, 0, 4096, 0x54455354, scratch);
  Rec r{};
  CHECK_EQ(st.load(&r, sizeof(r)), -1);
  Rec w{1, 2, "hello"};
  CHECK(st.save(&w, sizeof(w)));
  Rec back{};
  CHECK_EQ(st.load(&back, sizeof(back)), static_cast<int>(sizeof(Rec)));
  CHECK_EQ(back.a, 1u);
  CHECK_STR(back.text, "hello");

  // Alternates between the two sectors; newest wins.
  for (uint32_t i = 0; i < 10; ++i) {
    w.a = 100 + i;
    CHECK(st.save(&w, sizeof(w)));
  }
  wx::RecordStore st2(f, 0, 4096, 0x54455354, scratch);  // fresh instance, as after reboot
  CHECK_EQ(st2.load(&back, sizeof(back)), static_cast<int>(sizeof(Rec)));
  CHECK_EQ(back.a, 109u);
}

void test_store_power_cut_at_every_point() {
  // Save "old", then cut power at every possible byte of the next save. The
  // load must always return either the old or the new record, never garbage.
  for (long cut = 0; cut < 2 * 4096 + 600; cut += 37) {
    FakeFlash f(2);
    uint8_t scratch[wx::FlashDevice::kSectorSize];
    wx::RecordStore st(f, 0, 4096, 0x41424344, scratch);
    Rec old{1, 1, "old"};
    CHECK(st.save(&old, sizeof(old)));
    Rec neu{2, 2, "new"};
    f.budget = cut;
    st.save(&neu, sizeof(neu));
    f.budget = -1;

    wx::RecordStore again(f, 0, 4096, 0x41424344, scratch);
    Rec got{};
    const int n = again.load(&got, sizeof(got));
    CHECK_EQ(n, static_cast<int>(sizeof(Rec)));
    CHECK(got.a == 1 || got.a == 2);
    CHECK(std::strcmp(got.text, got.a == 1 ? "old" : "new") == 0);

    // And the store keeps working afterwards.
    Rec third{3, 3, "third"};
    CHECK(again.save(&third, sizeof(third)));
    CHECK_EQ(again.load(&got, sizeof(got)), static_cast<int>(sizeof(Rec)));
    CHECK_EQ(got.a, 3u);
  }
}

void test_store_kind_and_corruption() {
  FakeFlash f(2);
  uint8_t scratch[wx::FlashDevice::kSectorSize];
  wx::RecordStore a(f, 0, 4096, 0x11111111, scratch);
  Rec w{7, 7, "x"};
  CHECK(a.save(&w, sizeof(w)));
  wx::RecordStore other(f, 0, 4096, 0x22222222, scratch);  // different kind
  Rec got{};
  CHECK_EQ(other.load(&got, sizeof(got)), -1);
  // Flip one payload bit in place: CRC must catch it.
  uint8_t sector[4096];
  f.read(0, sector, sizeof(sector));
  bool in_a = sector[0] == 0x31;  // which sector holds it: "WXR1" LE starts with '1'
  uint32_t base = in_a ? 0 : 4096;
  f.read(base, sector, sizeof(sector));
  sector[30] ^= 0x01;
  f.erase_sector(base);
  f.program(base, sector, sizeof(sector));
  wx::RecordStore again(f, 0, 4096, 0x11111111, scratch);
  CHECK_EQ(again.load(&got, sizeof(got)), -1);
}

void test_store_shorter_record_fills_defaults() {
  FakeFlash f(2);
  uint8_t scratch[wx::FlashDevice::kSectorSize];
  wx::RecordStore st(f, 0, 4096, 0x1, scratch);
  uint32_t small = 42;
  CHECK(st.save(&small, sizeof(small)));
  Rec r{};
  r.b = 99;  // "default" for a newer field
  CHECK_EQ(st.load(&r, sizeof(r)), 4);
  CHECK_EQ(r.a, 42u);
  CHECK_EQ(r.b, 99u);
  CHECK(!st.save(scratch, wx::RecordStore::kMaxPayload + 1));
}

void test_settings_defaults_and_sanitize() {
  wx::Settings s;
  wx::settings_defaults(&s);
  CHECK_STR(s.hostname, "weather");
  CHECK_EQ(s.layout, wx::kLayoutAuto);
  CHECK_EQ(s.day_layout, 'B');
  CHECK_EQ(s.night_layout, 'D');
  CHECK(s.wifi_ssid[0] == '\0');

  std::memset(s.hostname, 'x', sizeof(s.hostname));  // unterminated
  s.layout = 'Z';
  s.temp_unit = static_cast<wx::TempUnit>(9);
  std::strcpy(s.pin, "12ab");
  s.rain_um_per_tip = 0;
  s.vane_offset_deg = 999;
  wx::settings_sanitize(&s);
  CHECK_EQ(std::strlen(s.hostname), 31u);
  CHECK(wx::hostname_valid(s.hostname));
  CHECK_EQ(s.layout, wx::kLayoutAuto);
  CHECK(s.temp_unit == wx::TempUnit::kF);
  CHECK_STR(s.pin, "");
  CHECK_EQ(s.rain_um_per_tip, 279u);
  CHECK_EQ(s.vane_offset_deg, 0);

  CHECK(wx::hostname_valid("weather"));
  CHECK(wx::hostname_valid("wx-2"));
  CHECK(!wx::hostname_valid("-bad"));
  CHECK(!wx::hostname_valid("bad-"));
  CHECK(!wx::hostname_valid("no spaces"));
  CHECK(!wx::hostname_valid(""));
  CHECK(wx::pin_valid(""));
  CHECK(wx::pin_valid("1234"));
  CHECK(!wx::pin_valid("123"));
  CHECK(!wx::pin_valid("123456789"));
}

}  // namespace

void run_storage_tests() {
  test_crc32_known_value();
  test_store_empty_then_save_load();
  test_store_power_cut_at_every_point();
  test_store_kind_and_corruption();
  test_store_shorter_record_fills_defaults();
  test_settings_defaults_and_sanitize();
}
