#include "wx/wind.h"

#include <cmath>

namespace wx {
namespace {
constexpr float kPi = 3.14159265358979f;
}

void WindSampler::add_second(uint16_t pulses, float vane_deg) {
  last3_[0] = last3_[1];
  last3_[1] = last3_[2];
  last3_[2] = pulses;
  if (filled_ < 3) ++filled_;
  const uint32_t window = static_cast<uint32_t>(last3_[0]) + last3_[1] + last3_[2];
  if (window > gust_) gust_ = window;

  pulses_ += pulses;
  ++seconds_;

  if (vane_deg >= 0.0f) {
    const float r = vane_deg * kPi / 180.0f;
    const float x = std::cos(r), y = std::sin(r);
    sx_ += x;
    sy_ += y;
    sx_w_ += x * pulses;
    sy_w_ += y * pulses;
    ++vane_n_;
  }
}

WindSampler::Result WindSampler::take() {
  Result r;
  r.pulses = static_cast<uint16_t>(pulses_ > 0xFFFF ? 0xFFFF : pulses_);
  const uint32_t ms = seconds_ * 1000u;
  r.interval_ms = static_cast<uint16_t>(ms > 0xFFFF ? 0xFFFF : ms);
  r.gust_pulses_3s = static_cast<uint16_t>(gust_ > 0xFFFF ? 0xFFFF : gust_);
  r.dir_deg = 0xFFFF;
  if (vane_n_ > 0) {
    float x = sx_w_, y = sy_w_;
    if (std::fabs(x) < 1e-6f && std::fabs(y) < 1e-6f) {
      x = sx_;
      y = sy_;
    }
    if (std::fabs(x) >= 1e-6f || std::fabs(y) >= 1e-6f) {
      float deg = std::atan2(y, x) * 180.0f / kPi;
      if (deg < 0) deg += 360.0f;
      int d = static_cast<int>(deg + 0.5f) % 360;
      r.dir_deg = static_cast<uint16_t>(d);
    }
  }
  // New interval. The 3-second window carries over so a gust spanning the
  // boundary is still caught next time.
  seconds_ = 0;
  pulses_ = 0;
  gust_ = 0;
  sx_ = sy_ = sx_w_ = sy_w_ = 0;
  vane_n_ = 0;
  return r;
}

float as5600_adc_to_deg(uint16_t adc12) {
  if (adc12 > 4095) adc12 = 4095;
  return static_cast<float>(adc12) * 360.0f / 4096.0f;
}

uint32_t wind_mps_x100(uint32_t pulses, uint32_t interval_ms, uint32_t mmps_per_hz) {
  if (interval_ms == 0) return 0;
  // pulses/s * mm/s-per-Hz / 10 → (m/s × 100). 64-bit to avoid overflow.
  const uint64_t num = static_cast<uint64_t>(pulses) * mmps_per_hz * 1000u;
  return static_cast<uint32_t>(num / interval_ms / 10u);
}

uint16_t vane_true_deg(uint16_t raw_deg, int16_t offset_deg) {
  int d = (static_cast<int>(raw_deg) + offset_deg) % 360;
  if (d < 0) d += 360;
  return static_cast<uint16_t>(d);
}

}  // namespace wx
