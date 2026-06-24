#include "screen_settings.h"
#include "ui_theme.h"
#include "ui_faces.h"
#include "ui.h"
#include "config.h"
#include "../hal/power.h"
#include "../features/sdcard.h"
#include "../net/sntp.h"
#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t* s_bright    = nullptr;
static lv_obj_t* s_brightVal = nullptr;
static lv_obj_t* s_dstSw     = nullptr;
static lv_obj_t* s_faceDd    = nullptr;
static lv_obj_t* s_themeDd   = nullptr;
static lv_obj_t* s_sdRow     = nullptr;
static lv_obj_t* s_aboutRow  = nullptr;

static void cb_bright(lv_event_t* e) {
  lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
  int v = lv_slider_get_value(sl);
  power::setBrightness((uint8_t)v);
  char buf[8]; snprintf(buf, sizeof(buf), "%d", v);
  if (s_brightVal) lv_label_set_text(s_brightVal, buf);
}

static void cb_dst(lv_event_t* e) {
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
  ntp::setDst(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void cb_face(lv_event_t* e) {
  lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
  faces::setCurrent((uint8_t)lv_dropdown_get_selected(dd));
}

static void cb_theme(lv_event_t* e) {
  lv_obj_t* dd = (lv_obj_t*)lv_event_get_target(e);
  theme::setCurrent((uint8_t)lv_dropdown_get_selected(dd));
  // After setCurrent rebuilds the watchface tile the Settings overlay still
  // has its previous palette. Cheapest fix: close+reopen on next tick. For
  // now we leave the overlay in stale colors until the user backs out and
  // reopens — easier than redoing styles in place.
}

static void cb_reboot(lv_event_t*) {
  delay(100);
  ESP.restart();
}

static void cb_modal_cancel(lv_event_t* e) {
  lv_obj_t* modal = (lv_obj_t*)lv_event_get_user_data(e);
  if (modal) lv_obj_del(modal);
}

static void cb_modal_off(lv_event_t*) {
  power::powerOff();  // does not return
}

static void showDeepSleepConfirm();

static void cb_deepsleep(lv_event_t*) {
  showDeepSleepConfirm();
}

// Build a newline-joined option string for an lv_dropdown.
static void joinNames(char* out, size_t cap, uint8_t n, const char* (*get)(uint8_t)) {
  out[0] = 0;
  for (uint8_t i = 0; i < n; ++i) {
    if (i) strncat(out, "\n", cap - strlen(out) - 1);
    strncat(out, get(i), cap - strlen(out) - 1);
  }
}

// Section header: red accent label with a thin underline.
static void sectionHeader(lv_obj_t* parent, const char* text) {
  lv_obj_t* h = ui_label(parent, text, &lv_font_montserrat_14, UI_RED);
  lv_obj_set_width(h, LV_PCT(100));
  lv_obj_set_style_pad_top(h, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(h, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(h, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(h, 1, LV_PART_MAIN);
  lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
}

// Two-column row: a left-aligned label and a right-aligned control area.
// Returns the row container so callers can drop the control into it.
static lv_obj_t* row(lv_obj_t* parent, const char* label) {
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_set_width(r, LV_PCT(100));
  lv_obj_set_height(r, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(r, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(r, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(r, 8, LV_PART_MAIN);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  if (label) {
    lv_obj_t* l = ui_label(r, label, &lv_font_montserrat_14, UI_DIM);
    lv_obj_set_flex_grow(l, 0);
  }
  return r;
}

static void styleDropdown(lv_obj_t* dd) {
  lv_obj_set_style_bg_color(dd, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_color(dd, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(dd, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(dd, 0, LV_PART_MAIN);
  lv_obj_set_style_text_color(dd, lv_color_hex(UI_FG), LV_PART_MAIN);
}

static void showDeepSleepConfirm() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_t* modal = lv_obj_create(scr);
  lv_obj_remove_style_all(modal);
  lv_obj_set_size(modal, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_pos(modal, 0, 0);
  lv_obj_set_style_bg_color(modal, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(modal, LV_OPA_90, LV_PART_MAIN);
  lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* panel = lv_obj_create(modal);
  lv_obj_set_size(panel, 220, 130);
  lv_obj_center(panel);
  lv_obj_set_style_bg_color(panel, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_color(panel, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = ui_label(panel, "POWER OFF", &lv_font_montserrat_20, UI_RED);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t* body = ui_label(panel, "press reset to wake", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 36);

  lv_obj_t* cancel = ui_button(panel, "CANCEL", cb_modal_cancel, modal);
  lv_obj_set_size(cancel, 90, 32);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t* off = ui_button(panel, "POWER OFF", cb_modal_off, nullptr);
  lv_obj_set_size(off, 100, 32);
  lv_obj_align(off, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

namespace screen_settings {

void create(lv_obj_t* parent) {
  s_bright = s_brightVal = s_dstSw = s_faceDd = s_themeDd = s_sdRow = s_aboutRow = nullptr;
  ui_fill_parent(parent);

  lv_obj_t* title = ui_label(parent, "SETTINGS", &lv_font_montserrat_20, UI_RED);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

  // Scrollable list container for everything below the title.
  lv_obj_t* list = lv_obj_create(parent);
  lv_obj_remove_style_all(list);
  lv_obj_set_size(list, LV_HOR_RES, LV_VER_RES - 30 - 36);  // minus title + back bar
  lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, 30);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_hor(list, 12, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(list, 12, LV_PART_MAIN);
  lv_obj_set_style_pad_row(list, 2, LV_PART_MAIN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);

  // ===== DISPLAY =====
  sectionHeader(list, "DISPLAY");

  // Brightness: label + value sit on top row, slider gets its own full-width row.
  {
    lv_obj_t* r = row(list, "brightness");
    char vb[8]; snprintf(vb, sizeof(vb), "%u", power::brightness());
    s_brightVal = ui_label(r, vb, &lv_font_montserrat_14, UI_FG);
  }
  {
    s_bright = lv_slider_create(list);
    lv_slider_set_range(s_bright, 5, 255);
    lv_slider_set_value(s_bright, power::brightness(), LV_ANIM_OFF);
    lv_obj_set_size(s_bright, LV_PCT(100), 10);
    lv_obj_set_style_bg_color(s_bright, lv_color_hex(UI_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bright, lv_color_hex(UI_RED), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bright, lv_color_hex(UI_RED), LV_PART_KNOB);
    lv_obj_set_style_radius(s_bright, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bright, 0, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bright, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(s_bright, cb_bright, LV_EVENT_VALUE_CHANGED, nullptr);
  }
  {
    lv_obj_t* r = row(list, "theme");
    s_themeDd = lv_dropdown_create(r);
    char opts[256];
    joinNames(opts, sizeof(opts), theme::count(), [](uint8_t i) { return theme::at(i).name; });
    lv_dropdown_set_options(s_themeDd, opts);
    lv_dropdown_set_selected(s_themeDd, theme::current());
    lv_obj_set_size(s_themeDd, 160, 30);
    styleDropdown(s_themeDd);
    lv_obj_add_event_cb(s_themeDd, cb_theme, LV_EVENT_VALUE_CHANGED, nullptr);
  }
  {
    lv_obj_t* r = row(list, "face");
    s_faceDd = lv_dropdown_create(r);
    char opts[256];
    joinNames(opts, sizeof(opts), faces::count(), [](uint8_t i) { return faces::at(i).name; });
    lv_dropdown_set_options(s_faceDd, opts);
    lv_dropdown_set_selected(s_faceDd, faces::current());
    lv_obj_set_size(s_faceDd, 160, 30);
    styleDropdown(s_faceDd);
    lv_obj_add_event_cb(s_faceDd, cb_face, LV_EVENT_VALUE_CHANGED, nullptr);
  }

  // ===== TIME =====
  sectionHeader(list, "TIME");
  {
    lv_obj_t* r = row(list, "dst (+1h)");
    s_dstSw = lv_switch_create(r);
    lv_obj_set_size(s_dstSw, 50, 24);
    lv_obj_set_style_bg_color(s_dstSw, lv_color_hex(UI_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dstSw, lv_color_hex(UI_RED), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(s_dstSw, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_dstSw, 0, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_dstSw, 0, LV_PART_KNOB);
    if (ntp::dst()) lv_obj_add_state(s_dstSw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_dstSw, cb_dst, LV_EVENT_VALUE_CHANGED, nullptr);
  }

  // ===== POWER =====
  sectionHeader(list, "POWER");
  {
    lv_obj_t* btn = ui_button(list, "DEEP SLEEP", cb_deepsleep, nullptr);
    lv_obj_set_size(btn, LV_PCT(100), 32);
  }
  {
    lv_obj_t* btn = ui_button(list, "REBOOT", cb_reboot, nullptr);
    lv_obj_set_size(btn, LV_PCT(100), 32);
  }

  // ===== ABOUT =====
  sectionHeader(list, "ABOUT");
  s_sdRow = ui_label(list, "sd: --", &lv_font_montserrat_14, UI_DIM);
  lv_obj_set_width(s_sdRow, LV_PCT(100));

  s_aboutRow = ui_label(list, "--", &lv_font_montserrat_14, UI_DIM);
  lv_label_set_long_mode(s_aboutRow, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_aboutRow, LV_PCT(100));
}

void tick() {
  static uint32_t last = 0;
  uint32_t now = lv_tick_get();
  if (now - last < 1000) return;
  last = now;

  if (s_sdRow) {
    char buf[64];
    if (sdcard::mounted()) {
      uint64_t mb = sdcard::totalBytes() / (1024ULL * 1024ULL);
      uint64_t um = sdcard::usedBytes()  / (1024ULL * 1024ULL);
      snprintf(buf, sizeof(buf), "sd: %s  %llu/%llu MB",
               sdcard::typeStr(), (unsigned long long)um, (unsigned long long)mb);
    } else {
      snprintf(buf, sizeof(buf), "sd: not detected");
    }
    lv_label_set_text(s_sdRow, buf);
  }

  if (s_aboutRow) {
    char buf[192];
    String mac = WiFi.macAddress();
    String ip  = WiFi.localIP().toString();
    snprintf(buf, sizeof(buf),
             "bat %u%% (%u mV)\nheap %u KB  free %u KB\nmac %s\nip  %s\nbuild %s",
             power::batteryPct(), power::batteryMv(),
             (unsigned)(ESP.getHeapSize() / 1024),
             (unsigned)(ESP.getFreeHeap() / 1024),
             mac.c_str(),
             ip.c_str(),
             __DATE__);
    lv_label_set_text(s_aboutRow, buf);
  }
}

} // namespace screen_settings
