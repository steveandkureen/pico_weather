# pico_weather — Project Plan

**Goal:** a weather station that runs for years without anyone touching it, especially the outdoor unit.
Whenever there's a trade-off, stability wins over features and convenience.

## System at a glance

```
 OUTDOOR (Pico 2, no WiFi)                       INDOOR (Pico 2 W)                      WALL
 ┌──────────────────────────┐   LoRa, 35-byte    ┌──────────────────────────────┐ WiFi  ┌────────┐
 │ temp / humidity / press. │   packet every     │ receive + validate packets   │──────▶│ iPad 4 │
 │ wind speed + gust + dir  │   30 s             │ indoor temp / humidity / prs │ HTTP  │ Safari │
 │ rain tip counter         │ ─────────────────▶ │ rain history in flash        │       │ iOS 10 │
 │ battery voltage          │                    │ NTP clock, web page + JSON   │       └────────┘
 │ no saved state           │                    │ health / uptime stats        │
 └──────────────────────────┘                    └──────────────────────────────┘
```

**Locked decisions**

| Decision | Choice | Why |
|---|---|---|
| Language / SDK | C++17 on the official Pico SDK (2.2.0 at `~/src/pico-sdk`) | Most proven WiFi driver and network stack; we control every allocation |
| Who keeps history | Base station only; the outdoor unit sends current conditions | The outdoor unit can reboot at any time and lose nothing |
| Rain in the packet | Running tip count since boot, plus a random boot ID | Lost packets and reboots on either side never lose rain |
| Rain history | Daily totals for 400 days, saved in flash | Today / month / year are sums of those days, and a daily chart comes free |
| Stability rules | Watchdog on both boards, no heap after startup, nothing in the main loop blocks, every sensor call has a time limit | Avoids the "reboot it every few weeks" failure |

**Firmware architecture:** one core, one super loop, no RTOS (details below).

### Firmware architecture

**One core, one cooperative super loop, no RTOS, on both boards.** Core 1 is left off.

```
main():
  init everything, enable watchdog
  loop forever:
    now = ms since boot
    radio.poll(now)      // read UART ring buffer, rebuild packets, recovery steps
    sensors.poll(now)    // BME280 state machine: start reading → wait → read → sleep
    wind.poll(now)       // outdoor: 1-s pulse buckets → 3-s gusts, vane samples
    wifi.poll(now)       // base: link supervision and recovery (async join)
    net.poll()           // base: cyw43_arch_poll() runs lwIP: web, mDNS, NTP, forecast
    storage.poll(now)    // base: save ledger/settings when due
    health.check(now)    // every task made progress? → watchdog_update()
    sleep until next timer or interrupt (≤ 10–50 ms)
```

**Rules every task follows**
- **`poll()` never waits.** It does a small step and returns, aiming for under 5 ms. Anything slow is a state machine. Example: the BME280 needs ~10 ms to measure, so `poll` starts the reading and records the time; a later `poll` collects the result. No `sleep_ms` inside tasks.
- **Every wait has a deadline.** I2C calls use the SDK's `*_blocking_until` versions with a few-ms limit; the radio's AUX pin, a WiFi join, a forecast fetch: each has a timeout, and each timeout leads to that task's own recovery steps.
- **The watchdog is fed only if every task checked in.** Each task bumps a "made progress" counter; `health.check` feeds the watchdog only when all of them moved recently. A task stuck in a bad state while still being polled, such as a radio state machine waiting forever, still causes a reset, instead of hiding behind a loop that keeps spinning.
- **No heap after startup.** All buffers are fixed and allocated at boot.
- **The loop measures itself:** the longest loop pass and each task's worst `poll` time are reported on `/config`, so a slow task shows up before it causes trouble.

**Interrupts: small and few**
- Outdoor: two GPIO edge interrupts (anemometer, rain) that only timestamp and count.
- Both: the UART receive interrupt copies bytes into a ring buffer (the LoRa module talks at 9600 baud; the hardware FIFO alone would overflow during a long flash write).
- Base: WiFi uses the SDK's **poll** mode (`pico_cyw43_arch_lwip_poll`), so the network stack only ever runs from the main loop. Nothing in the network stack can be interrupted halfway by other network code.

