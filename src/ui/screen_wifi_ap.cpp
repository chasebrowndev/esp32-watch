// Captive portal screen. Suspends STA mode, brings up an open softAP +
// hijack DNS + portal form via net/captive. Displays SSID, client count,
// and the last few /submit captures. Authorized testing only — captures
// are RAM-only and wiped when the screen closes.
//
// Each toggle ON picks a fresh SSID. With p=0.5 we clone a nearby AP seen
// in the latest scan (more convincing than a fixed name); otherwise we
// pick from a list of common open-network names.
#include "screen_wifi_ap.h"
#include "ui_theme.h"
#include "config.h"
#if FEAT_WIFI
#include "../net/wifi_mgr.h"
#include "../net/captive.h"
#include "../features/sd_config.h"
#include <WiFi.h>
#endif
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static lv_obj_t* s_status = nullptr;
static lv_obj_t* s_log    = nullptr;
static lv_obj_t* s_toggle = nullptr;
static lv_obj_t* s_ssidLbl = nullptr;
static lv_obj_t* s_customTa = nullptr;
// Edit overlay (same pattern as screen_wifi) — full-screen keyboard pops
// up when the custom-ssid textarea is focused.
static lv_obj_t* s_editPane = nullptr;
static lv_obj_t* s_editTa   = nullptr;
static lv_obj_t* s_kb       = nullptr;
static char      s_curSsid[33] = {0};

static const char* SSID_POOL_DEFAULT[] = {
  "FREE WIFI",
  "WIFIFREE",
  "GUESTWIFI",
  "Free Public WiFi",
  "xfinitywifi",
  "ATTwifi",
  "Starbucks WiFi",
  "Airport Free WiFi",
  "Hotel Guest",
  "Linksys",
  "NETGEAR",
};
// Runtime pool — seeded from defaults each time the screen opens, then
// optionally extended/overridden from /wifi_ap/ssids.txt on the SD card.
// One SSID per line; blank lines and lines starting with '#' are skipped.
static std::vector<String> s_ssidPool;

#if FEAT_WIFI
static void loadSsidPool() {
  s_ssidPool.clear();
  for (auto p : SSID_POOL_DEFAULT) s_ssidPool.emplace_back(p);
  String body;
  if (!sd_config::loadText("/wifi_ap/ssids.txt", body) || body.length() == 0) return;
  // Replace the embedded set entirely when SD provides one so the user gets
  // exactly what they curated. Tracks the ble_spam pattern.
  s_ssidPool.clear();
  int from = 0;
  while (from < (int)body.length()) {
    int nl = body.indexOf('\n', from);
    String line = (nl < 0) ? body.substring(from) : body.substring(from, nl);
    line.trim();
    if (line.length() && line[0] != '#' && line.length() <= 32) {
      s_ssidPool.emplace_back(line);
    }
    if (nl < 0) break;
    from = nl + 1;
  }
  if (s_ssidPool.empty()) {
    for (auto p : SSID_POOL_DEFAULT) s_ssidPool.emplace_back(p);
  } else {
    Serial.printf("[wifi_ap] ssid pool loaded from SD (%u)\n", (unsigned)s_ssidPool.size());
  }
}
#endif

#if FEAT_WIFI
static void pickSsid(char* out, size_t n) {
  // 50/50: clone a nearby AP from the latest scan vs. pick from the canned
  // list. Cloning only kicks in if there's at least one named AP to copy.
  int16_t sc = wifi::scanCount();
  bool clone = (esp_random() & 1) && sc > 0;
  if (clone) {
    int16_t idx = (int16_t)(esp_random() % (uint32_t)sc);
    const char* s = wifi::scanSsid(idx);
    if (s && s[0]) {
      strncpy(out, s, n - 1);
      out[n - 1] = 0;
      return;
    }
  }
  size_t pn = s_ssidPool.size();
  if (pn == 0) { out[0] = 0; return; }
  const String& s = s_ssidPool[esp_random() % pn];
  strncpy(out, s.c_str(), n - 1);
  out[n - 1] = 0;
}

