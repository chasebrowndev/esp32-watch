# Smartwatch Firmware

Custom firmware for a Hosyond ESP32-S3 2.8" (240×320) smartwatch/dev board, built with PlatformIO + Arduino framework. The watch runs LVGL 9 for the UI, LovyanGFX for display driving, NimBLE for Bluetooth, and TinyUSB for USB HID/MSC. It's more of a pocket hacking tool that happens to tell time than a traditional smartwatch.

## Hardware

- **MCU**: ESP32-S3 rev 0, 16 MB flash, no PSRAM
- **Panel**: ILI9341, 240×320, inverted (INVON required)
- **Touch**: FT6336G capacitive touch @ I2C 0x38
- **USB**: Native USB-C via TinyUSB (not the hardware JTAG bridge)
- **Storage**: SD card via SDMMC
- **LED**: Onboard WS2812 RGB status pixel

## Project Structure

```
smartwatch/
  platformio.ini        # build config (esp32-s3, arduino, LVGL 9, NimBLE)
  partitions.csv        # dual OTA + LittleFS (label="spiffs")
  include/
    board_pins.h        # all GPIO defines
    config.h            # compile-time feature flags + power/battery tunables
    lv_conf.h           # LVGL configuration
  data/
    wifi.json.example   # WiFi credentials template
  src/
    main.cpp            # setup/loop entry point
    hal/                # display, touch, power, RGB LED
    net/                # WiFi manager, SNTP time sync, captive portal
    ui/                 # screen manager, watch faces, all app screens
    features/           # BLE HID, BLE spam, USB HID, USB MSC, SD card, SD config
```

## UI Layout

The top level is a vertical LVGL tileview centered on the watchface at `(0,0)`:
- **Swipe up** → app drawer at `(0,1)`
- **Swipe down** → back to watchface

Tapping a drawer icon opens a full-screen app overlay with a back button in a header bar.

### Watch Faces (swipe left/right to cycle)

| Face | Description |
|------|-------------|
| Digital | Large HH:MM with date and battery |
| Analog | Classic clock hands |

### App Drawer

Apps are organized into folders:

**Bluetooth**
- **Remote** (subfolder)
  - **TV** — D-pad + media + volume BLE HID remote, aimed at smart TVs
  - **Trackpad** — touch surface maps to BLE HID mouse, tap/2-finger tap/scroll
  - **Phone** — media transport + volume + home/back for paired phones
- **Spam** — BLE advertisement spammer (Apple / Android / Windows / All)
- **Pair** — BLE HID pairing assistant

**WiFi**
- **Saved** — Connect to/manage saved WiFi networks (credentials from `data/wifi.json` or NVS)
- **AP** — Launch captive-portal access point

**USB**
- **Ducky** — USB HID keyboard that runs BadUSB-style keystroke injection scripts from SD card
- **SD Card** — USB Mass Storage: presents the SD card to the host as a flash drive

**Settings** — Theme, face selection, system info

## Feature Flags (`include/config.h`)

```c
FEAT_WIFI      // WiFi + SNTP + ArduinoOTA + captive portal + deauth
FEAT_BLE_HID   // NimBLE HID keyboard/mouse (remote + trackpad)
FEAT_BLE_SPAM  // BLE advertisement spammer
FEAT_USB_HID   // TinyUSB HID keyboard (USB ducky)
FEAT_USB_MSC   // TinyUSB mass storage (SD card to host)
```

## Networking

- **WiFi manager**: non-blocking connect with reconnect/backoff; credentials from `data/wifi.json` or NVS
- **SNTP**: time sync on connect via `pool.ntp.org` / `time.nist.gov`; TZ set via POSIX string in `config.h`
- **ArduinoOTA**: starts automatically once WiFi connects (`smartwatch.local`)
- **Captive portal**: ESPAsyncWebServer-based AP for config
- **Raw WiFi**: deauth frames via `wifi_raw`

## Power Management

Three-state backlight machine in `power.cpp`:
- **Bright** (BL=255) — active use
- **Dim** (BL=40) — after 8 s idle
- **Asleep** (BL=0) — after 20 s idle; any touch wakes back to Bright

Hardware deep sleep is disabled: the touch INT pin (GPIO17) does not reliably fire on this board, so ext0 wake is not usable.

## Build Environments

| Env | Purpose |
|-----|---------|
| `diag` | Lightweight silicon probe — I2C scan, display test, no UI libs |
| `watch` | Full firmware, USB flash (hold BOOT + tap RST before upload) |
| `watch-ota` | Full firmware, WiFi OTA flash |

Flash command: `~/.local/bin/pio run -e watch -t upload`

## Setup

1. Copy `data/wifi.json.example` → `data/wifi.json` and fill in SSID/password
2. Set `TZ_STRING` in `config.h` to your POSIX timezone
3. Upload filesystem: `pio run -e watch -t uploadfs`
4. Flash firmware: hold BOOT, tap RST, release BOOT, then `pio run -e watch -t upload`
