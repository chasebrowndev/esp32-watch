// Flashlight: full-white panel + backlight max. A slider lets the user dim.
// On close the user's stored brightness is restored via power::wake().
#include "screen_flashlight.h"
#include "ui_theme.h"
#include "../hal/display.h"
#include "../hal/power.h"

static lv_obj_t* s_panel  = nullptr;
static lv_obj_t* s_slider = nullptr;
static uint8_t   s_level  = 255;

static void cb_slider(lv_event_t* e) {
  lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
  int32_t v = lv_slider_get_value(sl);
  if (v < 5) v = 5;
  s_level = (uint8_t)v;
  disp::setBacklight(s_level);
}

static void cb_destroy(lv_event_t*) {
  // Hand brightness control back to the power manager. wake() re-applies
  // the persisted s_bright value held in power.cpp.
  power::wake();
  s_panel = s_slider = nullptr;
}

namespace screen_flashlight {

void create(lv_obj_t* parent) {
  s_panel = s_slider = nullptr;
  ui_fill_parent(parent);

  s_panel = lv_obj_create(parent);
  lv_obj_set_size(s_panel, 320, 240);
  lv_obj_align(s_panel, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(s_panel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_panel, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_panel, 0, LV_PART_MAIN);
  lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

  s_slider = lv_slider_create(s_panel);
  lv_slider_set_range(s_slider, 5, 255);
  lv_slider_set_value(s_slider, s_level, LV_ANIM_OFF);
  lv_obj_set_size(s_slider, 260, 12);
  lv_obj_align(s_slider, LV_ALIGN_BOTTOM_MID, 0, -18);
  lv_obj_set_style_bg_color(s_slider, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_slider, lv_color_hex(UI_RED),   LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_slider, lv_color_hex(UI_RED),   LV_PART_KNOB);
  lv_obj_set_style_radius(s_slider, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_slider, 0, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_slider, 0, LV_PART_KNOB);
  lv_obj_add_event_cb(s_slider, cb_slider, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_add_event_cb(parent, cb_destroy, LV_EVENT_DELETE, nullptr);

  disp::setBacklight(s_level);
}

void tick() {}

} // namespace screen_flashlight
