#include "screen_drawer.h"
#include "ui.h"
#include "ui_theme.h"

namespace {
  struct Entry { const char* sym; const char* name; ui::App app; };
  const Entry ENTRIES[] = {
    { LV_SYMBOL_BLUETOOTH, "BLUETOOTH", ui::APP_FOLDER_BT    },
    { LV_SYMBOL_USB,       "USB",       ui::APP_FOLDER_USB   },
    { LV_SYMBOL_WIFI,      "WIFI",      ui::APP_FOLDER_WIFI  },
    { LV_SYMBOL_EYE_OPEN,  "UTIL",      ui::APP_FOLDER_UTIL  },
    { LV_SYMBOL_SETTINGS,  "SETTINGS",  ui::APP_SETTINGS     },
  };
  const uint8_t COLS   = 3;
  const uint8_t TILE_W = 96,  TILE_H = 80;
  const uint8_t GAP_X  = 10,  GAP_Y  = 10;

  void cb_open(lv_event_t* e) {
    ui::App a = (ui::App)(uintptr_t)lv_event_get_user_data(e);
    ui::openApp(a);
  }
}

namespace screen_drawer {

void create(lv_obj_t* parent) {
  ui_fill_parent(parent);

  lv_obj_t* title = ui_label(parent, "APPS", &lv_font_montserrat_20, UI_RED);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

  uint8_t n    = sizeof(ENTRIES) / sizeof(ENTRIES[0]);
  uint8_t rows = (n + COLS - 1) / COLS;
  int grid_w   = COLS * TILE_W + (COLS - 1) * GAP_X;
  int grid_h   = rows * TILE_H + (rows - 1) * GAP_Y;
  int x0 = (LV_HOR_RES - grid_w) / 2;
  int y0 = 30 + (LV_VER_RES - 30 - grid_h) / 2;

  for (uint8_t i = 0; i < n; ++i) {
    uint8_t col = i % COLS, row = i / COLS;
    lv_obj_t* b = ui_button(parent, ENTRIES[i].sym, cb_open,
                            (void*)(uintptr_t)ENTRIES[i].app);
    lv_obj_set_size(b, TILE_W, TILE_H);
    lv_obj_set_pos(b, x0 + col * (TILE_W + GAP_X),
                      y0 + row * (TILE_H + GAP_Y));
    lv_obj_t* nm = lv_label_create(b);
    lv_label_set_text(nm, ENTRIES[i].name);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(nm, lv_color_hex(UI_DIM), LV_PART_MAIN);
    lv_obj_align(nm, LV_ALIGN_BOTTOM_MID, 0, -4);
  }
}

} // namespace screen_drawer
