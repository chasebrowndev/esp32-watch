// WiFi screen: status, saved-network list with forget button, add form.
// Keyboard is hidden until a textarea is focused, then it overlays the bottom.
#include "screen_wifi.h"
#include "ui_theme.h"
#include "config.h"
#if FEAT_WIFI
#include "../net/wifi_mgr.h"
#include <WiFi.h>
#include <esp_wifi.h>
#endif
#include <string.h>
#include <stdio.h>

static lv_obj_t* s_status   = nullptr;
static lv_obj_t* s_savedBox = nullptr;
static lv_obj_t* s_ssidTa   = nullptr;
static lv_obj_t* s_passTa   = nullptr;
// Edit overlay: shown while keyboard is up so the textarea stays visible.
static lv_obj_t* s_editPane = nullptr;
static lv_obj_t* s_editHint = nullptr;
static lv_obj_t* s_editTa   = nullptr;
static lv_obj_t* s_kb       = nullptr;
static lv_obj_t* s_editTarget = nullptr;  // ssidTa or passTa — where to write back

// Nearby-AP picker overlay state. Sniffs beacons (same pattern as deauth
// screen) and lets the user tap a row to fill the SSID textarea without
// typing. Active only while the picker pane is visible.
static lv_obj_t* s_pickPane   = nullptr;
static lv_obj_t* s_pickList   = nullptr;
static lv_obj_t* s_pickStatus = nullptr;
namespace {
  // enc: 0 open, 1 WEP/legacy, 2 WPA, 3 WPA2, 4 WPA3
  struct PickAp { uint8_t bssid[6]; char ssid[33]; uint8_t channel; int8_t rssi; uint8_t enc; };
  const char* encName(uint8_t e) {
    switch (e) {
      case 0: return "open";
      case 1: return "wep";
      case 2: return "wpa";
      case 3: return "wpa2";
      case 4: return "wpa3";
      default: return "?";
    }
  }
  const uint8_t PICK_MAX = 20;
  volatile uint8_t s_pickCount = 0;
  PickAp   s_pickAps[PICK_MAX];
  uint8_t  s_pickCh       = 1;
  uint32_t s_pickLastHop  = 0;
  uint32_t s_pickLastDraw = 0;
  bool     s_pickArmed    = false;
}

#if FEAT_WIFI
static void rebuildSavedList();

static void enterEdit(lv_obj_t* ta, const char* hint, bool pwd) {
  if (!s_editPane || !s_kb) return;
  s_editTarget = ta;
  lv_label_set_text(s_editHint, hint);
  lv_textarea_set_password_mode(s_editTa, pwd);
  lv_textarea_set_text(s_editTa, lv_textarea_get_text(ta));
  lv_keyboard_set_textarea(s_kb, s_editTa);
  lv_obj_clear_flag(s_editPane, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_kb,       LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_editPane);
  lv_obj_move_foreground(s_kb);
}
static void exitEdit(bool commit) {
  if (!s_editPane || !s_kb) return;
  if (commit && s_editTarget) {
    lv_textarea_set_text(s_editTarget, lv_textarea_get_text(s_editTa));
  }
  lv_keyboard_set_textarea(s_kb, nullptr);
  lv_obj_add_flag(s_kb,       LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_editPane, LV_OBJ_FLAG_HIDDEN);
  s_editTarget = nullptr;
}

static void cb_focus(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  const char* hint = (ta == s_passTa) ? "password" : "ssid";
  bool pwd = (ta == s_passTa);
  enterEdit(ta, hint, pwd);
}
static void cb_kb(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_READY)  exitEdit(true);
  if (c == LV_EVENT_CANCEL) exitEdit(false);
}
static void cb_add(lv_event_t*) {
  // Commit any in-progress keyboard edit first — otherwise the user could tap
  // ADD with the keyboard still showing their typed SSID, and we'd read the
  // background textarea (which only updates on EVENT_READY).
  if (s_editTarget) exitEdit(true);
  const char* ss = lv_textarea_get_text(s_ssidTa);
  const char* pw = lv_textarea_get_text(s_passTa);
  if (!ss || !ss[0]) return;
  wifi::addNetwork(ss, pw ? pw : "");
  lv_textarea_set_text(s_passTa, "");
  rebuildSavedList();
}
static void cb_forget(lv_event_t* e) {
  uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
  wifi::removeNetwork((uint8_t)idx);
  rebuildSavedList();
}

