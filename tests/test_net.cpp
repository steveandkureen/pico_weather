// JSON writer/reader, forecast parser (real Open-Meteo reply), HTTP parsing.
#include <cstdio>
#include <cstring>
#include <string>

#include "test_util.h"
#include "wx/calendar.h"
#include "wx/forecast.h"
#include "wx/http.h"
#include "wx/json.h"

#ifndef WX_FIXTURE_DIR
#define WX_FIXTURE_DIR "."
#endif

namespace {

std::string read_file(const char* name) {
  std::string path = std::string(WX_FIXTURE_DIR) + "/" + name;
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return "";
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
  std::fclose(f);
  return s;
}

void test_json_writer() {
  char buf[256];
  wx::JsonWriter w(buf, sizeof(buf));
  w.begin_object();
  w.key("a").value(int64_t{-12});
  w.key("b").fixed(-1205, 2);
  w.key("c").fixed(5, 2);
  w.key("s").value("q\"\\\n");
  w.key("arr").begin_array().value(int64_t{1}).null().boolean(true).end_array();
  w.key("o").begin_object().end_object();
  w.end_object();
  CHECK(w.ok());
  CHECK_STR(buf, "{\"a\":-12,\"b\":-12.05,\"c\":0.05,\"s\":\"q\\\"\\\\\\u000a\",\"arr\":[1,null,true],\"o\":{}}");

  char small[8];
  wx::JsonWriter w2(small, sizeof(small));
  w2.begin_object().key("long_key").value(int64_t{1}).end_object();
  CHECK(!w2.ok());
  CHECK(std::strlen(small) < sizeof(small));
}

struct Collected {
  int count = 0;
  int64_t last_num = 0;
  bool saw_nested = false;
  bool saw_idx2 = false;
};

void collect(void* u, const wx::JsonSlice* path, int depth, int index, const wx::JsonValue& v) {
  Collected* c = static_cast<Collected*>(u);
  ++c->count;
  if (v.type == wx::JsonType::kNumber) c->last_num = v.milli;
  if (depth == 2 && path[0].equals("x") && path[1].equals("y")) c->saw_nested = true;
  if (index == 2 && path[depth - 1].equals("list")) c->saw_idx2 = true;
}

void test_json_walk() {
  const char* doc = "{\"x\":{\"y\":true},\"list\":[1,2.5,-3e2],\"s\":\"a\\\"b\",\"n\":null}";
  Collected c;
  CHECK(wx::json_walk(doc, std::strlen(doc), &collect, &c));
  CHECK_EQ(c.count, 6);
  CHECK(c.saw_nested);
  CHECK(c.saw_idx2);
  CHECK(!wx::json_walk("{\"a\":", 5, &collect, &c));
  CHECK(!wx::json_walk("{\"a\":1}x", 8, &collect, &c));
  CHECK(!wx::json_walk("[[[[[[[[[[1]]]]]]]]]]", 21, &collect, &c));  // too deep
}

void test_forecast_real_reply() {
  std::string body = read_file("open_meteo_11day.json");
  CHECK(!body.empty());
  wx::Forecast f;
  CHECK(wx::forecast_parse(body.data(), body.size(), &f));
  CHECK(f.valid);
  CHECK_EQ(f.ndays, 11);
  CHECK_EQ(f.utc_offset_s, -21600);
  CHECK_EQ(f.elevation_m, 688);
  CHECK_STR(f.timezone, "America/Chicago");
  CHECK_EQ(f.days[0].day, wx::days_from_civil(2026, 11, 12));
  CHECK_EQ(f.days[10].day, wx::days_from_civil(2026, 11, 22));
  for (int i = 0; i < f.ndays; ++i) {
    CHECK(f.days[i].tmax_c_x10 >= f.days[i].tmin_c_x10);
    CHECK(f.days[i].sunrise_min > 5 * 60 && f.days[i].sunrise_min < 9 * 60);
    CHECK(f.days[i].sunset_min > 16 * 60 && f.days[i].sunset_min < 19 * 60);
  }
}

void test_forecast_bad_replies() {
  wx::Forecast f;
  const char* empty = "{}";
  CHECK(!wx::forecast_parse(empty, 2, &f));
  const char* trunc = "{\"daily\":{\"time\":[\"2026-11-12\"],\"temperature_2m_max\":[1";
  CHECK(!wx::forecast_parse(trunc, std::strlen(trunc), &f));
  // Missing temperatures for day 2: only day 1 counts.
  const char* partial =
      "{\"daily\":{\"time\":[\"2026-11-12\",\"2026-11-13\"],\"temperature_2m_max\":[10.5,null],"
      "\"temperature_2m_min\":[1.2,null],\"weather_code\":[3,61]}}";
  CHECK(wx::forecast_parse(partial, std::strlen(partial), &f));
  CHECK_EQ(f.ndays, 1);
  CHECK_EQ(f.days[0].tmax_c_x10, 105);
  CHECK_EQ(f.days[0].code, 3);
  CHECK_EQ(f.days[0].sunrise_min, -1);
}

void test_forecast_path_and_icons() {
  char path[512];
  CHECK(wx::forecast_request_path(3974392, -10498760, path, sizeof(path)) > 0);
  CHECK(std::strstr(path, "latitude=39.74392&longitude=-104.98760") != nullptr);
  CHECK(std::strstr(path, "forecast_days=11") != nullptr);
  CHECK(wx::forecast_request_path(1, 1, path, 20) == 0);
  CHECK(wx::weather_icon(0) == wx::WeatherIcon::kSun);
  CHECK(wx::weather_icon(2) == wx::WeatherIcon::kPartly);
  CHECK(wx::weather_icon(63) == wx::WeatherIcon::kRain);
  CHECK(wx::weather_icon(73) == wx::WeatherIcon::kSnow);
  CHECK(wx::weather_icon(95) == wx::WeatherIcon::kStorm);
}

void test_http_parse() {
  char buf[512];
  const char* req = "GET /data.json?x=1 HTTP/1.1\r\nHost: weather.local\r\nUser-Agent: t\r\n\r\n";
  std::strcpy(buf, req);
  wx::HttpRequest r;
  CHECK(wx::http_parse(buf, 10, sizeof(buf), &r) == wx::HttpParse::kIncomplete);
  CHECK(wx::http_parse(buf, std::strlen(req), sizeof(buf), &r) == wx::HttpParse::kOk);
  CHECK(r.method == wx::HttpMethod::kGet);
  CHECK_STR(r.path, "/data.json");
  CHECK_STR(r.query, "x=1");
  CHECK_STR(r.host, "weather.local");

  const char* post =
      "POST /api/config HTTP/1.1\r\nhost: 192.168.4.1\r\nContent-Length: 26\r\n\r\nssid=My+Net&pass=a%26b%3Dc";
  std::strcpy(buf, post);
  const size_t full = std::strlen(post);
  CHECK(wx::http_parse(buf, full - 5, sizeof(buf), &r) == wx::HttpParse::kIncomplete);
  CHECK(wx::http_parse(buf, full, sizeof(buf), &r) == wx::HttpParse::kOk);
  CHECK(r.method == wx::HttpMethod::kPost);
  CHECK_EQ(r.body_len, 26u);
  char v[32];
  CHECK(wx::form_get(r.body, r.body_len, "ssid", v, sizeof(v)));
  CHECK_STR(v, "My Net");
  CHECK(wx::form_get(r.body, r.body_len, "pass", v, sizeof(v)));
  CHECK_STR(v, "a&b=c");
  CHECK(!wx::form_get(r.body, r.body_len, "ss", v, sizeof(v)));

  std::strcpy(buf, "GARBAGE\r\n\r\n");
  CHECK(wx::http_parse(buf, std::strlen(buf), sizeof(buf), &r) == wx::HttpParse::kBad);
  std::memset(buf, 'a', sizeof(buf));
  CHECK(wx::http_parse(buf, sizeof(buf), sizeof(buf), &r) == wx::HttpParse::kTooBig);
  std::strcpy(buf, "POST / HTTP/1.1\r\nContent-Length: 99999\r\n\r\n");
  CHECK(wx::http_parse(buf, std::strlen(buf), sizeof(buf), &r) == wx::HttpParse::kTooBig);
}

void test_form_numbers() {
  int32_t v = 0;
  CHECK(wx::parse_fixed("-104.98765", 5, &v));
  CHECK_EQ(v, -10498765);
  CHECK(wx::parse_fixed("39.7", 5, &v));
  CHECK_EQ(v, 3970000);
  CHECK(wx::parse_fixed("5280", 0, &v));
  CHECK_EQ(v, 5280);
  CHECK(wx::parse_fixed("0.011", 3, &v));
  CHECK_EQ(v, 11);
  CHECK(!wx::parse_fixed("abc", 2, &v));
  CHECK(!wx::parse_fixed("1.2.3", 2, &v));
  CHECK(!wx::parse_fixed("", 2, &v));
  const char* form = "a=12&b=-3&c=x";
  CHECK(wx::form_get_int(form, std::strlen(form), "b", &v));
  CHECK_EQ(v, -3);
  CHECK(!wx::form_get_int(form, std::strlen(form), "c", &v));
}

}  // namespace

void run_net_tests() {
  test_json_writer();
  test_json_walk();
  test_forecast_real_reply();
  test_forecast_bad_replies();
  test_forecast_path_and_icons();
  test_http_parse();
  test_form_numbers();
}