**Why not two cores or FreeRTOS?**
- The workload is tiny: a packet every 30 s, a page fetch every 10 s, sensor reads every few seconds. One 150 MHz core will sit idle more than 99% of the time.
- Splitting across cores means shared data between them, locks and the chance of race conditions: rare, timing-dependent bugs that show up after three weeks. That's the sprinkler-timer failure we're trying to avoid.
- **Flash writes stall both cores anyway.** While erasing or writing flash, code can't run from flash, so with two cores the other one has to be paused (`multicore_lockout`). With one core it's just: save right after a LoRa packet arrives (the next one is ~30 s away), with interrupts briefly off.
- An RTOS adds per-task stacks (each can overflow), priorities and preemption. A cooperative loop has exactly one stack and one order of events, so a problem seen once can be reproduced.
- If a real need ever appears (e.g. TLS for the forecast taking too long), core 1 can be given one isolated job with a single one-way queue. It's not needed now.

---

## Status (2026-11-13)

**All firmware for Steps 2–5 is written, builds cleanly, and the hardware-independent logic passes PC tests. None of it has run on a real Pico yet.** Each step below stays open until its "Done when" check passes on hardware.

| Step | Code | Tested on PC | Tested on hardware |
|---|---|---|---|
| 2 · Base core (flash, NTP, web, setup mode, forecast, mDNS) | ✅ | logic ✅ (flash store with power cuts, settings, TZ/DST, forecast parser on a real reply, JSON, problems); pages ✅ in a desktop browser | ⬜ |
| 3 · Indoor BME280 | ✅ | ✅ (driver against a simulated sensor, incl. unplug/replug) | ⬜ |
| 4 · LoRa link (E220 UART) | ✅ | ✅ (driver against a simulated module: factory config, hangs, power cycles) | ⬜ (modules in shipping) |
| 5 · Outdoor sensors | ✅ | wind/rain math ✅ | ⬜ |

**Things to check first on hardware:**
- **E220 register values and timings:** written from the manual. Verify the config read-back on first power-up (the USB log prints it).
- **The ~1.7 kB Open-Meteo reply and the forecast request:** confirmed from this PC over raw HTTP/1.0. On the Pico, check that DNS + TCP work through the home network.
- **Setup mode captive page on an iPhone:** the redirect logic is in place; confirm the sheet pops up.
- **The iPad 4's real Safari:** the pages were checked in a desktop browser and scanned for ES5-only syntax, but not on iOS 10 itself.

---

## Step 0 — Lock down the hardware  ⬜ *needs your decisions*

Proposed parts list (✅ = confirmed, 🔶 = proposed, waiting for your OK):

| Part | Proposed | Alternative | Status |
|---|---|---|---|
| Outdoor board | Raspberry Pi Pico 2 | — | ✅ |
| Indoor board | Raspberry Pi Pico 2 W | — | ✅ |
| Outdoor temp/humidity/pressure | **Qoroos BME280** (I2C 0x76/0x77). Forced mode (one reading, then sleep) to avoid self-heating; firmware checks chip ID 0x60 to catch fake BMP280 boards | SHT45 if humidity drifts later | ✅ |
| Wind speed + rain pulses | **HiLetgo A3144 Hall switch** module, magnet on rotor / bucket. ⚠️ Rated 4.5–24 V: power it from 5 V with an open-collector pull-up to 3.3 V, or swap to TI DRV5032 (3.3 V) | DRV5032 / DRV5023 | ✅ (power TBD) |
| Wind direction | **WWZMDiB AS5600** magnetic encoder, 3.3 V (I2C 0x36). Use its analog OUT on an ADC pin if the wire to the Pico is long | — | ✅ |
| Anemometer / vane / rain gauge mechanics | DIY (Hall + magnet, AS5600 + magnet) | — | ⬜ describe |
| LoRa radio (×2) | **Semtech LLCC68 UART module, 868/915 MHz** (model TBD, likely Ebyte E220-900T22D). Module runs its own firmware; the Pico talks to it over UART, using the AUX/M0/M1 pins | — | ✅ (model TBD) |
| LoRa power control | GPIO-switched MOSFET on the module's supply, so the Pico can power-cycle a stuck module | — | 🔶 |
| Indoor temp/humidity/pressure | **Qoroos BME280** (same driver as outdoor) | — | ✅ |
| Housings (shield, anemometer, vane, rain gauge, electronics box) | **3D-printed by you.** See Step 6b | — | ⬜ to design |
| Outdoor power | **Solar + battery** (not ordered yet). Proposed: LiFePO4 cell + solar charge controller with load output + 5–6 V panel; sizing in Step 6 | — | ✅ type / ⬜ parts |
| Electrical protection | Surge/static protection on the long sensor wires, cable glands | — | 🔶 |

