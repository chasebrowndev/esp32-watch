// Analog clock: tick marks around the dial + hour/minute/second hands as
// lv_line. Hands rotate via lv_line_set_points each tick.
#include "face_analog.h"
#include "../ui_theme.h"
#include <time.h>
#include <math.h>

namespace {
  // Face geometry. Tile is 320x240; center vertically with a small offset.
  constexpr int CX = 160, CY = 120, R = 110;
  lv_obj_t* s_hr;
  lv_obj_t* s_mn;
  lv_obj_t* s_sc;
  lv_obj_t* s_label;

  lv_point_precise_t s_hr_p[2];
  lv_point_precise_t s_mn_p[2];
  lv_point_precise_t s_sc_p[2];

  void setHand(lv_obj_t* line, lv_point_precise_t* pts, float angle, int len) {
    pts[0].x = CX; pts[0].y = CY;
    pts[1].x = CX + (lv_value_precise_t)(len * sinf(angle));
    pts[1].y = CY - (lv_value_precise_t)(len * cosf(angle));
    lv_line_set_points(line, pts, 2);
  }

  lv_obj_t* makeHand(lv_obj_t* parent, uint32_t color, int width) {
    lv_obj_t* l = lv_line_create(parent);
    lv_obj_set_style_line_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_line_width(l, width, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(l, false, LV_PART_MAIN);
    return l;
  }
}

namespace face_analog {

void create(lv_obj_t* parent) {
  ui_fill_parent(parent);

  // Tick marks. 12 chunky, 60 thin.
  for (int i = 0; i < 60; ++i) {
    float a = i * (2.0f * M_PI / 60.0f);
    int inner = (i % 5 == 0) ? R - 12 : R - 6;
    int x1 = CX + (int)(inner * sinf(a));
    int y1 = CY - (int)(inner * cosf(a));
    int x2 = CX + (int)(R * sinf(a));
    int y2 = CY - (int)(R * cosf(a));
    lv_obj_t* t = lv_line_create(parent);
    lv_point_precise_t pts[2] = {{x1, y1}, {x2, y2}};
    lv_line_set_points(t, pts, 2);
    lv_obj_set_style_line_color(
      t, lv_color_hex(i % 5 == 0 ? UI_FG : UI_DIM), LV_PART_MAIN);
    lv_obj_set_style_line_width(t, i % 5 == 0 ? 3 : 1, LV_PART_MAIN);
  }

  s_hr = makeHand(parent, UI_FG,  5);
  s_mn = makeHand(parent, UI_FG,  3);
  s_sc = makeHand(parent, UI_RED, 1);

  // Center hub
  lv_obj_t* hub = lv_obj_create(parent);
  lv_obj_set_size(hub, 8, 8);
  lv_obj_set_pos(hub, CX - 4, CY - 4);
  lv_obj_set_style_radius(hub, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(hub, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(hub, 0, LV_PART_MAIN);

  s_label = ui_label(parent, "", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_label, LV_ALIGN_BOTTOM_MID, 0, -6);
}

void tick() {
  static uint32_t last = 0;
  uint32_t now = lv_tick_get();
  if (now - last < 500) return;
  last = now;
  time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);

  float sec = lt.tm_sec;
  float min = lt.tm_min + sec / 60.0f;
  float hr  = (lt.tm_hour % 12) + min / 60.0f;

  setHand(s_hr, s_hr_p, hr  * (2.0f * M_PI / 12.0f), R - 50);
  setHand(s_mn, s_mn_p, min * (2.0f * M_PI / 60.0f), R - 25);
  setHand(s_sc, s_sc_p, sec * (2.0f * M_PI / 60.0f), R - 10);

  if (lt.tm_year > 120) {
    char dt[24]; strftime(dt, sizeof(dt), "%a %b %d", &lt);
    lv_label_set_text(s_label, dt);
  }
}

} // namespace face_analog
