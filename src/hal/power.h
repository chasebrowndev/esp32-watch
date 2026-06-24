// Power / idle management: dim -> display-off after inactivity, wake on touch.
#pragma once
#include <stdbool.h>
#include <stdint.h>

namespace power {
  void init();
  void tick();         // call every loop; handles dim/sleep state machine
  bool asleep();       // true when display is off
  void wake();         // force wake (e.g. on notification)
  void noteActivity(); // reset idle timer (called on touch)

  // Power the chip off until EN/reset is pressed. No wake sources are
  // configured, so touch, USB plug-in, and timers will not bring it back.
  // Intended for overnight charging.
  void powerOff();

  // User-tunable bright-level (0..255). Persists in NVS; takes effect immediately.
  uint8_t brightness();
  void    setBrightness(uint8_t v);

  // Battery — sampled in tick() every few seconds and smoothed (EMA).
  uint16_t batteryMv();   // battery voltage in mV (after divider correction)
  uint8_t  batteryPct();  // 0..100, piecewise LiPo curve
  bool     batteryLow();  // true under BAT_MV_LOW
  bool     charging();    // USB attached (host is supplying power)
}
