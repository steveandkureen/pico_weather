// E220 LoRa driver (against a simulated module) and the packet framer.
#include <cstring>
#include <deque>
#include <vector>

#include "test_util.h"
#include "wx/e220.h"
#include "wx/packet.h"
#include "wx/packet_framer.h"

namespace {

// Simulated E220: answers config commands in mode 3, transmits in mode 0.
class FakeModule : public wx::E220Port {
 public:
  bool powered = false;
  bool m0 = false, m1 = false;
  uint8_t regs[8] = {0x00, 0x00, 0x62, 0x00, 0x17, 0x03, 0, 0};  // factory defaults
  bool responsive = true;   // false = hung module (never answers, AUX stuck low)
  bool forget_writes = false;
  int boot_ms_left = 0;
  int busy_ms_left = 0;
  int power_cycles = 0;
  std::deque<uint8_t> rx;   // bytes the module sends to the Pico
  std::vector<uint8_t> sent_air;

  void set_mode_pins(bool a, bool b) override {
    m0 = a;
    m1 = b;
  }
  bool aux_high() override { return powered && responsive && boot_ms_left <= 0 && busy_ms_left <= 0; }
  void set_power(bool on) override {
    if (on && !powered) {
      boot_ms_left = 100;
      ++power_cycles;
      if (hang_until_power_cycle) {
        responsive = true;
        hang_until_power_cycle = false;
      }
    }
    powered = on;
    if (!on) rx.clear();
  }
  void write(const uint8_t* d, size_t n) override {
    if (!powered || !responsive) return;
    if (m0 && m1) {
      if (n == 3 && d[0] == 0xC1) {
        reply();
      } else if (n == 11 && d[0] == 0xC0) {
        if (!forget_writes) std::memcpy(regs, d + 3, 8);
        rx.push_back(0xC1);
        rx.push_back(0x00);
        rx.push_back(0x08);
        for (int i = 0; i < 8; ++i) rx.push_back(d[3 + i]);
      }
    } else if (!m0 && !m1) {
      sent_air.assign(d, d + n);
      busy_ms_left = 300;
    }
  }
  int read_byte() override {
    if (rx.empty()) return -1;
    int b = rx.front();
    rx.pop_front();
    return b;
  }
  void tick() {
    if (boot_ms_left > 0) --boot_ms_left;
    if (busy_ms_left > 0) --busy_ms_left;
  }
  void reply() {
    rx.push_back(0xC1);
    rx.push_back(0x00);
    rx.push_back(0x08);
    for (int i = 0; i < 6; ++i) rx.push_back(regs[i]);
    rx.push_back(0);
    rx.push_back(0);
  }
  bool hang_until_power_cycle = false;
};

void run(wx::E220Radio& r, FakeModule& m, uint32_t& now, uint32_t ms) {
  for (uint32_t i = 0; i < ms; ++i) {
    m.tick();
    r.poll(now++);
  }
}

void test_e220_regs() {
  wx::E220Config c;
  uint8_t regs[8];
  c.to_regs(regs);
  CHECK_EQ(regs[2], 0x62);  // 9600 8N1, 2.4k
  CHECK_EQ(regs[3], 0x02);  // 13 dBm
  CHECK_EQ(regs[4], 65);    // 915.125 MHz
  CHECK_EQ(regs[5], 0x83);  // RSSI byte on, transparent, LBT off
  uint8_t cmd[16];
  CHECK_EQ(wx::e220_write_cmd(c, cmd), 11u);
  CHECK_EQ(cmd[0], 0xC0);
  CHECK_EQ(wx::e220_read_cmd(cmd), 3u);
  CHECK_EQ(cmd[0], 0xC1);
  CHECK(c.matches_regs(regs));
  regs[4] = 23;
  CHECK(!c.matches_regs(regs));
}

void test_e220_configures_factory_module() {
  FakeModule m;
  wx::E220Config cfg;
  wx::E220Radio r(m, cfg);
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 2000);
  CHECK(r.ready());
  CHECK_EQ(r.stats().config_fixes, 1u);  // factory channel 0x17 was rewritten
  CHECK_EQ(m.regs[4], 65);
  CHECK(!m.m0 && !m.m1);  // back in normal mode

  // Send a packet: AUX drops while transmitting, then it completes.
  uint8_t pkt[4] = {1, 2, 3, 4};
  CHECK(r.send(pkt, 4, now));
  CHECK(!r.send(pkt, 4, now));  // busy
  run(r, m, now, 1000);
  CHECK(!r.tx_busy());
  CHECK_EQ(r.stats().sent, 1u);
  CHECK_EQ(m.sent_air.size(), 4u);
}

void test_e220_already_configured_needs_no_write() {
  FakeModule m;
  wx::E220Config cfg;
  cfg.to_regs(m.regs);
  wx::E220Radio r(m, cfg);
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 2000);
  CHECK(r.ready());
  CHECK_EQ(r.stats().config_fixes, 0u);
}