**Parts on hand / ordering status (2026-11-12):** LoRa modules are being shipped; solar and battery are not ordered. Steps 1–3 need none of it and can be done indoors now.

Other decisions still open:
- [x] **Region / frequency:** US, 915 MHz ISM band (902–928 MHz). Run the module in that band only.
- [x] **Display units:** set on the `/config` page (see "Units" in Step 2); US units are the default.
- [x] **Rain year:** calendar year, resets Jan 1 (already how `RainLedger::year_tips` works).
- [x] **Station elevation:** entered on the `/config` page (feet or meters, following the units setting) and saved in flash. Used to show sea-level pressure, the number weather reports use; the page can also show the raw station pressure. Until it's set, the page shows station pressure labeled "uncorrected".
- [ ] **Power:** solar vs wired (see above).
- [ ] **Distance and obstacles** between the station and the house: decides antenna and LoRa settings.

**Done when:** every row above is ✅ and the pin map (Step 0b) is written.

### Step 0b — Pin map  ✅

In code: [`outdoor/src/board.h`](../outdoor/src/board.h) and [`base/src/board.h`](../base/src/board.h). **LoRa and I2C use the same pins on both boards**, so the same driver code runs on both.

**Outdoor: Pico 2**

| Function | Pico pin (GPIO) | Physical pin | Connects to | Notes |
|---|---|---|---|---|
| LoRa UART TX | GP8 (UART1 TX) | 11 | module RXD | |
| LoRa UART RX | GP9 (UART1 RX) | 12 | module TXD | |
| LoRa M0 | GP10 | 14 | module M0 | mode select |
| LoRa M1 | GP11 | 15 | module M1 | mode select |
| LoRa AUX | GP12 | 16 | module AUX | busy flag (input) |
| LoRa power enable | GP13 | 17 | load switch EN | high = radio on; lets firmware power-cycle a stuck module |
| I2C SDA | GP4 (I2C0) | 6 | BME280 SDA | 50 kHz, twisted pair to the shield |
| I2C SCL | GP5 (I2C0) | 7 | BME280 SCL | |
| Anemometer | GP14 | 19 | Hall sensor OUT | 10k pull-up to 3V3 + 1 nF to GND at the Pico |
| Rain gauge | GP15 | 20 | Hall sensor OUT | 10k pull-up to 3V3 + 1 nF to GND; debounced in software |
| Wind vane | GP26 (ADC0) | 31 | AS5600 OUT | analog 0–3.3 V = 0–360°; AS5600 DIR → GND |
| Battery voltage | GP27 (ADC1) | 32 | battery + via 100k/100k divider | ~18 µA drain; fine for solar |
| Solar voltage | GP28 (ADC2) | 34 | panel + via 220k/47k divider | shows whether it's charging |
| Pico supply | GP29 (ADC3) | internal | VSYS/3 on the Pico board | nothing to wire |
| Status LED | GP25 | on-board | | brief blink on send for the first 10 min after boot only |
| Running LED | GP16 | 21 | 330 Ω → LED → GND (pin 23) | 1 s on, 2 s off from the main loop; stops if the firmware hangs |
| 3.3 V out | 3V3 OUT | 36 | BME280, AS5600, Hall sensors, load switch → LoRa | 300 mA max; peak LoRa TX ~110 mA |
| Ground | GND | 38 (any GND) | all | |
| Power in | VSYS | 39 | solar charge controller load output | Pico's onboard regulator accepts 1.8–5.5 V |

**Base: Pico 2 W**

