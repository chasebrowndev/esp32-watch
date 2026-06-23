// Shared UI styling. Themes + ui_button/ui_label helpers used by every face and
// screen. Colors resolve through theme::cur() at widget-creation time, so a
// rebuild after theme switch is enough to repaint everything.
#pragma once
#include <lvgl.h>
#include <stdint.h>

namespace theme {
  struct Palette {
    const char* name;
    uint32_t accent;   // primary tint (titles, borders, indicators)
    uint32_t bg;       // background
    uint32_t fg;       // primary text
    uint32_t dim;      // secondary text
    uint32_t panel;    // raised surfaces (buttons, sliders)
  };
  uint8_t  count();
  const Palette& at(uint8_t i);
  const Palette& cur();
  uint8_t  current();
  void     setCurrent(uint8_t i);   // persists + rebuilds face

  void init();   // load saved selection from NVS
}

// Compatibility macros — existing call sites do `lv_color_hex(UI_RED)` etc.
// Resolving via cur() at use time means a face-rebuild fully recolors the UI.
#define UI_RED    (theme::cur().accent)
#define UI_BG     (theme::cur().bg)
#define UI_FG     (theme::cur().fg)
#define UI_DIM    (theme::cur().dim)
#define UI_PANEL  (theme::cur().panel)

static inline void ui_fill_parent(lv_obj_t* o) {
  lv_obj_set_style_bg_color(o, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(o, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static inline lv_obj_t* ui_button(lv_obj_t* parent, const char* txt,
                                  lv_event_cb_t cb, void* user) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_style_radius(b, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_color(b, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_FG), LV_PART_MAIN);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  return b;
}

static inline lv_obj_t* ui_label(lv_obj_t* parent, const char* txt,
                                 const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
  lv_label_set_text(l, txt);
  return l;
}
