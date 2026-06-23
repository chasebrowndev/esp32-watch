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

// Build a newline-joined option string for an lv_dropdown.
static void joinNames(char* out, size_t cap, uint8_t n, const char* (*get)(uint8_t)) {
  out[0] = 0;
  for (uint8_t i = 0; i < n; ++i) {
    if (i) strncat(out, "\n", cap - strlen(out) - 1);
    strncat(out, get(i), cap - strlen(out) - 1);
  }
}

namespace screen_settings {

void create(lv_obj_t* parent) {
  s_bright = s_brightVal = s_dstSw = s_faceDd = s_themeDd = s_sdRow = s_aboutRow = nullptr;
  ui_fill_parent(parent);
  ui_label(parent, "SETTINGS", &lv_font_montserrat_20, UI_RED);

  // Brightness ---------------------------------------------------------------
  lv_obj_t* lbl = ui_label(parent, "brightness", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 12, 30);

  s_bright = lv_slider_create(parent);
  lv_slider_set_range(s_bright, 5, 255);
  lv_slider_set_value(s_bright, power::brightness(), LV_ANIM_OFF);
  lv_obj_set_size(s_bright, 140, 10);
  lv_obj_align(s_bright, LV_ALIGN_TOP_LEFT, 12, 50);
  lv_obj_set_style_bg_color(s_bright, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_bright, lv_color_hex(UI_RED), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_bright, lv_color_hex(UI_RED), LV_PART_KNOB);
  lv_obj_set_style_radius(s_bright, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_bright, 0, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_bright, 0, LV_PART_KNOB);
  lv_obj_add_event_cb(s_bright, cb_bright, LV_EVENT_VALUE_CHANGED, nullptr);

  char vb[8]; snprintf(vb, sizeof(vb), "%u", power::brightness());
  s_brightVal = ui_label(parent, vb, &lv_font_montserrat_14, UI_FG);
  lv_obj_align(s_brightVal, LV_ALIGN_TOP_LEFT, 158, 47);

  // DST switch ---------------------------------------------------------------
  lv_obj_t* dstLbl = ui_label(parent, "dst (+1h)", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(dstLbl, LV_ALIGN_TOP_LEFT, 12, 72);
  s_dstSw = lv_switch_create(parent);
  lv_obj_set_size(s_dstSw, 50, 24);
  lv_obj_align(s_dstSw, LV_ALIGN_TOP_LEFT, 110, 68);
  lv_obj_set_style_bg_color(s_dstSw, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_dstSw, lv_color_hex(UI_RED), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_radius(s_dstSw, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_dstSw, 0, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_dstSw, 0, LV_PART_KNOB);
  if (ntp::dst()) lv_obj_add_state(s_dstSw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(s_dstSw, cb_dst, LV_EVENT_VALUE_CHANGED, nullptr);

  // Face dropdown ------------------------------------------------------------
  lv_obj_t* faceLbl = ui_label(parent, "face", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(faceLbl, LV_ALIGN_TOP_LEFT, 180, 30);
  s_faceDd = lv_dropdown_create(parent);
  char faceOpts[256];
  joinNames(faceOpts, sizeof(faceOpts), faces::count(),
            [](uint8_t i) { return faces::at(i).name; });
  lv_dropdown_set_options(s_faceDd, faceOpts);
  lv_dropdown_set_selected(s_faceDd, faces::current());
  lv_obj_set_size(s_faceDd, 120, 30);
  lv_obj_align(s_faceDd, LV_ALIGN_TOP_LEFT, 180, 48);
  lv_obj_set_style_bg_color(s_faceDd, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_faceDd, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_faceDd, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(s_faceDd, 0, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_faceDd, lv_color_hex(UI_FG), LV_PART_MAIN);
  lv_obj_add_event_cb(s_faceDd, cb_face, LV_EVENT_VALUE_CHANGED, nullptr);

  // Theme dropdown -----------------------------------------------------------
  lv_obj_t* themeLbl = ui_label(parent, "theme", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(themeLbl, LV_ALIGN_TOP_LEFT, 180, 82);
  s_themeDd = lv_dropdown_create(parent);
  char themeOpts[256];
  joinNames(themeOpts, sizeof(themeOpts), theme::count(),
            [](uint8_t i) { return theme::at(i).name; });
  lv_dropdown_set_options(s_themeDd, themeOpts);
  lv_dropdown_set_selected(s_themeDd, theme::current());
  lv_obj_set_size(s_themeDd, 120, 30);
  lv_obj_align(s_themeDd, LV_ALIGN_TOP_LEFT, 180, 100);
  lv_obj_set_style_bg_color(s_themeDd, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_themeDd, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_themeDd, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(s_themeDd, 0, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_themeDd, lv_color_hex(UI_FG), LV_PART_MAIN);
  lv_obj_add_event_cb(s_themeDd, cb_theme, LV_EVENT_VALUE_CHANGED, nullptr);

  // SD card row --------------------------------------------------------------
  s_sdRow = ui_label(parent, "sd: --", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_sdRow, LV_ALIGN_TOP_LEFT, 12, 100);

  // About row (heap, MAC) ----------------------------------------------------
  s_aboutRow = ui_label(parent, "--", &lv_font_montserrat_14, UI_DIM);
  lv_label_set_long_mode(s_aboutRow, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_aboutRow, 296);
  lv_obj_align(s_aboutRow, LV_ALIGN_TOP_LEFT, 12, 128);

  // Reboot button ------------------------------------------------------------
  lv_obj_t* btn = ui_button(parent, "REBOOT", cb_reboot, nullptr);
  lv_obj_set_size(btn, 100, 30);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
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