| Function | Pico pin (GPIO) | Physical pin | Connects to | Notes |
|---|---|---|---|---|
| LoRa UART TX / RX | GP8 / GP9 (UART1) | 11 / 12 | module RXD / TXD | same as outdoor |
| LoRa M0 / M1 / AUX | GP10 / GP11 / GP12 | 14 / 15 / 16 | module | same as outdoor |
| LoRa power enable | GP13 | 17 | load switch EN | same as outdoor |
| I2C SDA / SCL | GP4 / GP5 (I2C0) | 6 / 7 | BME280 | 100 kHz, short wires |
| Setup / reset button | GP15 | 20 | push button to GND | short press: setup WiFi; hold 10 s: wipe settings (not rain history) |
| **Reserved** | GP23, GP24, GP25, GP29 | — | WiFi chip | never use on the Pico 2 W |
| Status LED | WiFi chip LED | on-board | | |
| Power in | VBUS (USB) | 40 / USB | 5 V USB supply | |

**Why these pins**
- **A dedicated setup button instead of BOOTSEL.** Reading BOOTSEL while running briefly disconnects the flash chip; a $0.10 button on GP15 is simpler and safer. This replaces the "hold BOOTSEL" factory reset from the WiFi section.
- **UART1 for LoRa** keeps UART0 (GP0/GP1) free as a debug serial port, alongside USB.
- **The Hall modules (KY-003 style: A3144 + LED + one resistor R1):** the visible resistor is the LED's, and the LED lights when the output is pulled low. The module may have no real pull-up; check with a multimeter (resistance S↔VCC, power off): ~10k = a pull-up is fitted. Either way:
  - Keep the 10k pull-up **to 3.3 V at the Pico end**, so the signal level is set where it's read.
  - If the module *does* have a pull-up to its own VCC and that's 5 V, remove it, so 5 V never reaches the GPIO.
  - **Remove R1 (or the LED) for solar:** a rain bucket can rest with its magnet next to the sensor, leaving the LED lit for days.
  - The A3144 still needs 4.5 V+. A 3.3 V-rated replacement must sample continuously (or at ≥1 kHz) for the anemometer. Low-power "duty-cycled" Hall sensors like some DRV5032 versions sample at only 5–20 Hz and would miss pulses in high wind. A **reed switch** is a zero-power option for the rain gauge, where tips are slow.
- **External 10k pull-ups and a 1 nF capacitor** on the pulse inputs: the internal pull-ups are weak (~50k), and long sensor wires outdoors pick up noise that would read as false rain tips. 10k × 1 nF = 10 µs, far faster than the fastest anemometer pulse (~50 Hz at 100+ mph).
- **The wind vane uses the analog output** so the vane wire can be long; I2C wouldn't tolerate it.
- **Load-switch parts:** a TPS22918 (or an AO3401 P-MOSFET driven by a 2N7002) on the LoRa module's 3.3 V supply. Pin 13 low = module off.
- The **ADC pins are all used outdoors** (vane, battery, solar, VSYS). Anything else analog would need an external ADC on the I2C bus.

---

## Step 1 — Foundations  ✅ *done*

- [x] Repo layout: `common/`, `outdoor/`, `base/`, `tests/`
- [x] Shared packet format with CRC-16, explicit little-endian encoding
- [x] Rain ledger: catches up lost packets, handles reboots, rolls over day/month/year, handles the clock going backwards
- [x] Host tests (run under ASan/UBSan)
- [x] Firmware skeletons for both boards build to `.uf2`: watchdog, async WiFi supervisor with step-by-step recovery

---

## Step 2 — Base station core (needs only a Pico 2 W)

Everything that doesn't depend on the parts list.

- [ ] **Forecast** from Open-Meteo (see "Forecast" below).
- [ ] **Settings in flash.** One settings record: WiFi name and password, timezone, units, station elevation, latitude/longitude, rain per tip, anemometer factor, vane north offset. Stored like the rain ledger: two copies, each with a sequence number and CRC, so a power cut while saving can never brick the settings.
- [ ] **WiFi setup with no code changes** (see "WiFi setup" below).
- [ ] **Flash storage for the rain ledger.** Two copies, each with a sequence number and CRC; boot loads the newest valid one. Save after rain changes (at most every 10 minutes) and always at midnight. Writes spread across flash blocks.
- [ ] **Clock.** Get the time from an NTP server over WiFi, with a POSIX TZ string for local midnight and DST. Show "time not set" until the first sync; re-sync every few hours.
- [ ] **Web server.** lwIP's built-in httpd: `/` serves a fixed page compiled into the firmware, and `/data.json` returns current readings. No allocation per request.
- [ ] **iPad page.** Plain HTML/CSS and ES5 JavaScript (iOS 10 Safari), fetching data with XHR every 10 seconds. Large readable numbers, a daily rain bar chart, and a "last update N s ago" line that turns red when data is stale.
- [ ] **Health line.** Uptime, reboot count and reason, WiFi reconnects, packets received and rejected, last packet time.
- [ ] Host tests for the flash format: a save that cuts off halfway must still load the previous good copy.

