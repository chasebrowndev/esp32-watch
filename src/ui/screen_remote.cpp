// BLE HID remote. Layout (TV-style, landscape 320x204):
//   3x3 nav grid on the left:                    side column (right):
//   | PREV | UP    | SKIP  |                     | VOL+ |
//   | LEFT | OK    | RIGHT |                     | MUTE |
//   | BACK | DOWN  | PAUSE |                     | VOL- |
// Arrow keys/OK use the keyboard HID report; media + back use consumer control.
#include "screen_remote.h"
#include "ui_theme.h"
#include "config.h"
#if FEAT_BLE_HID
#include "../features/ble_hid.h"
#endif

static lv_obj_t* s_status = nullptr;

#if FEAT_BLE_HID
static void cb_back(lv_event_t*) { ble_hid::back(); }
static void cb_up  (lv_event_t*) { ble_hid::up(); }
static void cb_skip(lv_event_t*) { ble_hid::next(); }
static void cb_left(lv_event_t*) { ble_hid::left(); }
static void cb_ok  (lv_event_t*) { ble_hid::select(); }
static void cb_rt  (lv_event_t*) { ble_hid::right(); }
static void cb_mute(lv_event_t*) { ble_hid::mute(); }
static void cb_prev(lv_event_t*) { ble_hid::prev(); }
static void cb_down(lv_event_t*) { ble_hid::down(); }
static void cb_play(lv_event_t*) { ble_hid::playPause(); }
static void cb_vup (lv_event_t*) { ble_hid::volUp(); }
static void cb_vdn (lv_event_t*) { ble_hid::volDown(); }
#endif

namespace screen_remote {

void create(lv_obj_t* parent) {
  s_status = nullptr;
  ui_fill_parent(parent);

  ui_label(parent, "REMOTE", &lv_font_montserrat_20, UI_RED);
  s_status = ui_label(parent, "advertising", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 100, 6);

#if FEAT_BLE_HID
  // 3x3 nav grid: 56x44 buttons, 4px gaps, origin (8, 36).
  const int BW = 56, BH = 44, GAP = 4;
  const int X0 = 8, Y0 = 36;
  auto cell = [&](const char* sym, lv_event_cb_t cb, int col, int row) {
    lv_obj_t* b = ui_button(parent, sym, cb, nullptr);
    lv_obj_set_size(b, BW, BH);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT,
                 X0 + col * (BW + GAP), Y0 + row * (BH + GAP));
    return b;
  };
  cell(LV_SYMBOL_PREV,  cb_prev, 0, 0);   // previous track
  cell(LV_SYMBOL_UP,    cb_up,   1, 0);
  cell(LV_SYMBOL_NEXT,  cb_skip, 2, 0);   // SKIP forward
  cell(LV_SYMBOL_LEFT,  cb_left, 0, 1);
  cell(LV_SYMBOL_OK,    cb_ok,   1, 1);
  cell(LV_SYMBOL_RIGHT, cb_rt,   2, 1);
  cell(LV_SYMBOL_LEFT,  cb_back, 0, 2);   // BACK
  cell(LV_SYMBOL_DOWN,  cb_down, 1, 2);
  cell(LV_SYMBOL_PLAY,  cb_play, 2, 2);   // PAUSE / play

  // Right side column (volume).
  const int SX = 8 + 3 * (BW + GAP) + 12;   // ~204
  auto side = [&](const char* sym, lv_event_cb_t cb, int row) {
    lv_obj_t* b = ui_button(parent, sym, cb, nullptr);
    lv_obj_set_size(b, 96, BH);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, SX, Y0 + row * (BH + GAP));
    return b;
  };
  side(LV_SYMBOL_VOLUME_MAX, cb_vup,  0);
  side(LV_SYMBOL_MUTE,       cb_mute, 1);
  side(LV_SYMBOL_VOLUME_MID, cb_vdn,  2);
#endif
}

void tick() {
  if (!s_status) return;
  static uint32_t last = 0;
  uint32_t now = lv_tick_get();
  if (now - last < 1000) return;
  last = now;
#if FEAT_BLE_HID
  lv_label_set_text(s_status, ble_hid::connected() ? "paired" : "advertising");
#endif
}

} // namespace screen_remote
