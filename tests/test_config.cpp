// /config form handling.
#include <cstring>

#include "test_util.h"
#include "wx/config_form.h"
#include "wx/json.h"

namespace {

wx::ConfigResult apply(wx::Settings* s, const char* form) {
  return wx::apply_config_form(s, form, std::strlen(form));
}

void test_full_form() {
  wx::Settings s;
  wx::settings_defaults(&s);
  wx::ConfigResult r = apply(&s,
      "ssid=Home+Net&pass=supersecret&hostname=wx&tz=MST7&lat=33.44838&lon=-112.07404"
      "&elev=1086&elev_unit=ft&temp_unit=C&press_unit=hPa&wind_unit=km%2Fh&rain_unit=mm"
      "&layout=auto&day_layout=C&night_layout=A&rain_um=200&wind_mmps=700&vane_offset=-15");
  CHECK(r.ok);
  CHECK(r.wifi_changed);
  CHECK(r.hostname_changed);
  CHECK(r.location_changed);
  CHECK_STR(s.wifi_ssid, "Home Net");
  CHECK_STR(s.wifi_pass, "supersecret");
  CHECK_STR(s.hostname, "wx");
  CHECK_STR(s.tz, "MST7");
  CHECK_EQ(s.lat_e5, 3344838);
  CHECK_EQ(s.lon_e5, -11207404);
  CHECK_EQ(s.elevation_cm, 33101);  // 1086 ft
  CHECK(s.temp_unit == wx::TempUnit::kC);
  CHECK(s.press_unit == wx::PressUnit::kHPa);
  CHECK(s.wind_unit == wx::WindUnit::kKmh);
  CHECK(s.rain_unit == wx::RainUnit::kMm);
  CHECK_EQ(s.day_layout, 'C');
  CHECK_EQ(s.night_layout, 'A');
  CHECK_EQ(s.rain_um_per_tip, 200u);
  CHECK_EQ(s.wind_mmps_per_hz, 700u);
  CHECK_EQ(s.vane_offset_deg, -15);

  // Same values again: nothing "changed"; empty password keeps the old one.
  r = apply(&s, "ssid=Home+Net&pass=&hostname=wx&lat=33.44838&lon=-112.07404");
  CHECK(r.ok);
  CHECK(!r.wifi_changed);
  CHECK(!r.hostname_changed);
  CHECK(!r.location_changed);
  CHECK_STR(s.wifi_pass, "supersecret");
}

void test_errors_leave_settings_untouched() {
  wx::Settings s;
  wx::settings_defaults(&s);
  wx::Settings before = s;
  CHECK(!apply(&s, "hostname=bad name").ok);
  CHECK(!apply(&s, "pass=short").ok);
  CHECK(!apply(&s, "lat=91&lon=0").ok);
  CHECK(!apply(&s, "lat=10").ok);
  CHECK(!apply(&s, "tz=nonsense").ok);
  CHECK(!apply(&s, "temp_unit=K").ok);
  CHECK(!apply(&s, "layout=E").ok);
  CHECK(!apply(&s, "hostname=ok&new_pin=12").ok);
  CHECK(std::memcmp(&s, &before, sizeof(s)) == 0);
}

void test_pin() {
  wx::Settings s;
  wx::settings_defaults(&s);
  CHECK(apply(&s, "new_pin=4321").ok);
  CHECK_STR(s.pin, "4321");
  CHECK(!apply(&s, "hostname=other").ok);  // no PIN given
  CHECK(!apply(&s, "pin=1111&hostname=other").ok);
  CHECK(apply(&s, "pin=4321&hostname=other").ok);
  CHECK_STR(s.hostname, "other");
  CHECK(apply(&s, "pin=4321&new_pin=").ok);  // remove PIN
  CHECK_STR(s.pin, "");
}

void test_settings_json_hides_secrets() {
  wx::Settings s;
  wx::settings_defaults(&s);
  std::strcpy(s.wifi_pass, "topsecret99");
  std::strcpy(s.pin, "9876");
  char buf[1024];
  wx::JsonWriter w(buf, sizeof(buf));
  wx::write_settings_json(s, w);
  CHECK(w.ok());
  CHECK(std::strstr(buf, "topsecret99") == nullptr);
  CHECK(std::strstr(buf, "9876") == nullptr);
  CHECK(std::strstr(buf, "\"has_pass\":true") != nullptr);
  CHECK(std::strstr(buf, "\"has_pin\":true") != nullptr);
  CHECK(wx::json_walk(buf, std::strlen(buf), nullptr, nullptr));
}

}  // namespace

void run_config_tests() {
  test_full_form();
  test_errors_leave_settings_untouched();
  test_pin();
  test_settings_json_hides_secrets();
}
