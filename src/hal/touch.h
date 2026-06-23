// Touch -> LVGL input device. Also exposes "did the user touch recently" for power mgmt.
#pragma once
#include <stdint.h>

namespace touch {
  void init();                 // register LVGL pointer indev (reads disp::readTouches)
  uint32_t lastActivityMs();   // millis() of last finger-down, for idle timeout
  void ignoreUntil(uint32_t millis_deadline);  // suppress indev events until this time

  // Snapshot of the last multi-touch poll cached by the indev callback.
  // xs/ys must be sized to at least 2. Returns the number of fingers down.
  uint8_t readMulti(uint16_t* xs, uint16_t* ys);
}
