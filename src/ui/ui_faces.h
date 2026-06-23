// Watchface registry. Each face implements create(parent) + tick() and is
// listed in ui_faces.cpp::FACES. The Settings dropdown drives selection;
// ui::rebuildFace() destroys + recreates the current face onto the tile.
#pragma once
#include <lvgl.h>
#include <stdint.h>

namespace faces {
  struct Face {
    const char* name;
    void (*create)(lv_obj_t*);
    void (*tick)();
  };
  uint8_t      count();
  const Face&  at(uint8_t i);
  const Face&  cur();
  uint8_t      current();
  void         setCurrent(uint8_t i);   // persists + rebuilds

  void init();   // load saved selection
}
