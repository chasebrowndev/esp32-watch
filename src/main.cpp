// Smartwatch firmware entry. Brings up display -> touch -> power -> UI, then
// runs the LVGL + power loop. WiFi/SNTP/BLE layer in behind feature flags
// (config.h) as each is verified on hardware.
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "hal/display.h"
#include "hal/touch.h"
#include "hal/power.h"
#include "hal/rgb_led.h"
#include "ui/ui.h"
#if FEAT_WIFI
#include "net/wifi_mgr.h"
#include "net/sntp.h"
#include <ArduinoOTA.h>
static bool s_ota_started = false;
// Wifi is only needed until NTP syncs; after that we power the radio off to
// save battery. Cleared on first sync (or up-front if no saved networks).
// Re-enabling wifi via the settings screen is unaffected — this gate only
// governs the automatic boot-time sync window.
static bool s_wifi_needed = true;
#endif
#if FEAT_BLE_HID
#include "features/ble_hid.h"
#endif
#if FEAT_BLE_SPAM
#include "features/ble_spam.h"
#endif
#include "features/sdcard.h"
#include "features/usb_hid.h"
#include "features/usb_msc.h"

void setup() {
  // Bring up the display BEFORE the 2 s CDC-attach wait so the user sees
  // boot progress instead of a blank panel for ~2.5 s. Serial.begin()
  // triggers USB stack init internally; deferring it lets the TinyUSB
  // HID/MSC classes (registered at global-static scope below) finish
  // their descriptor registration before USB enumeration kicks off.
  disp::init();      // LovyanGFX + LVGL display

  Serial.begin(115200);
  delay(2000);   // let host CDC attach before any init prints
  Serial.println(F("\n[boot] smartwatch v1"));
  Serial.println(F("[i] disp ok"));   Serial.flush();
  Serial.println(F("[i] touch"));  Serial.flush();
  touch::init();     // LVGL pointer indev
  Serial.println(F("[i] power"));  Serial.flush();
  power::init();     // idle/backlight state machine
  rgb_led::init();   // onboard WS2812 status indicator
  Serial.println(F("[i] ui"));     Serial.flush();
  ui::init();        // default watch face

#if FEAT_WIFI
  Serial.println(F("[i] wifi"));   Serial.flush();
  wifi::init();      // load creds, begin non-blocking connect
  Serial.println(F("[i] ntp"));    Serial.flush();
  ntp::init();
  ArduinoOTA.setHostname("smartwatch");
  ArduinoOTA.onStart([]() { Serial.println(F("[ota] start")); });
  ArduinoOTA.onEnd([]()   { Serial.println(F("[ota] done"));  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[ota] error %u\n", e);
  });
  // No saved networks means we can't ever sync; cut the radio now so we
  // don't sit in STA mode burning power until the user adds one.
  if (!wifi::savedCount()) {
    Serial.println(F("[wifi] no saved networks; radio off"));
    wifi::shutdown();
    s_wifi_needed = false;
  }
#endif

#if FEAT_BLE_HID
  Serial.println(F("[i] ble_hid")); Serial.flush();
  ble_hid::begin("smartwatch");   // NimBLE init + connectable HID advertising
#endif
#if FEAT_BLE_SPAM
  Serial.println(F("[i] ble_spam")); Serial.flush();
  ble_spam::begin();              // shares NimBLE stack with ble_hid; idle until start()
#endif
  Serial.println(F("[i] sd")); Serial.flush();
  sdcard::init();                 // best-effort SDMMC mount
  Serial.println(F("[i] usb_hid")); Serial.flush();
  usb_hid::begin();              // activate TinyUSB HID keyboard
  Serial.println(F("[i] usb_msc")); Serial.flush();
  usb_msc::begin();              // configure TinyUSB MSC (media not presented yet)

  Serial.println(F("[boot] ui up"));
}

// Firmware install over USB: handled by the USB > INSTALL screen, which reads
// /firmware.bin off the SD card (delivered via the MSC interface) and flashes
// via Update.h. No serial protocol needed.

void loop() {
  lv_timer_handler();   // LVGL render + indev
#if FEAT_WIFI
  if (s_wifi_needed) {
    wifi::tick();         // reconnect/backoff
    ntp::tick();          // (re)sync time on connect
    if (!s_ota_started && wifi::connected()) {
      ArduinoOTA.begin();
      s_ota_started = true;
      Serial.println(F("[ota] listening on smartwatch.local"));
    }
    if (s_ota_started) ArduinoOTA.handle();
    // Once the clock is real, drop the radio. ESP32 RTC keeps time across
    // the rest of the boot, so we don't need wifi up again until reboot.
    if (ntp::synced()) {
      Serial.println(F("[wifi] time synced; radio off"));
      wifi::shutdown();
      s_ota_started = false;
      s_wifi_needed = false;
    }
  }
#endif
#if FEAT_BLE_HID
  ble_hid::tick();
#endif
#if FEAT_BLE_SPAM
  ble_spam::tick();     // rotate spam payload/MAC when active
#endif
  usb_hid::tick();      // advance active ducky script one line
  usb_msc::tick();      // handle host eject / FATFS remount
  ui::tick();           // clock text refresh
  power::tick();        // idle dim/sleep, wake-on-touch
  rgb_led::tick();      // status pixel
  delay(5);
}
