// Stopwatch: MM:SS.mm readout + START/STOP/RESET + lap list. Resets when
// the screen closes (no persistence by design).
#include "screen_stopwatch.h"
#include "ui_theme.h"
#include <stdio.h>

static lv_obj_t* s_readout = nullptr;
static lv_obj_t* s_toggle  = nullptr;
static lv_obj_t* s_lapBtn  = nullptr;
static lv_obj_t* s_laps    = nullptr;

static bool     s_running = false;
static uint32_t s_started = 0;   // lv_tick at last START
static uint32_t s_elapsed = 0;   // accumulated ms across STOP/START
static uint32_t s_lastLap = 0;   // total ms at last lap

static void formatMs(uint32_t ms, char* out, size_t n) {
  uint32_t mm = ms / 60000;
  uint32_t ss = (ms / 1000) % 60;
  uint32_t cc = (ms / 10)  % 100;   // hundredths
  snprintf(out, n, "%02lu:%02lu.%02lu",
           (unsigned long)mm, (unsigned long)ss, (unsigned long)cc);
}

static uint32_t totalMs() {
  return s_elapsed + (s_running ? (lv_tick_get() - s_started) : 0);
}

static void refreshToggleLabel() {
  if (!s_toggle) return;
  lv_obj_t* l = lv_obj_get_child(s_toggle, 0);
  if (l) lv_label_set_text(l, s_running ? "STOP" : "START");
}

static void cb_toggle(lv_event_t*) {
  if (s_running) {
    s_elapsed += lv_tick_get() - s_started;
    s_running = false;
  } else {
    s_started = lv_tick_get();
    s_running = true;
  }
  refreshToggleLabel();
}

static void cb_lap(lv_event_t*) {
  if (!s_laps) return;
  uint32_t now = totalMs();
  uint32_t split = now - s_lastLap;
  s_lastLap = now;
  char buf[48], a[16], b[16];
  formatMs(split, a, sizeof(a));
  formatMs(now,   b, sizeof(b));
  uint32_t idx = lv_obj_get_child_cnt(s_laps) + 1;
  snprintf(buf, sizeof(buf), "%2lu  +%s  %s", (unsigned long)idx, a, b);
  lv_obj_t* row = ui_label(s_laps, buf, &lv_font_montserrat_14, UI_FG);
  lv_obj_set_width(row, 300);
  lv_obj_scroll_to_view(row, LV_ANIM_OFF);
}

static void cb_reset(lv_event_t*) {
  s_running = false;
  s_elapsed = 0;
  s_lastLap = 0;
  if (s_laps) lv_obj_clean(s_laps);
  refreshToggleLabel();
  if (s_readout) lv_label_set_text(s_readout, "00:00.00");
}

static void cb_destroy(lv_event_t*) {
  s_running = false;
  s_elapsed = 0;
  s_lastLap = 0;
  s_readout = s_toggle = s_lapBtn = s_laps = nullptr;
}

namespace screen_stopwatch {

void create(lv_obj_t* parent) {
  s_running = false;
  s_elapsed = 0;
  s_lastLap = 0;
  ui_fill_parent(parent);
  ui_label(parent, "STOPWATCH", &lv_font_montserrat_20, UI_RED);

  s_readout = ui_label(parent, "00:00.00", &lv_font_montserrat_20, UI_FG);
  lv_obj_set_style_text_font(s_readout, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_align(s_readout, LV_ALIGN_TOP_MID, 0, 36);

  s_toggle = ui_button(parent, "START", cb_toggle, nullptr);
  lv_obj_set_size(s_toggle, 96, 32);
  lv_obj_align(s_toggle, LV_ALIGN_TOP_LEFT, 8, 64);

  s_lapBtn = ui_button(parent, "LAP", cb_lap, nullptr);
  lv_obj_set_size(s_lapBtn, 96, 32);
  lv_obj_align(s_lapBtn, LV_ALIGN_TOP_MID, 0, 64);

  lv_obj_t* rst = ui_button(parent, "RESET", cb_reset, nullptr);
  lv_obj_set_size(rst, 96, 32);
  lv_obj_align(rst, LV_ALIGN_TOP_RIGHT, -8, 64);

  s_laps = lv_obj_create(parent);
  lv_obj_set_size(s_laps, 318, 130);
  lv_obj_align(s_laps, LV_ALIGN_TOP_LEFT, 1, 104);
  lv_obj_set_style_bg_color(s_laps, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_laps, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_laps, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(s_laps, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_laps, 4, LV_PART_MAIN);
  lv_obj_set_scroll_dir(s_laps, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_laps, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_layout(s_laps, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_laps, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_laps, 2, LV_PART_MAIN);

  lv_obj_add_event_cb(parent, cb_destroy, LV_EVENT_DELETE, nullptr);
}

void tick() {
  if (!s_readout) return;
  static uint32_t last = 0;
  uint32_t now = lv_tick_get();
  if (now - last < 50) return;
  last = now;
  char buf[16];
  formatMs(totalMs(), buf, sizeof(buf));
  lv_label_set_text(s_readout, buf);
}

} // namespace screen_stopwatch
