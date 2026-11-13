#include <cstdio>

#include "test_util.h"

int g_failures = 0;

int main() {
  run_core_tests();
  run_storage_tests();
  run_time_tests();
  run_sensor_tests();
  run_radio_tests();
  run_net_tests();
  run_station_tests();
  run_health_tests();
  run_config_tests();

  if (g_failures == 0) {
    std::printf("all tests passed\n");
    return 0;
  }
  std::printf("%d failure(s)\n", g_failures);
  return 1;
}
