// BME280 math + driver state machine, wind statistics.
#include <cstring>

#include "test_util.h"
#include "wx/bme280.h"
#include "wx/health.h"
#include "wx/wind.h"

namespace {

// Calibration and raw values from the Bosch BMP280 datasheet worked example
// (the BME280 uses the same temperature and pressure formulas).
wx::Bme280Calib datasheet_calib() {
  wx::Bme280Calib c{};
  c.t1 = 27504;
  c.t2 = 26435;
  c.t3 = -1000;
  c.p1 = 36477;
  c.p2 = -10685;
  c.p3 = 3024;
  c.p4 = 2855;
  c.p5 = 140;
  c.p6 = -7;
  c.p7 = 15500;
  c.p8 = -14600;
  c.p9 = 6000;
  // Typical humidity calibration from a real module.
  c.h1 = 75;
  c.h2 = 362;
  c.h3 = 0;
  c.h4 = 313;
  c.h5 = 50;
  c.h6 = 30;
  return c;
}

void test_bme280_compensation() {
  const wx::Bme280Calib c = datasheet_calib();
  int32_t t_fine = 0;
  CHECK_EQ(wx::bme280_temp_x100(c, 519888, &t_fine), 2508);  // 25.08 °C
  CHECK_EQ(t_fine, 128422);
  CHECK_NEAR(wx::bme280_pressure_pa(c, 415148, t_fine), 100653, 2);  // 1006.53 hPa
  const uint32_t h = wx::bme280_humidity_x100(c, 30000, t_fine);
  CHECK(h <= 10000);
  // Humidity rises with the raw reading.
  CHECK(wx::bme280_humidity_x100(c, 32000, t_fine) > h);
}

void test_bme280_calib_parse() {
  uint8_t b88[26] = {0};
  uint8_t be1[7] = {0x6A, 0x01, 0x00, 0x13, 0x2A, 0x03, 0x1E};
  b88[0] = 0x70;  // t1 = 0x6B70 = 27504
  b88[1] = 0x6B;
  b88[25] = 75;
  wx::Bme280Calib c{};
  wx::bme280_parse_calib(b88, be1, &c);
  CHECK_EQ(c.t1, 27504);
  CHECK_EQ(c.h1, 75);
  CHECK_EQ(c.h2, 362);
  CHECK_EQ(c.h4, (0x13 << 4) | 0xA);
  CHECK_EQ(c.h5, (0x03 << 4) | 0x2);
  CHECK_EQ(c.h6, 30);
}

// Fake BME280 on a fake bus.
class FakeBus : public wx::I2cBus {
 public:
  uint8_t regs[256] = {};
  bool present = true;
  uint8_t only_addr = 0;  // nonzero: the chip answers only at this address
  int fail_next = 0;
  int recovers = 0;

