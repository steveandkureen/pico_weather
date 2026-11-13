# pico_weather

A two-part weather station built to run for years without being touched.

- **outdoor/**: Raspberry Pi Pico 2. Counts rain-bucket tips and anemometer pulses, reads the wind vane and a BME280, and sends one LoRa packet every 30 s. It keeps nothing in flash, so a reboot loses nothing.
- **base/**: Raspberry Pi Pico 2 W, indoors. Receives packets, reads its own BME280 and keeps rain history in flash. It also fetches the forecast from Open-Meteo and serves the iPad display at **http://weather.local**.
- **common/**: Plain C++ shared by both boards and tested on the PC: packet format, rain ledger, settings, timezone/DST, sensor and radio drivers, JSON, forecast parser, and the station model.
- **pico_drivers/**: Thin Pico SDK layers (I2C with timeouts and bus recovery, flash, LoRa UART).
- **web/**: The iPad pages. There are four layouts plus `/config`, all plain ES5 for iOS 10 Safari, gzipped into the firmware at build time.
- **tests/**: Host tests for `common/` (run with ASan/UBSan) plus a sample-data generator.
- **docs/plan.md**: The decisions, pin map and step-by-step plan.

C++17 on the official Pico SDK (2.2.0, found at `~/src/pico-sdk` or `$PICO_SDK_PATH`).

## Stability rules

- **One core, one cooperative loop.** No RTOS.
- **Watchdog on both boards**, fed only while every task keeps making progress.
- **No heap after startup.** lwIP uses its own fixed pools.
- **Nothing in the main loop blocks.** Every sensor, radio and network step has a deadline.
- **Recovery escalates:**
  - Sensors: reset the I2C bus, then re-initialize the sensor.
  - LoRa radio: re-apply its settings, then power-cycle it.
  - WiFi: rejoin, bring up the setup network, then power-cycle the WiFi chip.
- **Rain can't be lost by a dropped packet or a reboot.** The outdoor unit sends a running tip count plus a per-boot ID, and the base station saves history in two flash copies, each with a CRC.

## Build

```bash
cmake -S tests -B build/tests -G Ninja && cmake --build build/tests && ./build/tests/wx_tests
```

```bash
cmake -S outdoor -B build/outdoor -G Ninja && cmake --build build/outdoor
```

```bash
cmake -S base -B build/base -G Ninja && cmake --build build/base
```

**Development build** with your WiFi baked in. It overrides saved settings; never install it permanently:

```bash
cmake -S base -B build/base-dev -G Ninja -DWX_DEV_WIFI_SSID=MyNet -DWX_DEV_WIFI_PASSWORD=secret && cmake --build build/base-dev
```

`WX_DEV_WIFI_SSID` / `WX_DEV_WIFI_PASSWORD` environment variables work too. The values stay in the gitignored `build/` folder.

## Flash

Hold **BOOTSEL** while plugging the board into USB, then copy the `.uf2` onto the drive that appears:

- outdoor: `build/outdoor/wx_outdoor.uf2`
- base: `build/base/wx_base.uf2`

Or use picotool:

```bash
picotool load -x build/base/wx_base.uf2
```

Both boards print a log over USB serial (e.g. `screen /dev/ttyACM0`).

## First boot of the base station

1. With no WiFi saved, the base station starts an open network called **WeatherStation-XXXX**, and the on-board LED stays on.
2. Join it from a phone. The setup page should pop up automatically; if it doesn't, open **http://192.168.4.1/config**.
3. Tap **Find networks**, pick your WiFi, enter the password, and set your location. Then press **Save**.
4. The station joins your WiFi, and the setup network turns off a minute later.
5. On the iPad, open **http://weather.local** in Safari, then Share → **Add to Home Screen**.

The setup network comes back on its own if your WiFi is unreachable for 10 minutes. Press the setup button (GP15) to open it on demand, or hold it for 10 s to erase settings. Rain history is kept either way.

## Working on the web pages without a Pico

```bash
python3 tools/dev_server.py
```

Then open http://localhost:8000 (`/?layout=a` through `d`, or `/config`). Add `--problems` to see the status bar. The sample data comes from the real `Station` code (`build/tests/wx_sample`), so build the tests first.
