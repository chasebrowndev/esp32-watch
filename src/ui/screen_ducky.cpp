// USB ducky screen. Lists .txt scripts from /ducky/ on the SD card; tap to run,
// CANCEL to abort. Shows running script name in the status line while active.
// Tapping a script opens a confirm modal so a stray tap can't fire a payload.
#include "screen_ducky.h"
#include "ui_theme.h"
#include "../../include/config.h"
#include "../features/sdcard.h"
#if FEAT_USB_HID
#include "../features/usb_hid.h"
#include <SD_MMC.h>
#endif

static lv_obj_t* s_status = nullptr;
static lv_obj_t* s_cancel = nullptr;
static lv_obj_t* s_modal  = nullptr;
static lv_obj_t* s_modalLbl = nullptr;
static const char* s_pendingPath = nullptr;

#if FEAT_USB_HID

// Cap is the most scripts we'll display. Per-path is sized to fit "/ducky/" +
// any reasonable filename (long undo_persist_* names + ".txt" land near 50).
static constexpr int  DUCKY_CAP = 64;
static constexpr int  PATH_BUF  = 80;
static char s_paths[DUCKY_CAP][PATH_BUF];

static void hideModal() {
  if (s_modal) lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
  s_pendingPath = nullptr;
}
static void cb_modalRun(lv_event_t*) {
  if (s_pendingPath) usb_hid::runScript(s_pendingPath);
  hideModal();
}
static void cb_modalCancel(lv_event_t*) {
  hideModal();
}
static void cb_run(lv_event_t* e) {
  s_pendingPath = (const char*)lv_event_get_user_data(e);
  if (!s_modal || !s_modalLbl) return;
  // Show "Run <name>?" — strip the "/ducky/" prefix and ".txt" suffix.
  const char* base = strrchr(s_pendingPath, '/');
  base = base ? base + 1 : s_pendingPath;
  char name[64];
  size_t bl = strlen(base);
  size_t n = (bl >= 4) ? bl - 4 : bl;
  if (n >= sizeof(name)) n = sizeof(name) - 1;
  memcpy(name, base, n);
  name[n] = '\0';
  char buf[80];
  snprintf(buf, sizeof(buf), "Run\n%s ?", name);
  lv_label_set_text(s_modalLbl, buf);
  lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_modal);
}
static void cb_cancel(lv_event_t*) {
  usb_hid::cancel();
}

static void populateList(lv_obj_t* list) {
  if (!sdcard::mounted()) {
    lv_obj_t* l = ui_label(list, "no sd card", &lv_font_montserrat_14, UI_DIM);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 8);
    return;
  }

  File dir = SD_MMC.open("/ducky");
  if (!dir || !dir.isDirectory()) {
    lv_obj_t* l = ui_label(list, "create /ducky/ on sd", &lv_font_montserrat_14, UI_DIM);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 8);
    return;
  }

  int count = 0;
  File f = dir.openNextFile();
  while (f && count < DUCKY_CAP) {
    if (!f.isDirectory()) {
      // name() returns full VFS path e.g. "/sd/ducky/payload.txt"
      const char* fullname = f.name();
      const char* base     = strrchr(fullname, '/');
      base = base ? base + 1 : fullname;

      size_t bl = strlen(base);
      if (bl >= 4 && strcmp(base + bl - 4, ".txt") == 0) {
        // Store the SD-relative path (no mount point prefix) for SD_MMC.open()
        snprintf(s_paths[count], sizeof(s_paths[count]), "/ducky/%s", base);

        // Button label: filename without extension
        char label[60];
        size_t copy = (bl - 4 < sizeof(label) - 1) ? bl - 4 : sizeof(label) - 1;
        strncpy(label, base, copy);
        label[copy] = '\0';

        lv_obj_t* btn = ui_button(list, label, cb_run, (void*)s_paths[count]);
        lv_obj_set_size(btn, 296, 30);
        lv_obj_set_style_text_font(lv_obj_get_child(btn, 0),
                                   &lv_font_montserrat_14, LV_PART_MAIN);
        ++count;
      }
    }
    f = dir.openNextFile();
  }

  if (count == 0) {
    lv_obj_t* l = ui_label(list, "no .txt files in /ducky", &lv_font_montserrat_14, UI_DIM);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 8);
  }
}
#endif // FEAT_USB_HID

