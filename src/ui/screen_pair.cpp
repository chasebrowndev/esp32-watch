// BLE pair manager: shows connection state and a bonded peer SELECTOR.
// Tapping a row makes that peer the only one the watch will advertise to /
// accept (via NimBLE whitelist), so the user can pick "talk to TV" vs
// "talk to phone". Per-row edit (rename) + trash, plus Forget All + Pair New
// (clear selection + advertise to anyone) at the right. Rebuilt on open.
#include "screen_pair.h"
#include "ui_theme.h"
#include "config.h"
#if FEAT_BLE_HID
#include "../features/ble_hid.h"
#endif
#include <string.h>
#include <stdio.h>

static lv_obj_t* s_status   = nullptr;
static lv_obj_t* s_list     = nullptr;
static lv_obj_t* s_renamePg = nullptr;   // modal page (textarea + keyboard)
static lv_obj_t* s_renameTa = nullptr;
static lv_obj_t* s_exitBtn  = nullptr;   // shown only while in pair-new mode
static uint8_t   s_renameIx = 0;

#if FEAT_BLE_HID
static void rebuild();
static void openRename(uint8_t i);
static void closeRename();

static void cb_select(lv_event_t* e) {
  uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  if (ble_hid::isSelected(i)) ble_hid::deselectBond(i);
  else                        ble_hid::selectBond(i);
  rebuild();
}
static void cb_forget(lv_event_t* e) {
  uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  ble_hid::forget(i);
  rebuild();
}
static void cb_edit(lv_event_t* e) {
  uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  openRename(i);
}
static void cb_kb(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_READY) {
    ble_hid::setBondName(s_renameIx, lv_textarea_get_text(s_renameTa));
    closeRename();
    rebuild();
  } else if (c == LV_EVENT_CANCEL) {
    closeRename();
  }
}
static void cb_forgetAll(lv_event_t*) {
  ble_hid::forgetAll();
  rebuild();
}
static void cb_exitPairNew(lv_event_t*) {
  ble_hid::exitPairNewMode();
  rebuild();
}
static void cb_newDevice(lv_event_t*) {
  // Open the door for a fresh pairing: clear selection so a new peer can
  // discover us (whitelist would otherwise block unknown devices), and
  // re-advertise.
  ble_hid::clearSelection();
  ble_hid::stopAdvertising();
  ble_hid::startAdvertising();
  rebuild();
}

static void rebuild() {
  if (!s_list) return;
  lv_obj_clean(s_list);
  uint8_t n = ble_hid::numBonds();
  if (n == 0) {
    lv_obj_t* l = ui_label(s_list, "no bonded devices", &lv_font_montserrat_14, UI_DIM);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 4, 4);
    return;
  }
  for (uint8_t i = 0; i < n; ++i) {
    char addr[24], name[24], label[40];
    strncpy(addr, ble_hid::bondAddr(i), sizeof(addr) - 1); addr[sizeof(addr) - 1] = 0;
    strncpy(name, ble_hid::bondName(i), sizeof(name) - 1); name[sizeof(name) - 1] = 0;
    if (name[0]) {
      const char* tail = strlen(addr) > 5 ? addr + strlen(addr) - 5 : addr;
      snprintf(label, sizeof(label), "%s  %s", name, tail);
    } else {
      strncpy(label, addr, sizeof(label) - 1); label[sizeof(label) - 1] = 0;
    }

    bool sel = ble_hid::isSelected(i);
    lv_obj_t* row = lv_obj_create(s_list);
    ui_fill_parent(row);
    lv_obj_set_size(row, 192, 32);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, i * 36);
    if (sel) {
      lv_obj_set_style_border_color(row, lv_color_hex(UI_RED), LV_PART_MAIN);
      lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    }
    // Tap row -> toggle selection. The edit/trash buttons stop event
    // propagation on click, so they don't trigger this.
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cb_select, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

    lv_obj_t* mark = ui_label(row, sel ? LV_SYMBOL_OK : " ",
                              &lv_font_montserrat_14, sel ? UI_RED : UI_DIM);
    lv_obj_align(mark, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t* lbl = ui_label(row, label, &lv_font_montserrat_14, UI_FG);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 22, 0);
    lv_obj_t* eb = ui_button(row, LV_SYMBOL_EDIT, cb_edit, (void*)(uintptr_t)i);
    lv_obj_set_size(eb, 30, 26);
    lv_obj_align(eb, LV_ALIGN_RIGHT_MID, -38, 0);
    lv_obj_t* fb = ui_button(row, LV_SYMBOL_TRASH, cb_forget, (void*)(uintptr_t)i);
    lv_obj_set_size(fb, 30, 26);
    lv_obj_align(fb, LV_ALIGN_RIGHT_MID, -4, 0);
  }
}

