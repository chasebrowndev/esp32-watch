// Phone media remote: large transport row (prev/play/next), volume column,
// home + back. All actions route through BLE HID consumer-control reports.
#include "screen_phone.h"
#include "ui_theme.h"
#include "config.h"
#if FEAT_BLE_HID
#include "../features/ble_hid.h"
#endif

static lv_obj_t* s_status = nullptr;

#if FEAT_BLE_HID
static void cb_prev(lv_event_t*) { ble_hid::prev(); }
static void cb_play(lv_event_t*) { ble_hid::playPause(); }
static void cb_next(lv_event_t*) { ble_hid::next(); }
static void cb_vup (lv_event_t*) { ble_hid::volUp(); }
static void cb_mute(lv_event_t*) { ble_hid::mute(); }
static void cb_vdn (lv_event_t*) { ble_hid::volDown(); }
static void cb_home(lv_event_t*) { ble_hid::home(); }
static void cb_back(lv_event_t*) { ble_hid::back(); }
#endif

namespace screen_phone {

void create(lv_obj_t* parent) {
  s_status = nullptr;
  ui_fill_parent(parent);

  ui_label(parent, "PHONE", &lv_font_montserrat_20, UI_RED);
  s_status = ui_label(parent, "advertising", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 100, 6);

#if FEAT_BLE_HID
  // Transport row (prev / play / next) — large left block.
  const int BW = 60, BH = 60, GAP = 6;
  const int X0 = 12, Y0 = 44;
  auto trans = [&](const char* sym, lv_event_cb_t cb, int col) {
    lv_obj_t* b = ui_button(parent, sym, cb, nullptr);
    lv_obj_set_size(b, BW, BH);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, X0 + col * (BW + GAP), Y0);
    return b;
  };
  trans(LV_SYMBOL_PREV, cb_prev, 0);
  trans(LV_SYMBOL_PLAY, cb_play, 1);
  trans(LV_SYMBOL_NEXT, cb_next, 2);

  // Home + back row beneath transport.
  const int RY = Y0 + BH + 10;
  auto wide = [&](const char* txt, lv_event_cb_t cb, int col) {
    lv_obj_t* b = ui_button(parent, txt, cb, nullptr);
    lv_obj_set_size(b, 91, 44);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, X0 + col * (91 + GAP), RY);
    return b;
  };
  wide(LV_SYMBOL_HOME,  cb_home, 0);
  wide(LV_SYMBOL_LEFT,  cb_back, 1);

  // Volume column on the right.
  const int SX = X0 + 3 * (BW + GAP) + 14;
  auto side = [&](const char* sym, lv_event_cb_t cb, int row) {
    lv_obj_t* b = ui_button(parent, sym, cb, nullptr);
    lv_obj_set_size(b, 88, 44);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, SX, Y0 + row * 50);
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

} // namespace screen_phone
