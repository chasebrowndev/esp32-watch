// BLE advertising spam — flipper-style pairing-popup flood. Rapidly rotates
// manufacturer payloads (Apple / Android / Windows Swift Pair) and the random
// MAC so nearby phones throw pairing prompts. Android combines Samsung Buds/
// Watch and Google Fast Pair under one mode (they overlap on the same devices).
// Shares the radio with ble_hid: while spam is active it takes over advertising
// and the HID device cannot be paired.
#pragma once
#include <stdbool.h>
#include <stdint.h>

namespace ble_spam {
  enum Mode : uint8_t { APPLE = 0, ANDROID, WINDOWS, ALL, MODE_COUNT };

  void begin();                 // assumes NimBLEDevice already init'd (by ble_hid)
  void start(Mode m = ALL);     // pauses HID advertising, begins the flood
  void stop();                  // stops flood, restores HID advertising
  void tick();                  // rotate payload + MAC on interval (call in loop)
  bool active();
  Mode mode();
  uint32_t txCount();   // monotonic count of beacons transmitted since last start()
}
