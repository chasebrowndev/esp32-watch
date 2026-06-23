// BLE HID trackpad. Whole screen = touch surface. Single-finger drag moves
// the cursor; tap = left click; two-finger tap = right click; two-finger
// drag = scroll. Requires the watch to be paired as the HID device.
#pragma once
#include <lvgl.h>

namespace screen_trackpad {
  void create(lv_obj_t* parent);
  void tick();
}
