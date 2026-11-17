// DX-LR22 LoRa driver (against a simulated module) and the packet framer.
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "test_util.h"
#include "wx/dxlr22.h"
#include "wx/packet.h"
#include "wx/packet_framer.h"

namespace {

// Simulated DX-LR22, modelled on a real one (firmware V1.2.4):
//  * transparent mode at power-on; a burst of exactly "+++\r\n" enters AT mode
//  * in AT mode, lines must end in exactly "\r\n"; "+++" leaves AT mode and
//    restarts the module (AUX busy, then "Power on")
//  * AUX high while booting or transmitting
class FakeDx : public wx::RadioPort {
 public:
  bool powered = false;
  bool has_switch = true;   // false = no load switch fitted: set_power() does nothing
  bool responsive = true;   // false = hung (never answers, AUX stuck busy)
  bool hang_until_power_cycle = false;
  bool forget_writes = false;
  bool at_mode = false;
  int boot_ms_left = 0;
  int busy_ms_left = 0;
  int gap_ms = 0;
  int power_cycles = 0;
  std::map<std::string, int> settings = {{"MODE", 0},   {"SLEEP", 2},    {"SWITCH", 0}, {"LEVEL", 2},
                                         {"CHANNEL", 0x41}, {"POWE", 22}, {"DRSSI", 0}};
  std::deque<uint8_t> rx;  // bytes the module sends to the Pico
  std::string in;          // bytes from the Pico not yet handled
  std::vector<uint8_t> sent_air;
  std::vector<std::string> commands;

  bool aux_busy() override { return !powered || !responsive || boot_ms_left > 0 || busy_ms_left > 0; }
  void set_power(bool on) override {
    if (!has_switch) return;
    if (on && !powered) {
      ++power_cycles;
      at_mode = false;
      boot_ms_left = 300;
      if (hang_until_power_cycle) {
        responsive = true;
        hang_until_power_cycle = false;
      }
    }
    powered = on;
    if (!on) {
      rx.clear();
      in.clear();
    }
  }
  void write(const uint8_t* d, size_t n) override {
    if (!powered || !responsive) return;
    in.append(reinterpret_cast<const char*>(d), n);
    gap_ms = 0;
    if (at_mode) {
      size_t pos;
      while ((pos = in.find('\n')) != std::string::npos) {
        std::string line = in.substr(0, pos + 1);
        in.erase(0, pos + 1);
        at_line(line);
      }
    }
  }
  int read_byte() override {
    if (rx.empty()) return -1;
    int b = rx.front();
    rx.pop_front();
    return b;
  }
  void say(const std::string& s) {
    for (char c : s) rx.push_back(static_cast<uint8_t>(c));
    rx.push_back('\r');
    rx.push_back('\n');
  }
  void at_line(const std::string& raw) {
    if (raw.size() < 2 || raw[raw.size() - 2] != '\r') {
      say("ERROR=104");  // the real module rejects anything but CR LF
      return;
    }
    const std::string line = raw.substr(0, raw.size() - 2);
    commands.push_back(line);
    if (line == "+++") {
      say("Exit AT");
      at_mode = false;
      boot_ms_left = 150;  // restarts; "Power on" when done
      return;
    }
    if (line.compare(0, 3, "AT+") != 0) {
      say("ERROR=104");
      return;
    }
    for (auto& kv : settings) {
      const std::string& name = kv.first;
      if (line.compare(3, name.size(), name) != 0) continue;
      const std::string arg = line.substr(3 + name.size());
      const bool hex = name == "CHANNEL";
      char buf[32];
      if (arg.empty()) {
        std::snprintf(buf, sizeof(buf), hex ? "+%s=%02x" : "+%s=%d", name.c_str(), kv.second);
        say(buf);
        return;
      }
      const int v = static_cast<int>(std::strtol(arg.c_str(), nullptr, hex ? 16 : 10));
      if (!forget_writes) kv.second = v;
      std::snprintf(buf, sizeof(buf), hex ? "+%s=%02x" : "+%s=%d", name.c_str(), v);
      say(buf);
      say("OK");
      return;
    }
    say("ERROR=104");
  }
  void tick() {
    if (boot_ms_left > 0 && --boot_ms_left == 0 && powered && responsive) say("Power on");
    if (busy_ms_left > 0) --busy_ms_left;
    // Transparent mode: a burst ends after a short gap; "+++\r\n" alone = AT mode.
    if (!at_mode && !in.empty() && ++gap_ms >= 3) {
      if (in == "+++\r\n") {
        at_mode = true;
        say("Entry AT");
      } else {
        sent_air.assign(in.begin(), in.end());
        busy_ms_left = 300;
      }
      in.clear();
    }
  }
};

void run(wx::Dxlr22Radio& r, FakeDx& m, uint32_t& now, uint32_t ms) {
  for (uint32_t i = 0; i < ms; ++i) {
    m.tick();
    r.poll(now++);
  }
}

void test_dx_configures_factory_module() {
  FakeDx m;
  wx::Dxlr22Radio r(m, wx::Dxlr22Config{});
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 5000);
  CHECK(r.ready());
  CHECK(!m.at_mode);                      // left AT mode again
  CHECK_EQ(r.stats().configs, 1u);
  CHECK_EQ(r.stats().config_fixes, 1u);   // only DRSSI differs from the factory settings
  CHECK_EQ(m.settings["DRSSI"], 1);
  CHECK_EQ(m.settings["CHANNEL"], 0x41);  // read back in hex, left alone

