#include "screen_spam.h"
#include "ui_theme.h"
#include "config.h"
#include <cstdio>
#if FEAT_BLE_SPAM
#include "../features/ble_spam.h"
#endif

static lv_obj_t* s_status  = nullptr;
static lv_obj_t* s_sw      = nullptr;
static lv_obj_t* s_modeLbl = nullptr;

#if FEAT_BLE_SPAM
static const char* MODE_NAMES[] = { "APPLE", "ANDROID", "WINDOWS", "ALL" };

static void cb_mode(lv_event_t* e) {
  ble_spam::Mode m = (ble_spam::Mode)(uintptr_t)lv_event_get_user_data(e);
  if (ble_spam::active()) ble_spam::start(m);
  if (s_modeLbl) lv_label_set_text(s_modeLbl, MODE_NAMES[m]);
  if (s_sw)      lv_obj_set_user_data(s_sw, (void*)(uintptr_t)m);
}
static void cb_sw(lv_event_t* e) {
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
  ble_spam::Mode m = (ble_spam::Mode)(uintptr_t)lv_obj_get_user_data(sw);
  if (lv_obj_has_state(sw, LV_STATE_CHECKED)) ble_spam::start(m);
  else                                        ble_spam::stop();
}
#endif

namespace screen_spam {

void create(lv_obj_t* parent) {
  s_status = s_sw = s_modeLbl = nullptr;
  ui_fill_parent(parent);
  ui_label(parent, "SPAM", &lv_font_montserrat_20, UI_RED);

#if FEAT_BLE_SPAM
  // Content fits in 204px (240 - 36px back bar).
  // Layout: status(y=26) → mode label(y=44) → row1 buttons(y=70) →
  //         row2 buttons(y=112) → RUN+switch(bottom).

  s_status = ui_label(parent, "idle", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 12, 26);

  ble_spam::Mode current = ble_spam::mode();
  s_modeLbl = ui_label(parent, MODE_NAMES[current], &lv_font_montserrat_16, UI_FG);
  lv_obj_align(s_modeLbl, LV_ALIGN_TOP_MID, 0, 44);

  // Row 1: APPLE / ANDROID / WINDOWS.  Row 2: ALL (centered).
  const int H = 34, W1 = 88, GAP1 = 8, W2 = 160;
  const int x0r1 = (LV_HOR_RES - (3*W1 + 2*GAP1)) / 2;
  const int x0r2 = (LV_HOR_RES - W2) / 2;

  for (uint8_t i = 0; i < (uint8_t)ble_spam::MODE_COUNT; ++i) {
    int row = (i < 3) ? 0 : 1;
    int col = (i < 3) ? i : 0;
    int w   = (row == 0) ? W1  : W2;
    int gap = (row == 0) ? GAP1 : 0;
    int x0  = (row == 0) ? x0r1 : x0r2;
    lv_obj_t* b = ui_button(parent, MODE_NAMES[i], cb_mode, (void*)(uintptr_t)i);
    lv_obj_set_size(b, w, H);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x0 + col * (w + gap), 70 + row * 46);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &lv_font_montserrat_14, LV_PART_MAIN);
  }

  lv_obj_t* runLbl = ui_label(parent, "RUN", &lv_font_montserrat_16, UI_RED);
  lv_obj_align(runLbl, LV_ALIGN_BOTTOM_MID, -38, -16);

  s_sw = lv_switch_create(parent);
  lv_obj_set_size(s_sw, 60, 28);
  lv_obj_set_user_data(s_sw, (void*)(uintptr_t)current);
  lv_obj_set_style_bg_color(s_sw, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_sw, lv_color_hex(UI_RED), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_align(s_sw, LV_ALIGN_BOTTOM_MID, 22, -14);
  if (ble_spam::active()) lv_obj_add_state(s_sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(s_sw, cb_sw, LV_EVENT_VALUE_CHANGED, nullptr);
#else
  s_status = ui_label(parent, "BLE spam disabled", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 0);
#endif
}

void tick() {
  if (!s_status) return;
  static uint32_t last = 0;
  uint32_t now = lv_tick_get();
  if (now - last < 1000) return;
  last = now;
#if FEAT_BLE_SPAM
  if (ble_spam::active()) {
    static char buf[40];
    snprintf(buf, sizeof(buf), "SPAMMING tx=%u", (unsigned)ble_spam::txCount());
    lv_label_set_text(s_status, buf);
  } else {
    lv_label_set_text(s_status, "idle");
  }
#endif
}

} // namespace screen_spam
