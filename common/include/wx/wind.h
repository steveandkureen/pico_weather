// Wind statistics for the outdoor station, fed once per second.
//
// Speed: anemometer pulses are counted per 1-second slot. Over each send
// interval we report the total pulses (→ average speed) and the highest sum
// over any 3 consecutive seconds (→ gust, the usual definition).
//
// Direction: vane angles are averaged as vectors, so 350° and 10° average to
// 0°, not 180°. Samples are weighted by that second's pulses, so a vane
// flopping around in calm air doesn't skew the result; if there were no
// pulses at all, every sample counts equally.
#pragma once

#include <cstdint>

namespace wx {

class WindSampler {
 public:
  // One second's data. vane_deg < 0 means no valid vane reading.
  void add_second(uint16_t pulses, float vane_deg);

  struct Result {
    uint16_t pulses;
    uint16_t interval_ms;
    uint16_t gust_pulses_3s;
    uint16_t dir_deg;  // 0xFFFF if no vane data
  };

  // Returns stats since the last take() and starts a new interval.
  Result take();

  uint32_t seconds() const { return seconds_; }

 private:
  uint16_t last3_[3] = {0, 0, 0};
  uint32_t filled_ = 0;
  uint32_t seconds_ = 0;
  uint32_t pulses_ = 0;
  uint32_t gust_ = 0;
  float sx_w_ = 0, sy_w_ = 0;  // pulse-weighted vector sums
  float sx_ = 0, sy_ = 0;      // unweighted vector sums
  uint32_t vane_n_ = 0;
};

// AS5600 analog output (0..VDD over 0..360°) → degrees, from a 12-bit ADC count.
float as5600_adc_to_deg(uint16_t adc12);

// Converts a pulse count over an interval into m/s ×100 using the calibration.
uint32_t wind_mps_x100(uint32_t pulses, uint32_t interval_ms, uint32_t mmps_per_hz);

// Raw vane angle + offset → 0..359.
uint16_t vane_true_deg(uint16_t raw_deg, int16_t offset_deg);

}  // namespace wx
