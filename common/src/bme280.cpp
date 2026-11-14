#include "wx/bme280.h"

namespace wx {
namespace {

constexpr uint8_t kRegChipId = 0xD0;
constexpr uint8_t kRegReset = 0xE0;
constexpr uint8_t kRegCtrlHum = 0xF2;
constexpr uint8_t kRegCtrlMeas = 0xF4;
constexpr uint8_t kRegConfig = 0xF5;
constexpr uint8_t kRegData = 0xF7;
constexpr uint8_t kRegCalib1 = 0x88;
constexpr uint8_t kRegCalib2 = 0xE1;

// osrs_t ×1, osrs_p ×1, forced mode.
constexpr uint8_t kCtrlMeasForced = (1u << 5) | (1u << 2) | 0x01;
constexpr uint32_t kMeasureMs = 15;   // max conversion at ×1 oversampling is ~9.3 ms
constexpr uint32_t kResetMs = 5;
constexpr uint32_t kFailsBeforeInvalid = 3;

uint16_t u16le(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
int16_t s16le(const uint8_t* p) { return static_cast<int16_t>(u16le(p)); }

// "a is at or after b" on a wrapping millisecond counter.
bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }

}  // namespace

void bme280_parse_calib(const uint8_t* b, const uint8_t* e, Bme280Calib* c) {
  c->t1 = u16le(b + 0);
  c->t2 = s16le(b + 2);
  c->t3 = s16le(b + 4);
  c->p1 = u16le(b + 6);
  c->p2 = s16le(b + 8);
  c->p3 = s16le(b + 10);
  c->p4 = s16le(b + 12);
  c->p5 = s16le(b + 14);
  c->p6 = s16le(b + 16);
  c->p7 = s16le(b + 18);
  c->p8 = s16le(b + 20);
  c->p9 = s16le(b + 22);
  c->h1 = b[25];  // 0xA1
  c->h2 = s16le(e + 0);
  c->h3 = e[2];
  c->h4 = static_cast<int16_t>((static_cast<int8_t>(e[3]) * 16) | (e[4] & 0x0F));
  c->h5 = static_cast<int16_t>((static_cast<int8_t>(e[5]) * 16) | (e[4] >> 4));
  c->h6 = static_cast<int8_t>(e[6]);
}

int32_t bme280_temp_x100(const Bme280Calib& c, int32_t adc_t, int32_t* t_fine) {
  int32_t var1 = ((((adc_t >> 3) - (static_cast<int32_t>(c.t1) << 1))) * c.t2) >> 11;
  int32_t var2 = (((((adc_t >> 4) - static_cast<int32_t>(c.t1)) *
                    ((adc_t >> 4) - static_cast<int32_t>(c.t1))) >> 12) * c.t3) >> 14;
  *t_fine = var1 + var2;
  return (*t_fine * 5 + 128) >> 8;
}

uint32_t bme280_pressure_pa(const Bme280Calib& c, int32_t adc_p, int32_t t_fine) {
  int64_t var1 = static_cast<int64_t>(t_fine) - 128000;
  int64_t var2 = var1 * var1 * c.p6;
  var2 = var2 + ((var1 * c.p5) * 131072);
  var2 = var2 + (static_cast<int64_t>(c.p4) * 34359738368LL);
  var1 = ((var1 * var1 * c.p3) >> 8) + ((var1 * c.p2) * 4096);
  var1 = (((static_cast<int64_t>(1) << 47) + var1)) * c.p1 >> 33;
  if (var1 == 0) return 0;
  int64_t p = 1048576 - adc_p;
  p = (((p * 2147483648LL) - var2) * 3125) / var1;
  var1 = (static_cast<int64_t>(c.p9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (static_cast<int64_t>(c.p8) * p) >> 19;
  p = ((p + var1 + var2) >> 8) + (static_cast<int64_t>(c.p7) * 16);
  // p is Pa in Q24.8
  return static_cast<uint32_t>(p >> 8);
}

uint32_t bme280_humidity_x100(const Bme280Calib& c, int32_t adc_h, int32_t t_fine) {
  int32_t v = t_fine - 76800;
  v = (((((adc_h * 16384) - (static_cast<int32_t>(c.h4) * 1048576) - (static_cast<int32_t>(c.h5) * v)) +
         16384) >> 15) *
       (((((((v * c.h6) >> 10) * (((v * c.h3) >> 11) + 32768)) >> 10) + 2097152) * c.h2 + 8192) >> 14));
  v = v - (((((v >> 15) * (v >> 15)) >> 7) * c.h1) >> 4);
  if (v < 0) v = 0;
  if (v > 419430400) v = 419430400;
  const uint32_t q22_10 = static_cast<uint32_t>(v >> 12);  // %RH in Q22.10
  return (q22_10 * 100u + 512u) / 1024u;
}

bool Bme280::write_reg(uint8_t reg, uint8_t val) {
  const uint8_t buf[2] = {reg, val};
  return bus_.write(addr_, buf, 2);
}

void Bme280::fail(uint32_t now_ms) {
  ++errors_;
  ++consecutive_fail_;
  if (consecutive_fail_ >= kFailsBeforeInvalid) r_.valid = false;
  bus_.recover();
  // Back off 1 s, 2 s, 4 s ... up to 60 s, then re-probe from scratch.
  uint32_t backoff = 1000u << (consecutive_fail_ > 6 ? 6 : consecutive_fail_ - 1);
  if (backoff > 60000) backoff = 60000;
  wait_until_ms_ = now_ms + backoff;
  state_ = State::kBackoff;
}

void Bme280::poll(uint32_t now_ms) {
  // Count every poll, including while backing off from a missing sensor; the
  // watchdog gate only needs to know that poll() keeps returning. (Backoff
  // reaches 60 s, longer than the 15 s stall limit.)
  ++progress_;
  switch (state_) {
    case State::kBackoff:
      if (!reached(now_ms, wait_until_ms_)) return;
      state_ = State::kProbe;
      return;

    case State::kProbe: {
      uint8_t id = 0;
      if (!bus_.read_reg(addr_, kRegChipId, &id, 1) ||
          (id != kChipIdBme280 && id != kChipIdBmp280)) {
        fail(now_ms);
        return;
      }
      chip_id_ = id;
      if (!write_reg(kRegReset, 0xB6)) {
        fail(now_ms);
        return;
      }
      wait_until_ms_ = now_ms + kResetMs;
      state_ = State::kResetWait;
      return;
    }

    case State::kResetWait:
      if (!reached(now_ms, wait_until_ms_)) return;
      state_ = State::kCalib;
      return;

    case State::kCalib: {
      uint8_t b88[26];
      uint8_t be1[7] = {0};
      if (!bus_.read_reg(addr_, kRegCalib1, b88, sizeof(b88))) {
        fail(now_ms);
        return;
      }
      if (chip_id_ == kChipIdBme280 && !bus_.read_reg(addr_, kRegCalib2, be1, sizeof(be1))) {
        fail(now_ms);
        return;
      }
      bme280_parse_calib(b88, be1, &calib_);
      if (calib_.t1 == 0 || calib_.p1 == 0) {  // all-zero calibration = bad read
        fail(now_ms);
        return;
      }
      state_ = State::kConfigure;
      return;
    }

    case State::kConfigure:
      // Humidity ×1 (must be written before ctrl_meas), no IIR filter, sleep.
      if ((chip_id_ == kChipIdBme280 && !write_reg(kRegCtrlHum, 0x01)) ||
          !write_reg(kRegConfig, 0x00) || !write_reg(kRegCtrlMeas, 0x00)) {
        fail(now_ms);
        return;
      }
      state_ = State::kIdle;
      next_ms_valid_ = false;
      return;

    case State::kIdle:
      if (next_ms_valid_ && !reached(now_ms, next_ms_)) return;
      if (!write_reg(kRegCtrlMeas, kCtrlMeasForced)) {
        fail(now_ms);
        return;
      }
      wait_until_ms_ = now_ms + kMeasureMs;
      state_ = State::kMeasuring;
      return;

    case State::kMeasuring: {
      if (!reached(now_ms, wait_until_ms_)) return;
      uint8_t d[8];
      if (!bus_.read_reg(addr_, kRegData, d, sizeof(d))) {
        fail(now_ms);
        return;
      }
      const int32_t adc_p = (d[0] << 12) | (d[1] << 4) | (d[2] >> 4);
      const int32_t adc_t = (d[3] << 12) | (d[4] << 4) | (d[5] >> 4);
      const int32_t adc_h = (d[6] << 8) | d[7];
      // 0x80000 / 0x8000 = "measurement skipped" (sensor not actually measuring).
      if (adc_t == 0x80000 || adc_p == 0x80000) {
        fail(now_ms);
        return;
      }
      int32_t t_fine = 0;
      const int32_t t = bme280_temp_x100(calib_, adc_t, &t_fine);
      const uint32_t p = bme280_pressure_pa(calib_, adc_p, t_fine);
      if (t < -6000 || t > 8500 || p < 30000 || p > 110000) {  // outside the sensor's range
        fail(now_ms);
        return;
      }
      r_.temp_c_x100 = static_cast<int16_t>(t);
      r_.pressure_pa = p;
      r_.has_humidity = chip_id_ == kChipIdBme280 && adc_h != 0x8000;
      r_.humidity_x100 =
          r_.has_humidity ? static_cast<uint16_t>(bme280_humidity_x100(calib_, adc_h, t_fine)) : 0;
      r_.at_ms = now_ms;
      r_.valid = true;
      consecutive_fail_ = 0;
      next_ms_ = now_ms + interval_ms_;
      next_ms_valid_ = true;
      state_ = State::kIdle;
      return;
    }
  }
}

}  // namespace wx
