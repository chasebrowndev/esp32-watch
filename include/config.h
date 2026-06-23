// Compile-time feature toggles + tunables.
#pragma once

// ---- Panel / touch silicon selection ----
// Set after running the diag build. Comment/uncomment to match hardware.
//   PANEL_ILI9341 + TOUCH_FT6336  -> lcdwiki ES3C28P docs
//   PANEL_ST7789  + TOUCH_CST816  -> community ESPHome variant
#define PANEL_ILI9341
// #define PANEL_ST7789
#define TOUCH_FT6336
// #define TOUCH_CST816

// ---- Feature flags ----
#define FEAT_WIFI      1
#define FEAT_BLE_HID   1
#define FEAT_BLE_SPAM  1
#define FEAT_USB_HID   1   // USB-C ducky: requires ARDUINO_USB_MODE=0 in platformio.ini
#define FEAT_USB_MSC   1   // USB-C mass storage (present SD card to host)

// ---- Power management ----
#define IDLE_DIM_MS    8000    // dim backlight after this idle time
#define IDLE_SLEEP_MS  20000   // screen off / light-sleep after this idle time
// Deep sleep removed: PIN_TP_INT doesn't fire on this hardware, so an
// ext0 wake would never trigger and the board would brick until reset.
#define BL_BRIGHT      255
#define BL_DIM         40

// ---- Battery (LiPo via on-board divider on PIN_BAT_ADC) ----
// Most ESP32-S3 dev boards use a 2:1 resistor divider so 4.2V battery reads
// ~2.1V at the pin. analogReadMilliVolts() returns millivolts at the pin;
// multiply by BAT_DIVIDER to get battery voltage.
#define BAT_DIVIDER    2
#define BAT_MV_FULL    4150
#define BAT_MV_EMPTY   3300
#define BAT_MV_LOW     3500    // batteryLow() threshold

// ---- Timekeeping ----
#define SNTP_SERVER_1  "pool.ntp.org"
#define SNTP_SERVER_2  "time.nist.gov"
// POSIX TZ string — America/New_York.
#define TZ_STRING      "EST5EDT,M3.2.0,M11.1.0"

