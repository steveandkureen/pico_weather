// Minimal test harness (no framework).
#pragma once

#include <cstdio>

extern int g_failures;

#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
      ++g_failures;                                                \
    }                                                              \
  } while (0)

#define CHECK_EQ(a, b)                                                                     \
  do {                                                                                     \
    auto _a = (a);                                                                         \
    auto _b = (b);                                                                         \
    if (!(_a == _b)) {                                                                     \
      std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b,    \
                  static_cast<long long>(_a), static_cast<long long>(_b));                 \
      ++g_failures;                                                                        \
    }                                                                                      \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                              \
  do {                                                                                     \
    double _a = static_cast<double>(a);                                                    \
    double _b = static_cast<double>(b);                                                    \
    if (_a - _b > (tol) || _b - _a > (tol)) {                                              \
      std::printf("FAIL %s:%d: %s ~= %s (%g vs %g)\n", __FILE__, __LINE__, #a, #b, _a, _b); \
      ++g_failures;                                                                        \
    }                                                                                      \
  } while (0)

#define CHECK_STR(a, b)                                                                    \
  do {                                                                                     \
    const char* _a = (a);                                                                  \
    const char* _b = (b);                                                                  \
    if (std::strcmp(_a, _b) != 0) {                                                        \
      std::printf("FAIL %s:%d: %s == %s (\"%s\" vs \"%s\")\n", __FILE__, __LINE__, #a, #b, \
                  _a, _b);                                                                 \
      ++g_failures;                                                                        \
    }                                                                                      \
  } while (0)

// Each test file defines one of these, called from main.
void run_core_tests();
void run_storage_tests();
void run_time_tests();
void run_sensor_tests();
void run_radio_tests();
void run_net_tests();
void run_station_tests();
void run_health_tests();