  // Send a packet: AUX is high while it transmits, then it completes.
  uint8_t pkt[4] = {1, 2, 3, 4};
  CHECK(r.send(pkt, 4, now));
  CHECK(!r.send(pkt, 4, now));  // busy
  run(r, m, now, 1000);
  CHECK(!r.tx_busy());
  CHECK_EQ(r.stats().sent, 1u);
  CHECK_EQ(m.sent_air.size(), 4u);
}

void test_dx_writes_hex_channel_and_power() {
  FakeDx m;
  wx::Dxlr22Config cfg;
  cfg.channel = 0x3A;
  cfg.power_dbm = 13;
  wx::Dxlr22Radio r(m, cfg);
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 5000);
  CHECK(r.ready());
  CHECK_EQ(m.settings["CHANNEL"], 0x3A);
  CHECK_EQ(m.settings["POWE"], 13);
  bool saw = false;
  for (const auto& c : m.commands) saw = saw || c == "AT+CHANNEL3A";
  CHECK(saw);
}

void test_dx_already_configured_needs_no_write() {
  FakeDx m;
  m.settings["DRSSI"] = 1;
  wx::Dxlr22Radio r(m, wx::Dxlr22Config{});
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 5000);
  CHECK(r.ready());
  CHECK_EQ(r.stats().config_fixes, 0u);
}

void test_dx_left_in_at_mode() {
  // No load switch, and the Pico restarted while the module was still in AT
  // mode: the first "+++" takes it out instead, so the driver has to ask again.
  FakeDx m;
  m.powered = true;
  m.has_switch = false;
  m.at_mode = true;
  wx::Dxlr22Radio r(m, wx::Dxlr22Config{});
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 8000);
  CHECK(r.ready());
  CHECK(!m.at_mode);
  CHECK_EQ(r.stats().timeouts, 0u);
}

void test_dx_works_without_load_switch() {
  FakeDx m;
  m.powered = true;
  m.has_switch = false;
  wx::Dxlr22Radio r(m, wx::Dxlr22Config{});
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 5000);
  CHECK(r.ready());
  CHECK_EQ(m.settings["DRSSI"], 1);
}

void test_dx_hung_module_is_power_cycled() {
  FakeDx m;
  wx::Dxlr22Radio r(m, wx::Dxlr22Config{});
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 5000);
  CHECK(r.ready());

  // Hangs mid-transmit; only a power cycle fixes it.
  uint8_t pkt[2] = {9, 9};
  CHECK(r.send(pkt, 2, now));
  m.responsive = false;
  m.hang_until_power_cycle = true;
  const int cycles_before = m.power_cycles;
  run(r, m, now, 30000);
  CHECK(m.power_cycles > cycles_before);
  CHECK(r.ready());
  CHECK(r.stats().timeouts >= 1u);
}

void test_dx_dead_module_rests_then_retries() {
  FakeDx m;
  m.responsive = false;  // never answers at all
  wx::Dxlr22Radio r(m, wx::Dxlr22Config{});
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 60000);
  CHECK(r.dead());
  CHECK(!m.powered);  // left off while resting
  m.responsive = true;  // e.g. loose wire reseated
  run(r, m, now, 5 * 60 * 1000 + 10000);
  CHECK(r.ready());
}

void test_dx_settings_that_dont_stick() {
  FakeDx m;
  m.forget_writes = true;
  wx::Dxlr22Radio r(m, wx::Dxlr22Config{});
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 60000);
  CHECK(!r.ready());  // never reports ready with wrong settings
}

void test_dx_verify_fixes_drift() {
  FakeDx m;
  wx::Dxlr22Radio r(m, wx::Dxlr22Config{});
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 5000);
  CHECK(r.ready());
  const uint32_t fixes = r.stats().config_fixes;
  m.settings["LEVEL"] = 5;  // drifted
  r.verify(now);
  CHECK(!r.ready());
  run(r, m, now, 5000);
  CHECK(r.ready());
  CHECK_EQ(m.settings["LEVEL"], 2);
  CHECK_EQ(r.stats().config_fixes, fixes + 1);
  CHECK_EQ(r.stats().configs, 1u);  // verify passes aren't counted as new configs
}

void test_dx_receives_packets() {
  FakeDx m;
  wx::Dxlr22Radio r(m, wx::Dxlr22Config{});
  uint32_t now = 0;
  r.start(now);
  run(r, m, now, 5000);
  CHECK(r.ready());

  wx::OutdoorReading in;
  in.boot_id = 77;
  in.rain_tips = 12;
  uint8_t buf[64];
  wx::encode(in, buf, sizeof(buf));
  for (size_t i = 0; i < wx::kPacketSize; ++i) m.rx.push_back(buf[i]);
  m.rx.push_back(0xAB);  // RSSI byte: -(255 - 171) = -84 dBm (vendor's example)
  run(r, m, now, 5);
  wx::OutdoorReading out;
  int16_t rssi = 0;
  CHECK(!r.receive(now, &out, &rssi));  // frame not finished until the gap passes
  run(r, m, now, 50);
  CHECK(r.receive(now, &out, &rssi));
  CHECK_EQ(out.boot_id, 77);
  CHECK_EQ(out.rain_tips, 12u);
  CHECK_EQ(rssi, -84);
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
  test_dx_configures_factory_module();
  test_dx_writes_hex_channel_and_power();
  test_dx_already_configured_needs_no_write();
  test_dx_left_in_at_mode();
  test_dx_works_without_load_switch();
  test_dx_hung_module_is_power_cycled();
  test_dx_dead_module_rests_then_retries();
  test_dx_settings_that_dont_stick();
  test_dx_verify_fixes_drift();
  test_dx_receives_packets();
  test_framer_noise_and_split();
}
