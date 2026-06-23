#include "screen_folder.h"
#include "ui.h"
#include "ui_theme.h"
#include <lvgl.h>

namespace {
  // Closing+reopening synchronously inside an LVGL event handler would delete
  // the button's parent overlay while LVGL still has it on the dispatch stack.
  // Defer the swap to the next LVGL tick via a one-shot timer so the current
  // event finishes unwinding before any widgets are freed.
  void deferred_open(lv_timer_t* t) {
    ui::App a = (ui::App)(uintptr_t)lv_timer_get_user_data(t);
    ui::closeApp();
    ui::openApp(a);
    lv_timer_delete(t);
  }
  void cb_sub(lv_event_t* e) {
    ui::App a = (ui::App)(uintptr_t)lv_event_get_user_data(e);
    lv_timer_t* t = lv_timer_create(deferred_open, 0, (void*)(uintptr_t)a);
    lv_timer_set_repeat_count(t, 1);
  }
}

namespace screen_folder {

void create(lv_obj_t* parent, const char* title,
            const Entry* entries, uint8_t count) {
  ui_fill_parent(parent);

  lv_obj_t* lbl = ui_label(parent, title, &lv_font_montserrat_20, UI_RED);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 8);

  // Folder content lives inside the app overlay's `content` tile, which is
  // already offset by the 36 px back bar. Title sits at y≈8; tiles start
  // below it. Use content-relative coordinates (no extra 36 px offset).
  const uint8_t COLS   = 3;
  const uint8_t TILE_W = 90, TILE_H = 68;
  const uint8_t GAP_X  = 12, GAP_Y  = 8;
  const uint8_t TITLE_H = 36;
  uint8_t rows = (count + COLS - 1) / COLS;
  int grid_w   = COLS * TILE_W + (COLS - 1) * GAP_X;
  int grid_h   = rows * TILE_H + (rows > 1 ? (rows - 1) * GAP_Y : 0);
  int avail_h  = LV_VER_RES - 36 - TITLE_H;
  int x0 = (LV_HOR_RES - grid_w) / 2;
  int y0 = TITLE_H + (avail_h - grid_h) / 2;
  if (y0 < TITLE_H) y0 = TITLE_H;

  for (uint8_t i = 0; i < count; ++i) {
    uint8_t col = i % COLS, row = i / COLS;
    lv_obj_t* b = ui_button(parent, entries[i].sym, cb_sub,
                            (void*)(uintptr_t)entries[i].app);
    lv_obj_set_size(b, TILE_W, TILE_H);
    lv_obj_set_pos(b, x0 + col * (TILE_W + GAP_X),
                      y0 + row * (TILE_H + GAP_Y));
    // Tile is short (68 px); a centered font_20 icon collides with a
    // BOTTOM_MID name label. Pin the icon near the top so the name has room.
    lv_obj_t* icon = lv_obj_get_child(b, 0);
    if (icon) lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t* nm = lv_label_create(b);
    lv_label_set_text(nm, entries[i].name);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(nm, lv_color_hex(UI_DIM), LV_PART_MAIN);
    lv_obj_align(nm, LV_ALIGN_BOTTOM_MID, 0, -4);
  }
}

} // namespace screen_folder
