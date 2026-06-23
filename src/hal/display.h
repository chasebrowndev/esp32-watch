// Display + LVGL bring-up. Owns the LovyanGFX device and the LVGL draw buffers.
#pragma once
#include <stdint.h>

namespace disp {
  // Init LovyanGFX panel, backlight, and LVGL (display + buffers). Call once in setup().
  void init();
  // Set backlight 0..255 (PWM via LovyanGFX Light_PWM). Used by power mgmt for fades.
  void setBacklight(uint8_t level);
  uint8_t backlight();
  // Hard panel sleep/wake (display off + backlight off). LVGL keeps running.
  void sleepPanel();
  void wakePanel();
  // Read one touch point from the panel's touch controller (LovyanGFX).
  // Returns true if a finger is down; writes screen coords to x,y.
  bool readTouch(int16_t& x, int16_t& y);

  // Multi-touch read for the trackpad screen. Fills xs/ys with up to cap
  // touch points and returns how many fingers are down (0..2 typical for
  // FT6336G). Callers must size xs/ys to at least cap.
  uint8_t readTouches(uint16_t* xs, uint16_t* ys, uint8_t cap);
}
