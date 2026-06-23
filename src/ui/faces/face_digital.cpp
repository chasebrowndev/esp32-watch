#include "face_digital.h"
#include "../ui_theme.h"
#include "../../hal/power.h"
#include <time.h>
#include <stdio.h>

static lv_obj_t* s_time;
static lv_obj_t* s_date;
static lv_obj_t* s_bat;

namespace face_digital {

void create(lv_obj_t* parent) {
  ui_fill_parent(parent);
  s_time = ui_label(parent, "--:--", &lv_font_montserrat_48, UI_RED);
  lv_obj_align(s_time, LV_ALIGN_CENTER, 0, -10);
  s_date = ui_label(parent, "no time sync", &lv_font_montserrat_20, UI_DIM);
  lv_obj_align(s_date, LV_ALIGN_CENTER, 0, 40);
  s_bat = ui_label(parent, LV_SYMBOL_BATTERY_FULL "  --", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_bat, LV_ALIGN_TOP_RIGHT, -8, 6);
}

static const char* batSymbol(uint8_t pct) {
  if (pct >= 87) return LV_SYMBOL_BATTERY_FULL;
  if (pct >= 62) return LV_SYMBOL_BATTERY_3;
  if (pct >= 37) return LV_SYMBOL_BATTERY_2;
  if (pct >= 12) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

void tick() {
  static uint32_t last = 0;
  uint32_t now = lv_tick_get();
  if (now - last < 1000) return;
  last = now;

  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  if (lt.tm_year > 120) {
    char hm[8], dt[32];
    strftime(hm, sizeof(hm), "%H:%M", &lt);
    strftime(dt, sizeof(dt), "%a %b %d", &lt);
    lv_label_set_text(s_time, hm);
    lv_label_set_text(s_date, dt);
  }

  uint8_t pct = power::batteryPct();
  char bb[24];
  snprintf(bb, sizeof(bb), "%s  %u%%", batSymbol(pct), pct);
  lv_label_set_text(s_bat, bb);
  lv_obj_set_style_text_color(
    s_bat, lv_color_hex(power::batteryLow() ? UI_RED : UI_DIM), LV_PART_MAIN);
}

} // namespace face_digital