static void IRAM_ATTR pickSniffCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* p = pkt->payload;
  if ((p[0] & 0xFC) != 0x80) return;        // not a beacon
  const uint8_t* bssid = &p[16];
  // Capability info field is at offsets 34-35 (after MAC hdr + timestamp +
  // beacon interval). Privacy bit (encryption present) is bit 4 of the low
  // byte. We later refine wpa/wpa2/wpa3 from the RSN/WPA tags.
  bool privacy = (p[34] & 0x10) != 0;
  const uint8_t* tags  = &p[36];
  int total = pkt->rx_ctrl.sig_len - 36 - 4;  // strip header+fixed and FCS
  if (total < 2) return;

  // Walk all tagged parameters: tag 0 = SSID, tag 48 = RSN (WPA2/3),
  // tag 221 vendor with OUI 00:50:F2 type 01 = WPA legacy.
  const uint8_t* ssidPtr = nullptr;
  uint8_t        ssidLen = 0;
  bool hasRSN = false, hasWPA = false, hasSAE = false;
  int off = 0;
  while (off + 2 <= total) {
    uint8_t tag  = tags[off];
    uint8_t tlen = tags[off + 1];
    if (off + 2 + tlen > total) break;
    const uint8_t* val = &tags[off + 2];
    if (tag == 0 && tlen <= 32) {           // SSID
      ssidPtr = val;
      ssidLen = tlen;
    } else if (tag == 48 && tlen >= 2) {    // RSN IE
      hasRSN = true;
      // RSN layout: ver(2) groupCipher(4) pairwiseCount(2) pairwiseList(4*n)
      //             akmCount(2) akmList(4*n) ...
      // Walk to the AKM list; suite type 8 in OUI 00:0F:AC = SAE = WPA3.
      if (tlen >= 8) {
        uint16_t pairCount = (uint16_t)val[6] | ((uint16_t)val[7] << 8);
        int akmOff = 8 + pairCount * 4;
        if (akmOff + 2 <= tlen) {
          uint16_t akmCount = (uint16_t)val[akmOff] | ((uint16_t)val[akmOff + 1] << 8);
          int akmListOff = akmOff + 2;
          for (uint16_t k = 0; k < akmCount && akmListOff + 4 <= tlen; ++k, akmListOff += 4) {
            if (val[akmListOff] == 0x00 && val[akmListOff + 1] == 0x0F &&
                val[akmListOff + 2] == 0xAC && val[akmListOff + 3] == 8) {
              hasSAE = true;
            }
          }
        }
      }
    } else if (tag == 221 && tlen >= 4) {   // Vendor specific
      if (val[0] == 0x00 && val[1] == 0x50 && val[2] == 0xF2 && val[3] == 0x01) {
        hasWPA = true;                       // Microsoft WPA IE
      }
    }
    off += 2 + tlen;
  }
  if (!ssidPtr || ssidLen == 0) return;     // need an SSID to dedup on (paired with BSSID)

  uint8_t enc;
  if      (!privacy)        enc = 0;        // open
  else if (hasSAE)          enc = 4;        // wpa3
  else if (hasRSN)          enc = 3;        // wpa2
  else if (hasWPA)          enc = 2;        // wpa
  else                       enc = 1;       // wep/legacy

  int idx = -1;
  for (uint8_t i = 0; i < s_pickCount; ++i) {
    if (memcmp(s_pickAps[i].bssid, bssid, 6) == 0) { idx = i; break; }
  }
  if (idx < 0) {
    if (s_pickCount >= PICK_MAX) return;
    idx = s_pickCount++;
    memcpy(s_pickAps[idx].bssid, bssid, 6);
    memcpy(s_pickAps[idx].ssid, ssidPtr, ssidLen);
    s_pickAps[idx].ssid[ssidLen] = 0;
  }
  s_pickAps[idx].channel = pkt->rx_ctrl.channel;
  s_pickAps[idx].rssi    = pkt->rx_ctrl.rssi;
  s_pickAps[idx].enc     = enc;
}

static void pickerStop() {
  if (s_pickArmed) {
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    s_pickArmed = false;
    wifi::resume();
  }
  if (s_pickPane) lv_obj_add_flag(s_pickPane, LV_OBJ_FLAG_HIDDEN);
}

static void cb_pickRow(lv_event_t* e) {
  uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  if (i >= s_pickCount) return;
  lv_textarea_set_text(s_ssidTa, s_pickAps[i].ssid);
  pickerStop();
}
static void cb_pickCancel(lv_event_t*) { pickerStop(); }

static void cb_pick(lv_event_t*) {
  if (s_pickArmed || !s_pickPane) return;
  s_pickCount    = 0;
  s_pickCh       = 1;
  s_pickLastHop  = s_pickLastDraw = 0;
  if (s_pickList) lv_obj_clean(s_pickList);
  if (s_pickStatus) lv_label_set_text(s_pickStatus, "scanning...");
  lv_obj_clear_flag(s_pickPane, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_pickPane);
  wifi::suspend();
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&pickSniffCb);
  esp_wifi_set_channel(s_pickCh, WIFI_SECOND_CHAN_NONE);
  s_pickArmed = true;
}

