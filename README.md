# pico_weather

A two-part weather station built for long unattended uptime.

- **outdoor/**: Raspberry Pi Pico 2. Reads temp/humidity/pressure, wind and rain, then sends one LoRa packet every 30 s. Keeps no saved state.
- **base/**: Raspberry Pi Pico 2 W, indoors. Receives packets, reads its own indoor sensor, keeps rain history in flash and serves a lightweight page for an iPad 4.
- **common/**: Plain C++ shared by both boards: the packet format and the rain ledger.
- **tests/**: Host-side tests for `common/`, run on the PC.

C++17 on the official Pico SDK (2.2.0, found at `~/src/pico-sdk` or `$PICO_SDK_PATH`).

## Stability rules

- Hardware watchdog on both boards.
- No heap allocation after startup; lwIP uses its own fixed pools.
- No blocking calls in the main loop; every sensor and radio call has a time limit.
- Rain is sent as a running tip count plus a per-boot id, so lost packets and reboots on either side never lose rain.

## Build

Run the host tests:

```bash
cmake -S tests -B build/tests -G Ninja && cmake --build build/tests && ./build/tests/wx_tests
```

Build the outdoor firmware:

```bash
cmake -S outdoor -B build/outdoor -G Ninja && cmake --build build/outdoor
```

Build the base station firmware. WiFi is set from the station's own setup page and saved in flash, so there's nothing to edit before building:

```bash
cmake -S base -B build/base -G Ninja && cmake --build build/base
```

For development, bake in a network so the board joins it on its own (overrides saved settings; never install this build):

```bash
cmake -S base -B build/base-dev -G Ninja -DWX_DEV_WIFI_SSID=MyNet -DWX_DEV_WIFI_PASSWORD=secret && cmake --build build/base-dev
```

`WX_DEV_WIFI_SSID` / `WX_DEV_WIFI_PASSWORD` environment variables work too. The values stay in the gitignored `build/` folder.

To flash, hold BOOTSEL while plugging in the board, then copy the `.uf2` from `build/<name>/` onto it.
