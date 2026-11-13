// Base station pin map: Raspberry Pi Pico 2 W (RP2350A + CYW43439).
// Wiring table and rationale: docs/plan.md, Step 0b.
//
// GP23, GP24, GP25 and GP29 belong to the WiFi chip on the Pico 2 W: never use them.
// The LoRa and I2C pins match outdoor/src/board.h so the drivers are shared.
#pragma once

#include <cstdint>

namespace board {

// --- LoRa module (Ebyte E220-style LLCC68, UART) ---------------------------
constexpr unsigned kLoraUart = 1;      // uart1
constexpr uint32_t kLoraBaud = 9600;
constexpr unsigned kLoraTx = 8;        // GP8  -> module RXD
constexpr unsigned kLoraRx = 9;        // GP9  <- module TXD
constexpr unsigned kLoraM0 = 10;       // GP10 -> M0
constexpr unsigned kLoraM1 = 11;       // GP11 -> M1
constexpr unsigned kLoraAux = 12;      // GP12 <- AUX
constexpr unsigned kLoraPowerEn = 13;  // GP13 -> load switch / MOSFET, high = module powered

// --- I2C sensors -------------------------------------------------------------
constexpr unsigned kI2c = 0;           // i2c0
constexpr unsigned kI2cSda = 4;        // GP4
constexpr unsigned kI2cScl = 5;        // GP5
constexpr uint32_t kI2cHz = 100000;    // short wires indoors
constexpr uint8_t kBme280Addr = 0x76;

// --- Setup / factory-reset button (to GND, internal pull-up) --------------
// Short press: open setup WiFi. Hold 10 s: wipe settings (not rain history).
constexpr unsigned kSetupButtonPin = 15;  // GP15

// On-board LED is on the WiFi chip: cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, ...).

}  // namespace board