void test_e220_hung_module_is_power_cycled() {
  FakeModule m;
  wx::E220Config cfg;
  wx::E220Radio r(m, cfg);
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 2000);
  CHECK(r.ready());

  // Module hangs mid-transmit; only a power cycle fixes it.
  m.responsive = false;
  m.hang_until_power_cycle = true;
  uint8_t pkt[2] = {9, 9};
  // aux is low now, so send refuses; simulate the hang during a transmit instead.
  m.responsive = true;
  CHECK(r.send(pkt, 2, now));
  m.responsive = false;
  const int cycles_before = m.power_cycles;
  run(r, m, now, 20000);
  CHECK(m.power_cycles > cycles_before);
  CHECK(r.ready());
  CHECK(r.stats().timeouts >= 1u);
}

void test_e220_dead_module_rests_then_retries() {
  FakeModule m;
  m.responsive = false;  // never answers at all
  wx::E220Config cfg;
  wx::E220Radio r(m, cfg);
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 30000);
  CHECK(r.dead());
  CHECK(!m.powered);  // left off while resting
  // Later the module works again (e.g. loose wire reseated).
  m.responsive = true;
  run(r, m, now, 5 * 60 * 1000 + 5000);
  CHECK(r.ready());
}

void test_e220_settings_that_dont_stick() {
  FakeModule m;
  m.forget_writes = true;
  wx::E220Config cfg;
  wx::E220Radio r(m, cfg);
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 60000);
  CHECK(!r.ready());  // never reports ready with wrong settings
}

void test_e220_receives_packets() {
  FakeModule m;
  wx::E220Config cfg;
  wx::E220Radio r(m, cfg);
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 2000);
  CHECK(r.ready());

  wx::OutdoorReading in;
  in.boot_id = 77;
  in.rain_tips = 12;
  uint8_t buf[64];
  wx::encode(in, buf, sizeof(buf));
  for (size_t i = 0; i < wx::kPacketSize; ++i) m.rx.push_back(buf[i]);
  m.rx.push_back(200);  // RSSI byte → -56 dBm
  run(r, m, now, 5);
  wx::OutdoorReading out;
  int16_t rssi = 0;
  CHECK(!r.receive(now, &out, &rssi));  // frame not finished until the gap passes
  run(r, m, now, 50);
  CHECK(r.receive(now, &out, &rssi));
  CHECK_EQ(out.boot_id, 77);
  CHECK_EQ(out.rain_tips, 12u);
  CHECK_EQ(rssi, -56);
  CHECK(!r.receive(now, &out, &rssi));
}

void test_framer_noise_and_split() {
  wx::PacketFramer f;
  wx::OutdoorReading in;
  in.seq = 5;
  uint8_t buf[64];
  wx::encode(in, buf, sizeof(buf));
  wx::OutdoorReading out;
  int16_t rssi = 0;
  uint32_t t = 0;

  // Garbage, a gap, then a clean packet without RSSI.
  f.push(0x00, t);
  f.push(0x55, t);
  t += 100;
  CHECK(!f.poll(t, &out, &rssi));
  CHECK_EQ(f.rejected(), 1u);
  for (size_t i = 0; i < wx::kPacketSize; ++i) f.push(buf[i], t);
  t += 30;
  CHECK(f.poll(t, &out, &rssi));
  CHECK_EQ(out.seq, 5);
  CHECK_EQ(rssi, 0);

  // Leading noise in the same burst is skipped.
  f.push(0xA7, t);
  f.push(0x13, t);
  for (size_t i = 0; i < wx::kPacketSize; ++i) f.push(buf[i], t);
  t += 30;
  CHECK(f.poll(t, &out, &rssi));

  // A packet split by a long gap is two broken frames, both rejected.
  const uint32_t before = f.rejected();
  for (size_t i = 0; i < 10; ++i) f.push(buf[i], t);
  t += 100;
  CHECK(!f.poll(t, &out, &rssi));
  for (size_t i = 10; i < wx::kPacketSize; ++i) f.push(buf[i], t);
  t += 100;
  CHECK(!f.poll(t, &out, &rssi));
  CHECK_EQ(f.rejected(), before + 2);

  // Overflow is rejected, not crashed on.
  for (int i = 0; i < 500; ++i) f.push(0xA7, t);
  t += 100;
  CHECK(!f.poll(t, &out, &rssi));
}

}  // namespace

void run_radio_tests() {
  test_e220_regs();
  test_e220_configures_factory_module();
  test_e220_already_configured_needs_no_write();
  test_e220_hung_module_is_power_cycled();
  test_e220_dead_module_rests_then_retries();
  test_e220_settings_that_dont_stick();
  test_e220_receives_packets();
  test_framer_noise_and_split();
}
