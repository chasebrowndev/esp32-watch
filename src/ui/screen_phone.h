// BLE HID phone remote. Media + volume controls aimed at controlling a
// paired phone's music/playback. No D-pad — see screen_remote.h for the
// TV-style layout.
#pragma once
#include <lvgl.h>

namespace screen_phone {
  void create(lv_obj_t* parent);
  void tick();
}