static void openRename(uint8_t i) {
  s_renameIx = i;
  lv_obj_t* parent = lv_obj_get_parent(s_list);
  if (s_renamePg) lv_obj_delete(s_renamePg);
  s_renamePg = lv_obj_create(parent);
  lv_obj_set_size(s_renamePg, 320, 240);
  lv_obj_align(s_renamePg, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(s_renamePg, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_renamePg, LV_OPA_COVER, LV_PART_MAIN);

  s_renameTa = lv_textarea_create(s_renamePg);
  lv_textarea_set_one_line(s_renameTa, true);
  lv_textarea_set_max_length(s_renameTa, 19);
  lv_textarea_set_text(s_renameTa, ble_hid::bondName(i));
  lv_obj_set_size(s_renameTa, 296, 36);
  lv_obj_align(s_renameTa, LV_ALIGN_TOP_LEFT, 12, 8);

  lv_obj_t* kb = lv_keyboard_create(s_renamePg);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_set_size(kb, 320, 150);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, s_renameTa);
  lv_obj_add_event_cb(kb, cb_kb, LV_EVENT_READY,  nullptr);
  lv_obj_add_event_cb(kb, cb_kb, LV_EVENT_CANCEL, nullptr);
}

static void closeRename() {
  if (!s_renamePg) return;
  lv_obj_delete(s_renamePg);
  s_renamePg = nullptr;
  s_renameTa = nullptr;
}
#endif

namespace screen_pair {

void create(lv_obj_t* parent) {
  // Previous overlay was destroyed by LVGL. s_status/s_list get re-assigned
  // below, but the rename modal pointers won't be touched unless the user
  // opens rename again — null them so closeRename()'s guard is correct.
  s_status = s_list = s_renamePg = s_renameTa = s_exitBtn = nullptr;
  ui_fill_parent(parent);
  ui_label(parent, "PAIR", &lv_font_montserrat_20, UI_RED);
#if FEAT_BLE_HID
  // Watch's own BD addr — beside the title so it doesn't push the status
  // line into the list box below.
  static char addrBuf[24];
  snprintf(addrBuf, sizeof(addrBuf), "%s", ble_hid::localAddr());
  lv_obj_t* selfLbl = ui_label(parent, addrBuf, &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(selfLbl, LV_ALIGN_TOP_LEFT, 70, 6);
#endif
  s_status = ui_label(parent, "--", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 12, 30);

  s_list = lv_obj_create(parent);
  ui_fill_parent(s_list);
  lv_obj_set_size(s_list, 200, 150);
  lv_obj_align(s_list, LV_ALIGN_TOP_LEFT, 8, 50);
  lv_obj_set_style_border_color(s_list, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_list, 1, LV_PART_MAIN);
  lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

#if FEAT_BLE_HID
  rebuild();

  lv_obj_t* fa = ui_button(parent, "FORGET", cb_forgetAll, nullptr);
  lv_obj_set_size(fa, 100, 44);
  lv_obj_align(fa, LV_ALIGN_TOP_RIGHT, -8, 50);
  lv_obj_set_style_text_font(lv_obj_get_child(fa, 0), &lv_font_montserrat_14, LV_PART_MAIN);

  lv_obj_t* nd = ui_button(parent, "PAIR NEW", cb_newDevice, nullptr);
  lv_obj_set_size(nd, 100, 44);
  lv_obj_align(nd, LV_ALIGN_TOP_RIGHT, -8, 102);
  lv_obj_set_style_text_font(lv_obj_get_child(nd, 0), &lv_font_montserrat_14, LV_PART_MAIN);

  // Exit-pair-new lives just below PAIR NEW. Hidden by default; tick() shows
  // it whenever every existing bond is blacklisted.
  s_exitBtn = ui_button(parent, "EXIT", cb_exitPairNew, nullptr);
  lv_obj_set_size(s_exitBtn, 100, 44);
  lv_obj_align(s_exitBtn, LV_ALIGN_TOP_RIGHT, -8, 154);
  lv_obj_set_style_text_font(lv_obj_get_child(s_exitBtn, 0), &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_add_flag(s_exitBtn, LV_OBJ_FLAG_HIDDEN);
#endif
}

void tick() {
  if (!s_status) return;
  static uint32_t last = 0;
  uint32_t now = lv_tick_get();
  if (now - last < 1000) return;
  last = now;
#if FEAT_BLE_HID
  if (s_exitBtn) {
    bool inMode = ble_hid::pairNewMode();
    bool hidden = lv_obj_has_flag(s_exitBtn, LV_OBJ_FLAG_HIDDEN);
    if (inMode && hidden)       lv_obj_clear_flag(s_exitBtn, LV_OBJ_FLAG_HIDDEN);
    else if (!inMode && !hidden) lv_obj_add_flag(s_exitBtn, LV_OBJ_FLAG_HIDDEN);
  }
  bool pairNew = ble_hid::pairNewMode();
  if (ble_hid::connected() && (pairNew || !ble_hid::peerBlacklisted())) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "paired: %s", ble_hid::peerAddr());
    lv_label_set_text(s_status, buf);
  } else if (ble_hid::peerBlacklisted()) {
    lv_label_set_text(s_status, "kicking blacklisted peer...");
  } else {
    uint8_t n = ble_hid::numBonds(), allowed = 0;
    for (uint8_t i = 0; i < n; ++i) if (ble_hid::isSelected(i)) allowed++;
    static char buf[64];
    if (n == 0)              lv_label_set_text(s_status, "advertising (no bonds)");
    else if (allowed == 0)   lv_label_set_text(s_status, "pair-new mode: all bonds blocked");
    else {
      snprintf(buf, sizeof(buf), "advertising (%u/%u allowed)", allowed, n);
      lv_label_set_text(s_status, buf);
    }
  }
#endif
}

} // namespace screen_pair
