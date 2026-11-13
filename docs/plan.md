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
- [ ] **Display units:** US units by default (°F / inHg / mph / inches); confirm.
- [ ] **Rain year:** calendar year (Jan 1) vs water year (Oct 1).
- [ ] **Station elevation:** needed to correct pressure to sea level.
- [ ] **Power:** solar vs wired (see above).
- [ ] **Distance and obstacles** between the station and the house: decides antenna and LoRa settings.

**Done when:** every row above is ✅ and the pin map (Step 0b) is written.

### Step 0b — Pin map
Assign GPIO, I2C, SPI and ADC pins for both boards and put them in `outdoor/src/board.h` and `base/src/board.h`.
Rules: keep I2C sensors on their own bus away from the radio's SPI bus; the wind vane goes on an ADC pin; the anemometer and rain gauge go on interrupt-capable GPIOs with pull-ups; pins GP23/24/25/29 on the Pico 2 W are reserved for its WiFi chip.

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

- [ ] **Settings in flash.** One settings record: WiFi name and password, timezone, units, station elevation, rain per tip, anemometer factor, vane north offset. Stored like the rain ledger: two copies, each with a sequence number and CRC, so a power cut while saving can never brick the settings.
- [ ] **WiFi setup with no code changes** (see "WiFi setup" below).
- [ ] **Flash storage for the rain ledger.** Two copies, each with a sequence number and CRC; boot loads the newest valid one. Save after rain changes (at most every 10 minutes) and always at midnight. Writes spread across flash blocks.
- [ ] **Clock.** Get the time from an NTP server over WiFi, with a POSIX TZ string for local midnight and DST. Show "time not set" until the first sync; re-sync every few hours.
- [ ] **Web server.** lwIP's built-in httpd: `/` serves a fixed page compiled into the firmware, and `/data.json` returns current readings. No allocation per request.
- [ ] **iPad page.** Plain HTML/CSS and ES5 JavaScript (iOS 10 Safari), fetching data with XHR every 10 seconds. Large readable numbers, a daily rain bar chart, and a "last update N s ago" line that turns red when data is stale.
- [ ] **Health line.** Uptime, reboot count and reason, WiFi reconnects, packets received and rejected, last packet time.
- [ ] Host tests for the flash format: a save that cuts off halfway must still load the previous good copy.

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
- **Factory reset:** wipe saved settings from the config page, or by holding the BOOTSEL button for 10 s while the board is running.

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
- [ ] Power budget: the Hall sensors run all the time. The A3144 draws a few mA *each*, constantly; a DRV5032 draws about 1 µA. On solar that difference matters; another reason to swap.
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
