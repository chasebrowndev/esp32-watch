// Settings: brightness slider, SD card status, about info, reboot.
#pragma once
#include <lvgl.h>

namespace screen_settings {
  void create(lv_obj_t* parent);
  void tick();
}