### Local name: `http://weather.local`

The base station announces itself as **`weather.local`** using mDNS (Bonjour), which is how printers and Apple TVs get found. (`local.weather` wouldn't work: `.local` has to be at the end.)

- **lwIP's built-in mDNS responder** (`LWIP_MDNS_RESPONDER`, part of the SDK's lwIP) also advertises an `_http._tcp` service, so it shows up in Bonjour browsers.
- **Works natively** on the iPad 4 (iOS 10), iPhones, Macs, Windows 10+ and Linux. Android support is patchy on older phones; use the IP address there.
- **DHCP hostname is set to `weather` too**, so many routers also let you reach it as `weather`, `weather.lan` or `weather.home`, and it's easy to spot in the router's device list.
- **The hostname is a setting on `/config`** (default `weather`; letters, digits and `-` only), in case of a clash or a second station.
- **Re-announced** every time WiFi reconnects or the IP changes, so the name keeps working after a router reboot.
- **Fallback:** the setup page shows the IP address after saving WiFi, and so does `/config`. Recommended: a DHCP reservation in the router so the IP never changes either.
- **On the iPad:** open `http://weather.local` in Safari → Share → Add to Home Screen, for a full-screen icon.

### iPad page design

Mockups: claude.ai Design canvas "Weather Station Display" (owner's private link), four 1024×768 landscape options. 1024×768 is the iPad 4's CSS viewport, so the sizes carry over directly.

| Option | Look | Notable |
|---|---|---|
| A · Dark panels | Dark cards, big outside temp, compass dial for wind | Today high/low: actual vs forecast side by side; 5-day strip |
| B · Sky band | Dark blue left band, light cards right | 5-day list with low→high range bars |
| C · Desert, open type | Warm light background, very large temp, no cards | Today's range bar (actual vs forecast vs now); vertical range bars per day |
| D · Station vs forecast | Dark, two columns: station data left, forecast right | Today's forecast card; **scrollable 10-day list** |

**Chosen: all four, selectable on the `/config` page.** A "Layout" setting (A/B/C/D, plus an optional **Auto** that uses a light layout by day and a dark one at night, switching at sunrise/sunset from Open-Meteo's `daily=sunrise,sunset`).

How it's built, so four layouts don't mean four times the bugs:
- **One shared script** does everything except drawing: fetching `/data.json` every 10 s, unit conversion, number formatting, staleness colors, forecast icon mapping. Written and tested once.
- **Each layout is only HTML + CSS** with named slots (`data-f="out_temp"`, `data-f="rain_month"`, …). The shared script fills whatever slots a layout has, so a layout can't break the data logic.
- **Small:** each layout is ~12 KB, ~3 KB gzipped. All four plus the script fit in about 20 KB of the Pico 2 W's 4 MB flash. Served pre-compressed, so the Pico does no work per request.
- `/` serves the layout picked in settings. `/?layout=c` overrides it for one visit, handy for comparing them on the iPad.
- The page checks `/data.json` for a layout change and reloads itself, so changing the setting from a phone switches the wall iPad within 10 s.
- **Build order:** one layout first, completely (data, staleness, units, iOS 10 testing), then port the other three onto the same slots.

What all four need from the firmware (added to Step 2):
- **Today's actual outdoor high and low.** The base station tracks min/max outdoor temperature since local midnight, saved to flash with the rain ledger so a reboot doesn't reset them.
- **Current condition text and icon** ("Sunny") come from the forecast's `current.weather_code`, since the station can't see clouds.
- **"Updated 2:41 PM"** = time of the last outdoor packet; turns amber when stale (no packet for 5 min) and red at 15 min.
- Option D's 10-day list → request `forecast_days=11` (today + 10), ~1.7 KB.

**iOS 10 Safari rules when turning the mockups into the real page:**
- **No flexbox `gap`** (it only arrived in Safari 14.1). Use margins on children instead, or the layout collapses on the iPad.
- ES5 JavaScript only: no `??`, `=>`, `let`/`const`, template strings or `fetch`. Use `var`, `function` and `XMLHttpRequest`.
- `-webkit-` prefixes for flex (`display: -webkit-flex`) to be safe.
- Inline SVG icons are fine; Helvetica Neue (including the thin weights) is built into iOS.
- No web fonts or external files: the whole page is one HTML file served by the Pico.
- The health line (uptime, reboots, packet counts) and the daily rain chart from earlier in this plan move to the `/config` page, keeping the main screen as in the mockup.

### Problem status line

A thin bar (about 32 px) along the bottom of every layout. **Hidden when everything is fine**; it appears only when something needs attention. It sits on top of the layout's bottom edge rather than pushing content up, so the layouts never reflow. Tapping it opens `/config` with the health details.

The firmware reports a list of active problems in `/data.json` (`"problems":[{"code":"outdoor_stale","level":2,"since":…}]`); the shared page script turns codes into text. The worst problem is shown; if there are more, "+2 more". Amber = degraded, red = data is missing or wrong.

| Problem | Level | Shown as |
|---|---|---|
| iPad can't reach the base station (detected by the page itself: 3 failed fetches) | red | "Can't reach weather station: last update 2:41 PM" |
| No outdoor packet for 5 min / 15 min | amber / red | "No data from outdoor station for 12 min" |
| Outdoor sensor not reporting (valid bit cleared for 3 packets in a row) | amber | "Outdoor humidity/pressure sensor not responding" (or wind / wind direction) |
| Outdoor battery low / critical | amber / red | "Outdoor battery low (3.1 V)" |
| Outdoor station rebooted by its watchdog in the last 24 h | amber | "Outdoor station restarted itself 2× today" |
| Base station rebooted by its watchdog in the last 24 h | amber | "Base station restarted itself today" |
| Indoor sensor not responding | amber | "Indoor sensor not responding" |
| Clock not set (no NTP yet) | amber | "Clock not set: rain totals waiting" (rain is still counted; it's credited to a day once the clock is set) |
| Forecast older than 3 h | amber | "Forecast not updated since 9:10 AM" |
| Flash save failed | red | "Can't save rain history" |
| Weak radio signal (average RSSI below threshold) | amber | "Weak signal from outdoor station" |

Rules:
- **Problems clear on their own** when the condition goes away; nothing needs acknowledging.
- **A delay before showing** (e.g. 2 missed packets, not 1) so one-off blips don't flash the bar.
- The page's own "can't reach" check runs even when the base station is completely down. Otherwise the iPad would keep showing frozen numbers that look current.

### Forecast (Open-Meteo)

The base station fetches a local forecast from **[Open-Meteo](https://open-meteo.com)**: free for non-commercial use, no API key, open-source server (can be self-hosted later if the public one ever goes away).

- **Request:** `http://api.open-meteo.com/v1/forecast?latitude=..&longitude=..&current=weather_code&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,precipitation_sum,wind_speed_10m_max&forecast_days=5&timezone=auto`. Tested 2026-11-12: about 900 bytes for 5 days.
- **Plain HTTP, not HTTPS:** the data is public and read-only. Skipping TLS saves ~40 KB of RAM and means there's no certificate store to expire in five years. If Open-Meteo ever drops HTTP, add mbedTLS (already in the SDK) then.
- **Schedule:** every 30 minutes, with a randomized offset. On failure, retry after 5, 10, then 20 minutes, and never more often than that.
- **Isolated from the core:**
  - Every step (DNS, connect, read) has a short time limit.
  - The response is read into a fixed 4 KB buffer; anything larger is thrown away, not reallocated.
  - Only the fields we need are parsed, with range checks.
  - A forecast failure can never block LoRa, rain or the web page. It shows "Forecast updated 2 h ago" and turns grey when stale (more than 3 h).
- **Not saved to flash:** after a reboot the page shows "Forecast loading…" until the first fetch.
- **On the page:** today's high/low and a condition icon, plus a 5-day strip (icon, high/low, chance of rain, expected rain). Converted to your units like everything else; WMO weather codes map to a small set of icons built into the firmware.
- **Config page:** latitude and longitude (decimal). A "fill from forecast" button can pre-fill **elevation** from Open-Meteo's answer, and suggest the timezone it reports.

### Units

Set on the `/config` page, one choice per measurement, saved in flash with the other settings:

| Measurement | Choices | Default |
|---|---|---|
| Temperature | °F, °C | °F |
| Pressure | inHg, hPa (mbar), mmHg | inHg |
| Wind speed | mph, km/h, m/s, knots | mph |
| Rain | inches, mm | inches |

The config page also has **US** and **Metric** buttons that set all four at once.

How it works: the firmware stores and sends everything in fixed metric units (°C×100, Pa, m/s×100, rain tips plus mm per tip). `/data.json` includes the chosen units, and the iPad page converts for display. The firmware has no unit math to get wrong, and the rain history is unaffected when units change.

### WiFi setup

The WiFi name and password are **never in the source code**. They're set from a web page and stored in flash.

How the base station picks a network at boot:
1. **Dev build override:** if the firmware was built with a dev network, it uses that and ignores flash:
   `cmake -S base -B build/base -DWX_DEV_WIFI_SSID=MyNet -DWX_DEV_WIFI_PASSWORD=secret`
   The `WX_DEV_WIFI_SSID` / `WX_DEV_WIFI_PASSWORD` environment variables also work. Values only end up in the gitignored `build/` folder, never in the repo.
2. **Saved settings:** if flash has a WiFi config, it joins that network.
3. **Nothing saved (first boot):** it starts **setup mode**:
   - The Pico runs its own WiFi network, e.g. `WeatherStation-1A2B` (last 4 hex digits of its MAC address).
   - It answers every web address with its own setup page, so iPhones and iPads pop up the page automatically when you join, like hotel WiFi.
   - The page lists nearby networks (from a scan), you pick one and enter the password, and the Pico saves it to flash and reboots into normal mode.

Recovery so it never needs a re-flash:
- **The saved network fails for 10 minutes** (router password changed, new router): the setup network comes back up *alongside* continued retries on the saved one. The Pico's WiFi chip can join a network and host one at the same time. Once you fix the settings, or the old network returns, setup mode turns off.
- **`/config` page in normal mode:** change WiFi, timezone, units, calibration. The password is never sent back to the page; leave the field blank to keep it.
- **Factory reset:** wipe saved settings from the config page, or by holding the setup button (GP15) for 10 s. A short press opens the setup network on demand.

Open questions: should the setup network have a password (printed on a label) or stay open? It only exists while the station is unconfigured or can't reach its network. And should `/config` on the home network ask for a PIN?

**Done when:** the iPad shows a live page from the Pico 2 W with fake readings, rain totals survive unplugging the base station, the board recovers on its own after a router reboot, and a freshly flashed board can be put on WiFi from a phone with no computer.

---

## Step 3 — Indoor sensors

- [ ] SHT45 + BMP390 drivers with a time limit on every I2C call and bus recovery (clock out a stuck bus, then re-initialize the sensor).
- [ ] Driver logic tested on the host against a fake I2C layer.
- [ ] Indoor readings shown on the page.

**Done when:** pulling a sensor's wires and plugging them back in leads to recovery within one read cycle, without a reboot.

---

## Step 4 — LoRa link

- [ ] UART driver for the LLCC68 module: configure through M0/M1 (address, channel, air rate, power, RSSI byte on). Read back the settings on every boot and rewrite them if they've drifted.
- [ ] Wait for the AUX pin with a time limit before every send and config step; never block forever.
- [ ] Outdoor: send the 35-byte packet every 30 s. Base: put received bytes back into whole packets (magic byte + fixed length + CRC, plus a gap timeout between bytes), decode, feed the ledger.
- [ ] Radio health check: no packet for N minutes → re-enter config mode and re-apply settings → power-cycle the module through its MOSFET → reboot as a last resort.
- [ ] Choose the air data rate for the distance. (LLCC68 tops out below SX1262's longest-range settings; plenty for a yard.)
- [ ] Range test at the real mounting spot; record RSSI and SNR on the page.

**Done when:** a week on the bench with no lost-packet streaks longer than a few minutes, and the radio recovers from a forced lock-up.

---

## Step 5 — Outdoor sensors

- [ ] **Rain:** gauge pin interrupt with debounce (done in skeleton); confirm the mm per tip (SEN-15901: 0.2794 mm / 0.011").
- [ ] **Wind speed:** count anemometer pulses per second (SEN-15901: 1 Hz = 2.4 km/h). Average over the send interval, gust = highest 3-second average.
- [ ] **Wind direction:** read the vane on the ADC, look up the angle from a resistor table, vector-average over the interval.
- [ ] **Temp/humidity/pressure:** same drivers as Step 3; run the SHT45 heater after long periods of high humidity.
- [ ] **Battery voltage** on the ADC (if solar).
- [ ] Valid bits set per sensor; a dead sensor never blocks the others.

**Done when:** every value on the page matches a reference (another thermometer, a hand-spun anemometer, a measured cup of water through the gauge).

---

## Step 6 — Power, enclosure, protection

- [ ] **Measure the outdoor unit's real current draw** on the bench (USB power meter) before ordering solar parts; size from the measurement, not a guess.
- [ ] Size the panel and battery for ~7 dark days of winter. Prefer LiFePO4 (safe, long cycle life, tolerates heat in an enclosure; Li-ion cells must not be charged below 0 °C).
- [ ] Power budget: the Hall sensors run all the time. The A3144 draws ~4 mA *each*, constantly, plus its LED when lit (remove R1). Micropower Hall sensors sample too slowly for the anemometer (see Step 0b), so budget ~4 mA for that one. A reed switch on the rain gauge costs nothing.
- [ ] Low-power design: put the LoRa module in sleep between sends, run the BME280 in forced mode, slow the Pico's clock or light-sleep between samples.
- [ ] Brownout behavior: clean reset at low voltage, no flapping.
- [ ] Surge/static protection on sensor wires, cable glands, vent, conformal coating on the board.

**Done when:** the outdoor unit runs on its real power source through a simulated week of no sun (or a power cycle test, if wired).

### Step 6b — 3D-printed housings

- [ ] **Material:** PETG at minimum, ASA preferred for anything in full sun. PLA softens and warps outdoors in summer.
- [ ] **Radiation shield** for the BME280: stacked louvered plates, white, open airflow, rain can't reach the sensor.
- [ ] **Anemometer:** cup rotor on a sealed bearing, magnet passing the Hall sensor (1 or 2 pulses per turn). Needs a calibration factor (pulses/s → mph), checked against a handheld meter or a car on a calm day.
- [ ] **Wind vane:** diametric magnet on the shaft end, about 1–2 mm above the AS5600 chip, centered. A "north" offset set in config at install time.
- [ ] **Rain gauge:** tipping bucket, magnet on the bucket passing the Hall sensor. Calibrate the rain per tip by pouring a measured volume slowly through the funnel; store it in config. A screen over the funnel keeps debris out.
- [ ] **Electronics box:** sealed, with a Gore-style vent or a drip-loop vent hole to stop condensation, cable glands, the antenna mounted outside or through a sealed bulkhead.

---

## Step 7 — Soak and fault test  *(the "never touch it" gate)*

Run both units on the bench for **at least 2 weeks**, while deliberately:
- [ ] rebooting the router and turning WiFi off for an hour
- [ ] cutting base station power mid-save, repeatedly
- [ ] unplugging and re-plugging each sensor
- [ ] switching the outdoor unit off and on
- [ ] blocking the radio (metal box) for an hour
- [ ] watching the health line for memory or reconnect counters creeping up

**Done when:** no hand resets, no unexplained watchdog reboots, rain totals exactly right.

---

## Step 8 — Install and calibrate

- [ ] Mount: the anemometer about 10 ft (3 m) above obstacles if possible, the rain gauge level and clear of overhangs, the shield out of direct sun.
- [ ] Set the station elevation for sea-level pressure.
- [ ] Wall-mount the iPad, Guided Access or kiosk mode, auto-lock off, always plugged in.
- [ ] Write the final settings into the repo.

---

## Later / nice to have

- Pressure trend arrow (3-hour change) — the history is cheap to keep.
- Hourly rain rate.
- Look at the sprinkler timer's code: it probably has the same lock-up causes and the same fixes.
