// /config page <-> Settings. Kept out of the firmware so it can be tested.
#pragma once

#include <cstddef>

#include "wx/json.h"
#include "wx/settings.h"

namespace wx {

struct ConfigResult {
  bool ok;
  const char* error;       // message for the page when !ok
  bool wifi_changed;       // ssid or password changed → rejoin
  bool hostname_changed;   // → rename mDNS / DHCP hostname
  bool location_changed;   // → fetch a new forecast now
};

// Applies the submitted form to `s`. Fields that are absent are left alone;
// an empty password means "keep the current one". If a PIN is set, the form
// must carry it as `pin`. On error `s` is unchanged.
ConfigResult apply_config_form(Settings* s, const char* form, size_t len);

// Settings for the page. Never includes the WiFi password or the PIN itself.
void write_settings_json(const Settings& s, JsonWriter& w);

// Checks the `pin` field of a form against the settings (true if no PIN set).
bool pin_ok(const Settings& s, const char* form, size_t len);

}  // namespace wx
