# Smartwatch Firmware — Handoff (2026-06-12)

## Where we are

**Task #4 (Touch input device) — DONE.** Touch reports real coordinates, LVGL indev polls at ~51 Hz, taps brighten from Dim and wake from Asleep. Task #7 (power management) is largely already in place via the existing Bright/Dim/Asleep state machine in `power.cpp`; what remains is the hardware deep-sleep / wake-on-INT path.

Two fixes landed to close out #4:
1. `lgfx_board.h`: `c.pin_int = -1` (bypass FT6336G INT gating — see prior notes).
2. `display.cpp` after `lv_init()`: `lv_tick_set_cb([](){ return millis(); })`. LVGL 9 removed `LV_TICK_CUSTOM`; without it, no timers fired (read_cb was never polled). This was the root cause for touch appearing dead in Bright/Dim.

Everything else is done: display HAL + LVGL (Task #3), watch faces + swipe (Task #5), WiFi/SNTP (Task #6), BLE HID (Task #8), BLE spam (Task #9), Spotify (Task #10).

## What was just changed (NOT yet flashed)

Two edits were made, build succeeds, flash was interrupted:

### 1. `src/hal/lgfx_board.h` — `pin_int = -1`

**Root cause of touch returning 0 samples:** `Touch_FT5x06::getTouchRaw()` gates all reads on the INT GPIO. When `pin_int >= 0`, if INT (GPIO17) is HIGH (pull-up, not being driven LOW by FT6336G), the driver sets `_flg_released = true` and returns 0 immediately — no I2C read happens at all. Setting `pin_int = -1` bypasses the INT gate and polls the touch-count register directly.

Changed:
```cpp
c.pin_int = -1;   // was PIN_TP_INT (GPIO17); INT gating suppressed all reads
```

### 2. `src/hal/display.cpp` — raw I2C probe in `init()`

Added `probe_touch()` call before `gfx.init()`. It resets the FT6336G via RST pin, then directly reads register 0xA3 (chip ID), 0xA8 (vendor ID), and 0x02 (touch point count) over Wire. Output line:
```
[tp] chipID(0xA3)=0xXX[1] vendID(0xA8)=0xXX[1] pts(0x02)=0x00[1]
```
If all three `[1]` = I2C is alive. If chip/vend IDs are wrong, wrong device address. If `[0]` = no response.

Also added `#include <Wire.h>` to display.cpp.

## Next step on laptop

1. `cd ~/smartwatch && ~/.local/bin/pio run -e watch -t upload`
2. Capture serial (see method below)
3. Look for `[tp]` line — if all three `[1]`, touch I2C is alive
4. Tap the screen — look for `[t] x,y` lines in serial output
5. If touch works → mark Task #4 done, move to Task #7 (power management)

## Serial capture method (working on this board)

```bash
# Flash first, then:
python3 ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 --port /dev/ttyACM0 --after hard_reset run

python3 - <<'EOF'
import serial, time
s = serial.Serial('/dev/ttyACM0', 115200, timeout=0.1)
s.dtr = True; s.rts = False
end = time.time() + 20
while time.time() < end:
    line = s.readline()
    if line: print(line.decode(errors='replace'), end='')
s.close()
EOF
```

## If touch still returns 0 after this flash

The diag scan showed an unexpected device at I2C 0x18 alongside FT6336G at 0x38. If `probe_touch()` shows `[0]` (no I2C response), try:
- Check if FT6336G needs address 0x38 confirmed (it should be, diag confirmed it)
- The 0x18 device is unknown — may be a second sensor or artifact
- Try increasing RST pulse delay in `probe_touch()` from 5ms to 20ms

## Key facts about this hardware

- ESP32-S3 rev 0, 16MB flash, **NO PSRAM**
- Panel: ILI9341 (`c.invert = true` — this unit boots inverted, INVON corrects it)
- Touch: FT6336G @ I2C 0x38 (FT5x06-protocol compatible)
- Touch pins: SDA=16, SCL=15, RST=18, INT=17
- Flash via: `/dev/ttyACM0` (native USB-Serial-JTAG, 303a:1001)
- PlatformIO at: `~/.local/bin/pio`
- Build env: `watch` (`pio run -e watch`)

## Known quirks / bugs already solved

- `Serial.flush()` back-to-back on HWCDC deadlocks — always space prints with delays
- `lv_display_set_buffers()` hangs on this build — use external `lv_draw_buf_t` structs + `lv_display_set_draw_buffers()` (already done in display.cpp)
- LittleFS partition must be labeled `"spiffs"` in partitions.csv even with spiffs subtype
- `c.invert = true` required in lgfx_board.h for this ILI9341 unit

## Project layout

```
~/smartwatch/
  platformio.ini          # esp32-s3, arduino, LVGL 9, NimBLE, ArduinoJson, TJpg_Decoder
  partitions.csv          # dual OTA + spiffs (label="spiffs")
  include/
    board_pins.h          # all GPIO defines
    config.h              # FEAT_WIFI, FEAT_BLE_HID, FEAT_BLE_SPAM, FEAT_SPOTIFY toggles
    lv_conf.h             # LVGL config
  src/
    main.cpp
    hal/display.cpp/.h    # LovyanGFX + LVGL flush, readTouch, backlight
    hal/touch.cpp/.h      # LVGL indev, DEBUG [t] coord logging still active
    hal/power.cpp/.h      # idle timer skeleton (Task #7 pending)
    hal/lgfx_board.h      # LGFX_Hosyond device class
    net/wifi_mgr.cpp/.h
    net/sntp.cpp/.h
    ui/ui.cpp/.h          # screen manager, swipe router
    ui/faces/             # face_digital, face_analog, face_modules
    ui/screen_spotify.cpp
    ui/screen_ble.cpp
    features/ble_hid.cpp/.h
    features/ble_spam.cpp/.h
    features/spotify.cpp/.h
```

## Still needs user action (non-blocking)

- Spotify: client_id/secret + refresh token → `/data/spotify_tokens.json`, then `pio run -e watch -t uploadfs`
- WiFi: SSID/pass → NVS or `/data/wifi.json`
