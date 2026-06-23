#pragma once
#include <lvgl.h>
#include "ui.h"

namespace screen_folder {
  struct Entry { const char* sym; const char* name; ui::App app; };
  void create(lv_obj_t* parent, const char* title,
              const Entry* entries, uint8_t count);
}