namespace screen_ducky {

void create(lv_obj_t* parent) {
  s_status = s_cancel = s_modal = s_modalLbl = nullptr;
  s_pendingPath = nullptr;
  ui_fill_parent(parent);
  ui_label(parent, "DUCKY", &lv_font_montserrat_20, UI_RED);

  s_status = ui_label(parent, "ready", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 12, 30);

  // Scrollable script list — flex column so items stack automatically.
  lv_obj_t* list = lv_obj_create(parent);
  lv_obj_set_size(list, 318, 142);
  lv_obj_align(list, LV_ALIGN_TOP_LEFT, 1, 52);
  lv_obj_set_style_bg_color(list, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_border_color(list, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_width(list, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(list, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_column(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(list, 4, LV_PART_MAIN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_layout(list, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

#if FEAT_USB_HID
  populateList(list);
#else
  lv_obj_t* l = ui_label(list, "FEAT_USB_HID disabled", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 8);
#endif

  s_cancel = ui_button(parent, "CANCEL",
#if FEAT_USB_HID
    cb_cancel,
#else
    nullptr,
#endif
    nullptr);
  lv_obj_set_size(s_cancel, 100, 30);
  lv_obj_align(s_cancel, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
  lv_obj_add_flag(s_cancel, LV_OBJ_FLAG_HIDDEN);

#if FEAT_USB_HID
  // Confirm modal — centered, hidden by default. Shown when a script is tapped.
  s_modal = lv_obj_create(parent);
  lv_obj_set_size(s_modal, 260, 150);
  lv_obj_align(s_modal, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(s_modal, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_modal, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_modal, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(s_modal, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_modal, 10, LV_PART_MAIN);
  lv_obj_add_flag(s_modal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

  s_modalLbl = ui_label(s_modal, "Run ?", &lv_font_montserrat_14, UI_FG);
  lv_obj_set_width(s_modalLbl, 236);
  lv_label_set_long_mode(s_modalLbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_modalLbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_align(s_modalLbl, LV_ALIGN_TOP_MID, 0, 4);

  lv_obj_t* btnRun = ui_button(s_modal, "RUN", cb_modalRun, nullptr);
  lv_obj_set_size(btnRun, 100, 32);
  lv_obj_align(btnRun, LV_ALIGN_BOTTOM_LEFT, 4, -4);

  lv_obj_t* btnCx = ui_button(s_modal, "CANCEL", cb_modalCancel, nullptr);
  lv_obj_set_size(btnCx, 100, 32);
  lv_obj_align(btnCx, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
#endif
}

void tick() {
  if (!s_status) return;
  static uint32_t last = 0;
  uint32_t now = lv_tick_get();
  if (now - last < 200) return;
  last = now;

#if FEAT_USB_HID
  if (usb_hid::active()) {
    char buf[96];
    unsigned cur = usb_hid::currentLine();
    unsigned tot = usb_hid::totalLines();
    if (tot) snprintf(buf, sizeof(buf), ">> %s (%u/%u)", usb_hid::scriptName(), cur, tot);
    else     snprintf(buf, sizeof(buf), ">> %s", usb_hid::scriptName());
    lv_label_set_text(s_status, buf);
    if (s_cancel) lv_obj_clear_flag(s_cancel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_label_set_text(s_status, "ready");
    if (s_cancel) lv_obj_add_flag(s_cancel, LV_OBJ_FLAG_HIDDEN);
  }
#endif
}

} // namespace screen_ducky
