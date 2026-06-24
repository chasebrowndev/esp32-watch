// USB mass storage screen. Toggle switch to present the SD card to the connected
// host. While presenting, DUCKY scripts are blocked. Toggling off schedules a
// FATFS remount so the firmware can use the SD card again.
#include "screen_msc.h"
#include "ui_theme.h"
#include "../../include/config.h"
#include "../features/sdcard.h"
#if FEAT_USB_MSC
#include "../features/usb_msc.h"
#endif

static lv_obj_t* s_status = nullptr;
static lv_obj_t* s_sw     = nullptr;

#if FEAT_USB_MSC
static void cb_sw(lv_event_t* e) {
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
  if (lv_obj_has_state(sw, LV_STATE_CHECKED)) usb_msc::enable();
  else                                        usb_msc::disable();
}
#endif

namespace screen_msc {

void create(lv_obj_t* parent) {
  s_status = s_sw = nullptr;
  ui_fill_parent(parent);
  ui_label(parent, "SD CARD", &lv_font_montserrat_20, UI_RED);

  s_status = ui_label(parent, "--", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 12, 30);

  lv_obj_t* swLbl = ui_label(parent, "present to host", &lv_font_montserrat_14, UI_FG);
  lv_obj_align(swLbl, LV_ALIGN_TOP_LEFT, 12, 70);

  s_sw = lv_switch_create(parent);
  lv_obj_set_size(s_sw, 60, 28);
  lv_obj_align(s_sw, LV_ALIGN_TOP_RIGHT, -12, 64);
  lv_obj_set_style_bg_color(s_sw, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_sw, lv_color_hex(UI_RED),   LV_PART_INDICATOR | LV_STATE_CHECKED);

  lv_obj_t* note = ui_label(parent,
    "eject from host\nbefore disabling",
    &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 12, -8);

#if FEAT_USB_MSC
  if (usb_msc::enabled()) lv_obj_add_state(s_sw, LV_STATE_CHECKED);
  if (!sdcard::mounted()) lv_obj_add_state(s_sw, LV_STATE_DISABLED);
  lv_obj_add_event_cb(s_sw, cb_sw, LV_EVENT_VALUE_CHANGED, nullptr);
#else
  lv_obj_add_state(s_sw, LV_STATE_DISABLED);
#endif
}

void tick() {
  if (!s_status) return;
  static uint32_t last = 0;
  uint32_t now = lv_tick_get();
  if (now - last < 300) return;
  last = now;

  if (!sdcard::mounted()) {
    lv_label_set_text(s_status, "no sd card");
    if (s_sw) lv_obj_add_state(s_sw, LV_STATE_DISABLED);
    return;
  }

#if FEAT_USB_MSC
  // SD is mounted: clear any DISABLED state that was set during a prior
  // unmount so the user can toggle again without reopening the screen.
  if (s_sw) lv_obj_clear_state(s_sw, LV_STATE_DISABLED);
  lv_label_set_text(s_status, usb_msc::enabled() ? "PRESENTING" : "ready");
#else
  lv_label_set_text(s_status, "disabled");
#endif
}

} // namespace screen_msc
