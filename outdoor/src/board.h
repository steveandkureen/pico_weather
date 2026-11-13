// Outdoor station pin map: Raspberry Pi Pico 2 (RP2350A).
// Wiring table and rationale: docs/plan.md, Step 0b.
//
// The LoRa and I2C pins match base/src/board.h so the drivers are shared.
#pragma once

#include <cstdint>

namespace board {

// --- LoRa module (Ebyte E220-style LLCC68, UART) ---------------------------
constexpr unsigned kLoraUart = 1;      // uart1
constexpr uint32_t kLoraBaud = 9600;   // module default; may raise later
constexpr unsigned kLoraTx = 8;        // GP8  -> module RXD
constexpr unsigned kLoraRx = 9;        // GP9  <- module TXD
constexpr unsigned kLoraM0 = 10;       // GP10 -> M0 (mode select)
constexpr unsigned kLoraM1 = 11;       // GP11 -> M1 (mode select)
constexpr unsigned kLoraAux = 12;      // GP12 <- AUX (busy, low = busy)
constexpr unsigned kLoraPowerEn = 13;  // GP13 -> load switch / MOSFET, high = module powered

// --- I2C sensors -------------------------------------------------------------
constexpr unsigned kI2c = 0;           // i2c0
constexpr unsigned kI2cSda = 4;        // GP4
constexpr unsigned kI2cScl = 5;        // GP5
constexpr uint32_t kI2cHz = 50000;     // slow on purpose: sensor wires run out to the shield
constexpr uint8_t kBme280Addr = 0x76;  // 0x77 if the module's SDO jumper is high

// --- Pulse sensors (Hall switches, open-drain, external 10k pull-up to 3V3) -
constexpr unsigned kAnemometerPin = 14;  // GP14, falling edge per magnet pass
constexpr unsigned kRainGaugePin = 15;   // GP15, falling edge per bucket tip

// --- Analog --------------------------------------------------------------
constexpr unsigned kWindVanePin = 26;    // GP26 / ADC0 <- AS5600 OUT (0..3V3 = 0..360°)
constexpr unsigned kBatteryPin = 27;     // GP27 / ADC1 <- battery via 100k/100k divider
constexpr unsigned kSolarPin = 28;       // GP28 / ADC2 <- panel via 220k/47k divider
// GP29 / ADC3 is wired on the Pico 2 board itself to VSYS/3 (the Pico's own supply).
constexpr unsigned kVsysPin = 29;

constexpr float kBatteryDivider = 2.0f;           // 100k / 100k
constexpr float kSolarDivider = (220.0f + 47.0f) / 47.0f;
constexpr float kVsysDivider = 3.0f;

// --- On-board ------------------------------------------------------------
constexpr unsigned kLedPin = 25;  // Pico 2 on-board LED

}  // namespace board
