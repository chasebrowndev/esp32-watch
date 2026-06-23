// Onboard WS2812 status indicator on PIN_RGB_LED (GPIO42).
// Policy is implicit: the LED reflects the most-important transient state
// (low battery > spam active > paired > advertising). Idle = off.
#pragma once
#include <stdint.h>

namespace rgb_led {
  void init();
  void tick();   // run every loop; throttles itself
  // Direct override for manual testing / Settings tile.
  void set(uint8_t r, uint8_t g, uint8_t b);
  void off();
}
