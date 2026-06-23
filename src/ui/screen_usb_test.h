#pragma once
#include <lvgl.h>

// "USB install" screen: looks for /firmware.bin on the SD card and flashes it
// via Update.h. Host copies the bin over MSC, then taps INSTALL here.
namespace screen_usb_test {
  void create(lv_obj_t* parent);
  void tick();
}
