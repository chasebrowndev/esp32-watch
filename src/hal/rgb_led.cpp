#include "rgb_led.h"
#include "power.h"
#include "config.h"
#include "board_pins.h"
#if FEAT_BLE_HID
#include "../features/ble_hid.h"
#endif
#if FEAT_BLE_SPAM
#include "../features/ble_spam.h"
#endif
#include <Arduino.h>
#include <esp32-hal-rgb-led.h>

namespace {
  uint8_t  s_r = 0, s_g = 0, s_b = 0;
  uint32_t s_lastTick = 0;

  void write(uint8_t r, uint8_t g, uint8_t b) {
    if (r == s_r && g == s_g && b == s_b) return;
    s_r = r; s_g = g; s_b = b;
    neopixelWrite(PIN_RGB_LED, r, g, b);
  }
}

namespace rgb_led {

void init() {
  pinMode(PIN_RGB_LED, OUTPUT);
  write(0, 0, 0);
}

void set(uint8_t r, uint8_t g, uint8_t b) { write(r, g, b); }
void off()                                { write(0, 0, 0); }

void tick() {
  uint32_t now = millis();
  if (now - s_lastTick < 100) return;
  s_lastTick = now;

  // While the panel is awake the LED would just be light pollution; keep it
  // dark unless the screen is off.
  if (!power::asleep()) { write(0, 0, 0); return; }

  // During sleep, only light up for things the user actually needs to know
  // about. The "connected" steady green and "advertising" breathing blue
  // used to run here continuously — that's a constant WS2812 draw for no
  // useful signal (the user is, by definition, not looking at the watch).
  bool blinkSlow = ((now / 1000) & 1);
  bool blinkFast = ((now / 200)  & 1);

  if (power::batteryLow()) {
    write(blinkSlow ? 12 : 0, 0, 0);          // dim red, 1 Hz
    return;
  }
#if FEAT_BLE_SPAM
  if (ble_spam::active()) {
    write(blinkFast ? 10 : 0, 0, blinkFast ? 10 : 0);   // magenta, 5 Hz
    return;
  }
#endif
  write(0, 0, 0);
}

} // namespace rgb_led