static void refreshToggleLabel() {
  if (!s_toggle) return;
  lv_obj_t* l = lv_obj_get_child(s_toggle, 0);
  if (l) lv_label_set_text(l, captive::running() ? "STOP" : "START");
}

static void apStart() {
  // Custom takes precedence: if the user typed something into the textarea,
  // use it verbatim and skip the scan-and-clone path.
  const char* custom = s_customTa ? lv_textarea_get_text(s_customTa) : "";
  if (custom && custom[0]) {
    strncpy(s_curSsid, custom, sizeof(s_curSsid) - 1);
    s_curSsid[sizeof(s_curSsid) - 1] = 0;
  } else {
    // Kick a scan first so SSID-cloning has something to pick from. Best-effort
    // — if results aren't ready by the time we sample, we fall back to the pool.
    WiFi.mode(WIFI_STA);
    wifi::scanStart();
    delay(800);   // small window to let the scan return something usable
    pickSsid(s_curSsid, sizeof(s_curSsid));
  }
  captive::start(s_curSsid);
  if (s_ssidLbl) lv_label_set_text_fmt(s_ssidLbl, "ssid: %s", s_curSsid);
  refreshToggleLabel();
}
static void apStop() {
  captive::stop();
  s_curSsid[0] = 0;
  if (s_ssidLbl) lv_label_set_text(s_ssidLbl, "ssid: (off)");
  refreshToggleLabel();
}

static void cb_toggle(lv_event_t*) {
  if (captive::running()) apStop();
  else apStart();
}

static void enterEdit() {
  if (!s_editPane || !s_kb || !s_customTa) return;
  lv_textarea_set_text(s_editTa, lv_textarea_get_text(s_customTa));
  lv_keyboard_set_textarea(s_kb, s_editTa);
  lv_obj_clear_flag(s_editPane, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_kb,       LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_editPane);
  lv_obj_move_foreground(s_kb);
}
static void exitEdit(bool commit) {
  if (!s_editPane || !s_kb) return;
  if (commit && s_customTa) {
    lv_textarea_set_text(s_customTa, lv_textarea_get_text(s_editTa));
  }
  lv_keyboard_set_textarea(s_kb, nullptr);
  lv_obj_add_flag(s_kb,       LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_editPane, LV_OBJ_FLAG_HIDDEN);
}
static void cb_focus(lv_event_t*) { enterEdit(); }
static void cb_kb(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_READY)  exitEdit(true);
  if (c == LV_EVENT_CANCEL) exitEdit(false);
}
#endif

static void cb_destroy(lv_event_t*) {
#if FEAT_WIFI
  captive::stop();
  wifi::resume();
#endif
  s_status = s_log = s_toggle = s_ssidLbl = nullptr;
  s_customTa = s_editPane = s_editTa = s_kb = nullptr;
  s_curSsid[0] = 0;
}