static void cb_destroy(lv_event_t*) {
  // If user backs out of the screen while picker is mid-sniff, tear down
  // promiscuous mode so the radio returns to STA before resume.
  if (s_pickArmed) {
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    s_pickArmed = false;
    wifi::resume();
  }
}

static void rebuildSavedList() {
  if (!s_savedBox) return;
  lv_obj_clean(s_savedBox);
  uint8_t n = wifi::savedCount();
  if (!n) {
    lv_obj_t* l = ui_label(s_savedBox, "(no saved networks)", &lv_font_montserrat_14, UI_DIM);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 4, 4);
    return;
  }
  for (uint8_t i = 0; i < n; ++i) {
    lv_obj_t* row = lv_obj_create(s_savedBox);
    ui_fill_parent(row);
    lv_obj_set_size(row, 280, 26);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, i * 28);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 2, LV_PART_MAIN);

    lv_obj_t* lbl = ui_label(row, wifi::savedSsid(i), &lv_font_montserrat_14, UI_FG);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* x = ui_button(row, LV_SYMBOL_TRASH, cb_forget, (void*)(uintptr_t)i);
    lv_obj_set_size(x, 40, 22);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, -2, 0);
  }
}
#endif

static lv_obj_t* mkField(lv_obj_t* parent, const char* placeholder, int y, bool pwd) {
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_textarea_set_password_mode(ta, pwd);
  lv_obj_set_size(ta, 200, 26);
  lv_obj_align(ta, LV_ALIGN_TOP_LEFT, 10, y);
  lv_obj_set_style_radius(ta, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ta, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_color(ta, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
  lv_obj_set_style_text_color(ta, lv_color_hex(UI_FG), LV_PART_MAIN);
  lv_obj_set_style_pad_all(ta, 4, LV_PART_MAIN);
#if FEAT_WIFI
  lv_obj_add_event_cb(ta, cb_focus, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(ta, cb_focus, LV_EVENT_CLICKED, nullptr);
#endif
  return ta;
}

namespace screen_wifi {

// Content area is 320 wide × ~204 tall (display minus 36px back bar).
// Layout (top→bottom):
//   y=0-22   title
//   y=26-42  status row
//   y=46-130 saved networks scroll box (84 tall ≈ 3 rows)
//   y=136-162 ssid field + ADD button
//   y=166-192 password field + clear button
// Keyboard hidden by default, overlays bottom when a field is focused.
void create(lv_obj_t* parent) {
  s_status = s_savedBox = s_ssidTa = s_passTa = nullptr;
  s_editPane = s_editHint = s_editTa = s_kb = s_editTarget = nullptr;
  s_pickPane = s_pickList = s_pickStatus = nullptr;
  s_pickArmed = false;
  s_pickCount = 0;
  ui_fill_parent(parent);
  ui_label(parent, "WIFI", &lv_font_montserrat_20, UI_RED);
#if FEAT_WIFI
  lv_obj_t* pickBtn = ui_button(parent, "PICK", cb_pick, nullptr);
  lv_obj_set_size(pickBtn, 70, 22);
  lv_obj_align(pickBtn, LV_ALIGN_TOP_RIGHT, -8, 2);
#endif
  s_status = ui_label(parent, "--", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 10, 26);

  s_savedBox = lv_obj_create(parent);
  ui_fill_parent(s_savedBox);
  lv_obj_set_size(s_savedBox, 300, 86);
  lv_obj_align(s_savedBox, LV_ALIGN_TOP_LEFT, 10, 46);
  lv_obj_set_style_pad_all(s_savedBox, 2, LV_PART_MAIN);
  lv_obj_set_scroll_dir(s_savedBox, LV_DIR_VER);

  s_ssidTa = mkField(parent, "SSID",     136, false);
  s_passTa = mkField(parent, "password", 166, true);

#if FEAT_WIFI
  rebuildSavedList();

  lv_obj_t* btn = ui_button(parent, "ADD", cb_add, nullptr);
  lv_obj_set_size(btn, 90, 26);
  lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -10, 136);

  // Edit overlay — hidden by default, shown when a field is focused.
  s_editPane = lv_obj_create(parent);
  ui_fill_parent(s_editPane);
  lv_obj_set_size(s_editPane, 320, 80);
  lv_obj_align(s_editPane, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(s_editPane, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_editPane, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_editPane, 1, LV_PART_MAIN);
  lv_obj_set_style_border_side(s_editPane, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_editPane, 4, LV_PART_MAIN);

  s_editHint = ui_label(s_editPane, "ssid", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_editHint, LV_ALIGN_TOP_LEFT, 4, 2);

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

  // Picker pane — full content overlay with title row, status, scrolling
  // list, and a CANCEL button. Hidden until PICK is tapped.
  s_pickPane = lv_obj_create(parent);
  ui_fill_parent(s_pickPane);
  lv_obj_set_size(s_pickPane, 320, 204);
  lv_obj_align(s_pickPane, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(s_pickPane, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_pickPane, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_pickPane, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_pickPane, 0, LV_PART_MAIN);

  ui_label(s_pickPane, "NEARBY", &lv_font_montserrat_20, UI_RED);
  s_pickStatus = ui_label(s_pickPane, "scanning...", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_pickStatus, LV_ALIGN_TOP_LEFT, 10, 28);

  lv_obj_t* cancel = ui_button(s_pickPane, "X", cb_pickCancel, nullptr);
  lv_obj_set_size(cancel, 44, 22);
  lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -8, 2);

  s_pickList = lv_obj_create(s_pickPane);
  lv_obj_set_size(s_pickList, 318, 150);
  lv_obj_align(s_pickList, LV_ALIGN_TOP_LEFT, 1, 50);
  lv_obj_set_style_bg_color(s_pickList, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_pickList, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_pickList, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(s_pickList, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_pickList, 4, LV_PART_MAIN);
  lv_obj_set_scroll_dir(s_pickList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_pickList, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_layout(s_pickList, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_pickList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_pickList, 2, LV_PART_MAIN);
  lv_obj_add_flag(s_pickPane, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_event_cb(parent, cb_destroy, LV_EVENT_DELETE, nullptr);
#endif
}

void tick() {
  if (!s_status) return;
#if FEAT_WIFI
  // While the picker is sniffing, hop channels and redraw its list. STA
  // status is meaningless here (wifi is suspended).
  if (s_pickArmed) {
    uint32_t now = lv_tick_get();
    if (now - s_pickLastHop > 300) {
      s_pickLastHop = now;
      s_pickCh = (s_pickCh % 13) + 1;
      esp_wifi_set_channel(s_pickCh, WIFI_SECOND_CHAN_NONE);
    }
    if (now - s_pickLastDraw > 500) {
      s_pickLastDraw = now;
      char tmp[80];
      snprintf(tmp, sizeof(tmp), "ch %u  aps %u", (unsigned)s_pickCh, (unsigned)s_pickCount);
      lv_label_set_text(s_pickStatus, tmp);

      uint8_t n = s_pickCount;
      uint32_t cc = lv_obj_get_child_cnt(s_pickList);
      while (cc > n) {
        lv_obj_delete(lv_obj_get_child(s_pickList, cc - 1));
        cc--;
      }
      for (uint8_t i = 0; i < n; ++i) {
        PickAp& a = s_pickAps[i];
        snprintf(tmp, sizeof(tmp), "%-4d c%-2u %-4s %s",
                 a.rssi, a.channel, encName(a.enc),
                 a.ssid[0] ? a.ssid : "(hidden)");
        lv_obj_t* row;
        if (i < cc) {
          row = lv_obj_get_child(s_pickList, i);
          lv_obj_t* lbl = lv_obj_get_child(row, 0);
          if (lbl) lv_label_set_text(lbl, tmp);
        } else {
          row = lv_obj_create(s_pickList);
          ui_fill_parent(row);
          lv_obj_set_size(row, 300, 22);
          lv_obj_set_style_border_color(row, lv_color_hex(UI_DIM), LV_PART_MAIN);
          lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
          lv_obj_set_style_pad_all(row, 2, LV_PART_MAIN);
          lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
          lv_obj_t* lbl = ui_label(row, tmp, &lv_font_montserrat_14, UI_FG);
          lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 2, 0);
          lv_obj_add_event_cb(row, cb_pickRow, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
        }
      }
    }
    return;
  }

  static uint32_t last = 0;
  uint32_t now = lv_tick_get();
  if (now - last < 1000) return;
  last = now;
  static char buf[64];
  if (wifi::connected()) {
    snprintf(buf, sizeof(buf), "ok  %s  %d dBm", wifi::ssid(), (int)wifi::rssi());
  } else if (wifi::savedCount()) {
    snprintf(buf, sizeof(buf), "trying %s (%u saved)", wifi::ssid(), wifi::savedCount());
  } else {
    snprintf(buf, sizeof(buf), "no saved networks");
  }
  lv_label_set_text(s_status, buf);
#endif
}

} // namespace screen_wifi