  FakeBus() {
    regs[0xD0] = 0x60;
    // Calibration matching datasheet_calib (t and p part).
    const uint16_t t1 = 27504;
    const int16_t t2 = 26435, t3 = -1000;
    const uint16_t p1 = 36477;
    const int16_t p[] = {-10685, 3024, 2855, 140, -7, 15500, -14600, 6000};
    put16(0x88, t1);
    put16(0x8A, static_cast<uint16_t>(t2));
    put16(0x8C, static_cast<uint16_t>(t3));
    put16(0x8E, p1);
    for (int i = 0; i < 8; ++i) put16(static_cast<uint8_t>(0x90 + 2 * i), static_cast<uint16_t>(p[i]));
    regs[0xA1] = 75;
    regs[0xE1] = 0x6A;
    regs[0xE2] = 0x01;
    // Raw data: pressure 415148, temperature 519888, humidity 30000.
    set_raw(415148, 519888, 30000);
  }
  void put16(uint8_t r, uint16_t v) {
    regs[r] = static_cast<uint8_t>(v);
    regs[r + 1] = static_cast<uint8_t>(v >> 8);
  }
  void set_raw(int32_t p, int32_t t, int32_t h) {
    regs[0xF7] = static_cast<uint8_t>(p >> 12);
    regs[0xF8] = static_cast<uint8_t>(p >> 4);
    regs[0xF9] = static_cast<uint8_t>((p & 0xF) << 4);
    regs[0xFA] = static_cast<uint8_t>(t >> 12);
    regs[0xFB] = static_cast<uint8_t>(t >> 4);
    regs[0xFC] = static_cast<uint8_t>((t & 0xF) << 4);
    regs[0xFD] = static_cast<uint8_t>(h >> 8);
    regs[0xFE] = static_cast<uint8_t>(h);
  }
  bool ok(uint8_t addr) {
    if (!present) return false;
    if (only_addr && addr != only_addr) return false;
    if (fail_next > 0) {
      --fail_next;
      return false;
    }
    return true;
  }
  bool write(uint8_t a, const uint8_t* d, size_t n) override {
    if (!ok(a)) return false;
    if (n >= 2) regs[d[0]] = d[1];
    return true;
  }
  bool read_reg(uint8_t a, uint8_t reg, uint8_t* d, size_t n) override {
    if (!ok(a)) return false;
    for (size_t i = 0; i < n; ++i) d[i] = regs[(reg + i) & 0xFF];
    return true;
  }
  void recover() override { ++recovers; }
};

// Runs the driver for `ms` milliseconds in 1 ms steps.
void run(wx::Bme280& s, uint32_t& now, uint32_t ms) {
  for (uint32_t i = 0; i < ms; ++i) s.poll(now++);
}

void test_bme280_driver_reads() {
  FakeBus bus;
  wx::Bme280 s(bus, 0x76, 30000);
  uint32_t now = 1000;
  run(s, now, 100);
  CHECK(s.reading().valid);
  CHECK_EQ(s.chip_id(), 0x60);
  CHECK_EQ(s.reading().temp_c_x100, 2508);
  CHECK_NEAR(s.reading().pressure_pa, 100653, 2);
  CHECK(s.reading().has_humidity);
  CHECK(s.progress() > 0);
  // Ctrl_meas was written with forced mode.
  CHECK_EQ(bus.regs[0xF4] & 0x03, 0x01);
}

// Sensor not wired at all: the driver keeps retrying with backoff (up to 60 s)
// but must keep reporting progress so the watchdog gate never resets the board.
void test_bme280_absent_keeps_watchdog_fed() {
  FakeBus bus;
  bus.present = false;
  wx::Bme280 s(bus, 0x76, 30000);
  wx::HealthMonitor h;
  const int t = h.add("bme280", 15000);
  uint32_t now = 0;
  for (int i = 0; i < 5 * 60 * 1000; i += 10, now += 10) {
    s.poll(now);
    h.report(t, s.progress(), now);
    CHECK(h.all_ok(now));
    if (!h.all_ok(now)) break;
  }
  CHECK(!s.reading().valid);
  // Plugged in later: it is found on the next probe.
  bus.present = true;
  run(s, now, 61000);
  CHECK(s.reading().valid);
}

void test_bme280_driver_recovers() {
  FakeBus bus;
  wx::Bme280 s(bus, 0x76, 1000);
  uint32_t now = 0;
  run(s, now, 100);
  CHECK(s.reading().valid);

  // Sensor disappears: after 3 failures the reading becomes invalid.
  bus.present = false;
  run(s, now, 20000);
  CHECK(!s.reading().valid);
  CHECK(bus.recovers >= 3);
  CHECK(s.errors() >= 3);

  // Comes back: driver re-probes and reads again without a reboot.
  bus.present = true;
  run(s, now, 70000);
  CHECK(s.reading().valid);
}

void test_bme280_bmp280_has_no_humidity() {
  FakeBus bus;
  bus.regs[0xD0] = 0x58;
  wx::Bme280 s(bus, 0x76, 1000);
  uint32_t now = 0;
  run(s, now, 100);
  CHECK(s.reading().valid);
  CHECK(!s.reading().has_humidity);
  CHECK_EQ(s.chip_id(), 0x58);
}

void test_bme280_rejects_out_of_range() {
  FakeBus bus;
  bus.set_raw(0x80000, 0x80000, 0x8000);  // "skipped" values
  wx::Bme280 s(bus, 0x76, 1000);
  uint32_t now = 0;
  run(s, now, 5000);
  CHECK(!s.reading().valid);
}

void test_wind_gust_and_average() {
  wx::WindSampler w;
  const uint16_t pulses[] = {1, 1, 1, 5, 9, 2, 0, 0, 1, 1};
  for (uint16_t p : pulses) w.add_second(p, -1.0f);
  wx::WindSampler::Result r = w.take();
  CHECK_EQ(r.pulses, 21);
  CHECK_EQ(r.interval_ms, 10000);
  CHECK_EQ(r.gust_pulses_3s, 16);  // 5 + 9 + 2
  CHECK_EQ(r.dir_deg, 0xFFFF);
  // Next interval starts fresh.
  w.add_second(2, -1.0f);
  r = w.take();
  CHECK_EQ(r.pulses, 2);
  CHECK_EQ(r.interval_ms, 1000);
}

void test_wind_direction_vector_average() {
  wx::WindSampler w;
  w.add_second(3, 350.0f);
  w.add_second(3, 10.0f);
  wx::WindSampler::Result r = w.take();
  CHECK(r.dir_deg == 0 || r.dir_deg == 359);
  // Calm: unweighted average still gives a direction.
  w.add_second(0, 90.0f);
  w.add_second(0, 90.0f);
  r = w.take();
  CHECK_EQ(r.dir_deg, 90);
  // Weighted: strong wind from 180 dominates a calm-ish 90.
  w.add_second(10, 180.0f);
  w.add_second(1, 90.0f);
  r = w.take();
  CHECK(r.dir_deg > 170 && r.dir_deg < 180);
}

void test_wind_conversions() {
  // 30 pulses in 30 s = 1 Hz × 0.667 m/s per Hz = 0.667 m/s → 66 (×100).
  CHECK_EQ(wx::wind_mps_x100(30, 30000, 667), 66u);
  CHECK_EQ(wx::wind_mps_x100(0, 30000, 667), 0u);
  CHECK_EQ(wx::wind_mps_x100(10, 0, 667), 0u);
  CHECK_NEAR(wx::as5600_adc_to_deg(0), 0.0, 0.01);
  CHECK_NEAR(wx::as5600_adc_to_deg(2048), 180.0, 0.01);
  CHECK_EQ(wx::vane_true_deg(350, 20), 10);
  CHECK_EQ(wx::vane_true_deg(10, -20), 350);
}

// Boards strapped to 0x77 work even though the base station asks for 0x76.
void test_bme280_finds_alternate_address() {
  FakeBus bus;
  bus.only_addr = 0x77;
  wx::Bme280 s(bus, 0x76, 30000);
  uint32_t now = 1000;
  run(s, now, 200);
  CHECK(s.reading().valid);
  CHECK_EQ(s.addr(), 0x77);
}

}  // namespace

void run_sensor_tests() {
  test_bme280_compensation();
  test_bme280_calib_parse();
  test_bme280_driver_reads();
  test_bme280_driver_recovers();
  test_bme280_absent_keeps_watchdog_fed();
  test_bme280_finds_alternate_address();
  test_bme280_bmp280_has_no_humidity();
  test_bme280_rejects_out_of_range();
  test_wind_gust_and_average();
  test_wind_direction_vector_average();
  test_wind_conversions();
}