namespace screen_wifi_ap {

void create(lv_obj_t* parent) {
  s_status = s_log = s_toggle = s_ssidLbl = nullptr;
  s_customTa = s_editPane = s_editTa = s_kb = nullptr;
  s_curSsid[0] = 0;
  ui_fill_parent(parent);
  ui_label(parent, "WIFI AP", &lv_font_montserrat_20, UI_RED);

#if FEAT_WIFI
  s_toggle = ui_button(parent, "START", cb_toggle, nullptr);
  lv_obj_set_size(s_toggle, 80, 26);
  lv_obj_align(s_toggle, LV_ALIGN_TOP_RIGHT, -8, 4);
#endif

  s_ssidLbl = ui_label(parent, "ssid: (off)", &lv_font_montserrat_14, UI_FG);
  lv_obj_align(s_ssidLbl, LV_ALIGN_TOP_LEFT, 12, 30);

  s_status = ui_label(parent, "0 clients", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 12, 50);

#if FEAT_WIFI
  s_customTa = lv_textarea_create(parent);
  lv_textarea_set_one_line(s_customTa, true);
  lv_textarea_set_placeholder_text(s_customTa, "custom ssid (blank = random)");
  lv_obj_set_size(s_customTa, 300, 26);
  lv_obj_align(s_customTa, LV_ALIGN_TOP_LEFT, 10, 70);
  lv_obj_set_style_radius(s_customTa, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_customTa, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_customTa, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_customTa, 1, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_customTa, lv_color_hex(UI_FG), LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_customTa, 4, LV_PART_MAIN);
  lv_obj_add_event_cb(s_customTa, cb_focus, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_customTa, cb_focus, LV_EVENT_CLICKED, nullptr);
#endif

  s_log = lv_obj_create(parent);
  lv_obj_set_size(s_log, 318, 104);
  lv_obj_align(s_log, LV_ALIGN_TOP_LEFT, 1, 100);
  lv_obj_set_style_bg_color(s_log, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_log, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_log, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(s_log, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_log, 4, LV_PART_MAIN);
  lv_obj_set_scroll_dir(s_log, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_log, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_layout(s_log, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_log, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_log, 2, LV_PART_MAIN);

  lv_obj_add_event_cb(parent, cb_destroy, LV_EVENT_DELETE, nullptr);

#if FEAT_WIFI
  // Keyboard overlay for the custom-ssid textarea. Hidden until focused.
  s_editPane = lv_obj_create(parent);
  ui_fill_parent(s_editPane);
  lv_obj_set_size(s_editPane, 320, 80);
  lv_obj_align(s_editPane, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(s_editPane, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_editPane, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_editPane, 1, LV_PART_MAIN);
  lv_obj_set_style_border_side(s_editPane, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_editPane, 4, LV_PART_MAIN);

  lv_obj_t* hint = ui_label(s_editPane, "ssid", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 4, 2);

  s_editTa = lv_textarea_create(s_editPane);
  lv_textarea_set_one_line(s_editTa, true);
  lv_obj_set_size(s_editTa, 308, 44);
  lv_obj_align(s_editTa, LV_ALIGN_TOP_LEFT, 4, 22);
  lv_obj_set_style_radius(s_editTa, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_editTa, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_editTa, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_editTa, 1, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_editTa, lv_color_hex(UI_FG), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_editTa, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_add_flag(s_editPane, LV_OBJ_FLAG_HIDDEN);

  s_kb = lv_keyboard_create(parent);
  lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_set_size(s_kb, 320, 124);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(s_kb, cb_kb, LV_EVENT_READY,  nullptr);
  lv_obj_add_event_cb(s_kb, cb_kb, LV_EVENT_CANCEL, nullptr);

  // Pause STA on entry; user must press START to actually open the AP.
  wifi::suspend();
  loadSsidPool();
#endif
}

void tick() {
#if FEAT_WIFI
  if (!s_status || !s_log) return;
  captive::tick();

  static uint32_t last = 0;
  uint32_t now = lv_tick_get();
  if (now - last < 500) return;
  last = now;

  if (!captive::running()) {
    lv_label_set_text(s_status, "off");
    return;
  }

  char tmp[80];
  snprintf(tmp, sizeof(tmp), "clients: %u", (unsigned)captive::clientCount());
  lv_label_set_text(s_status, tmp);

  captive::Hit hits[10];
  uint8_t n = captive::snapshot(hits, 10);

  uint32_t childCount = lv_obj_get_child_cnt(s_log);
  while (childCount > n) {
    lv_obj_delete(lv_obj_get_child(s_log, childCount - 1));
    childCount--;
  }
  for (uint8_t i = 0; i < n; ++i) {
    snprintf(tmp, sizeof(tmp), "%6lus  %s : %s",
             (unsigned long)(hits[i].ms / 1000), hits[i].user, hits[i].pass);
    if (i < childCount) {
      lv_label_set_text(lv_obj_get_child(s_log, i), tmp);
    } else {
      lv_obj_t* row = ui_label(s_log, tmp, &lv_font_montserrat_14, UI_FG);
      lv_obj_set_width(row, 300);
    }
  }
#endif
}

} // namespace screen_wifi_ap
