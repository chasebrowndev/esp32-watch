// USB install: read /firmware.bin off the SD card and flash it.
//
// Why this exists instead of OTA-over-Serial: TinyUSB CDC backpressure on the
// host side is unreliable — the kernel reports writes as complete that the
// device hasn't drained, so a long streamed flash overflows somewhere along
// the line. MSC sidesteps the problem entirely: the host writes a file to
// sectors (already buffered by the SD card), and we read it back at flash
// speed with no live timing constraint.
#include "screen_usb_test.h"
#include "ui_theme.h"
#include "../features/sdcard.h"
#include "../features/usb_msc.h"
#include <Arduino.h>
#include <Update.h>
#include <SD_MMC.h>

namespace screen_usb_test {

static lv_obj_t* s_status = nullptr;
static lv_obj_t* s_action = nullptr;
static lv_obj_t* s_action_label = nullptr;
static bool      s_have_file = false;
static uint32_t  s_file_size = 0;

static const char* const FW_PATH = "/firmware.bin";

static void scan() {
  s_have_file = false;
  s_file_size = 0;
  if (usb_msc::enabled()) {
    lv_label_set_text(s_status,
      "USB drive is mounted on host.\n"
      "Eject it from the host first,\n"
      "then return here.");
    if (s_action) lv_obj_add_flag(s_action, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (!sdcard::mounted()) {
    lv_label_set_text(s_status,
      "No SD card mounted.\n\n"
      "Insert a card and tap TRY MOUNT.");
    if (s_action) {
      lv_obj_clear_flag(s_action, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(s_action_label, "TRY MOUNT");
    }
    return;
  }
  File f = SD_MMC.open(FW_PATH, FILE_READ);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    lv_label_set_text(s_status,
      "No firmware.bin on SD.\n\n"
      "Enable USB drive (SD CARD app),\n"
      "copy firmware.bin to root,\n"
      "eject, then come back.");
    if (s_action) lv_obj_add_flag(s_action, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  s_file_size = f.size();
  f.close();
  s_have_file = true;
  lv_label_set_text_fmt(s_status,
    "firmware.bin ready\n"
    "%lu bytes (%lu KB)\n\n"
    "Tap INSTALL to flash and reboot.",
    (unsigned long)s_file_size,
    (unsigned long)((s_file_size + 1023) / 1024));
  if (s_action) {
    lv_obj_clear_flag(s_action, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_action_label, "INSTALL");
  }
}

static void cb_action(lv_event_t*) {
  if (!sdcard::mounted()) {
    lv_label_set_text(s_status, "Mounting...");
    lv_refr_now(nullptr);
    sdcard::init();
    scan();
    return;
  }
  if (!s_have_file) { scan(); return; }

  lv_label_set_text(s_status, "Flashing... do not power off.");
  lv_label_set_text(s_action_label, "...");
  lv_obj_add_state(s_action, LV_STATE_DISABLED);
  lv_refr_now(nullptr);   // paint the status before we go non-responsive

  File f = SD_MMC.open(FW_PATH, FILE_READ);
  if (!f) { lv_label_set_text(s_status, "open failed"); return; }
  uint32_t sz = f.size();
  if (!Update.begin(sz, U_FLASH)) {
    lv_label_set_text_fmt(s_status, "begin err:\n%s", Update.errorString());
    f.close();
    lv_obj_clear_state(s_action, LV_STATE_DISABLED);
    return;
  }
  // writeStream pulls from the file at flash speed. No serial, no host timing.
  size_t written = Update.writeStream(f);
  f.close();
  if (written != sz) {
    lv_label_set_text_fmt(s_status,
      "write %u/%u\n%s", (unsigned)written, (unsigned)sz, Update.errorString());
    Update.abort();
    lv_obj_clear_state(s_action, LV_STATE_DISABLED);
    return;
  }
  if (!Update.end(true)) {
    lv_label_set_text_fmt(s_status, "end err:\n%s", Update.errorString());
    lv_obj_clear_state(s_action, LV_STATE_DISABLED);
    return;
  }
  // Best-effort cleanup so the next session doesn't re-flash the same image.
  SD_MMC.remove(FW_PATH);
  lv_label_set_text(s_status, "OK — rebooting");
  lv_refr_now(nullptr);
  delay(400);
  ESP.restart();
}

void create(lv_obj_t* parent) {
  ui_fill_parent(parent);

  lv_obj_t* title = ui_label(parent, "USB INSTALL",
                             &lv_font_montserrat_20, UI_RED);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

  s_status = ui_label(parent, "", &lv_font_montserrat_14, UI_FG);
  lv_obj_set_width(s_status, LV_HOR_RES - 16);
  lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
  lv_obj_align(s_status, LV_ALIGN_CENTER, 0, -10);

  s_action = ui_button(parent, "RESCAN", cb_action, nullptr);
  lv_obj_set_size(s_action, 140, 44);
  lv_obj_align(s_action, LV_ALIGN_BOTTOM_MID, 0, -10);
  s_action_label = lv_obj_get_child(s_action, 0);

  scan();
}

void tick() {
  // Cheap: re-scan once a second so the screen reacts to host eject /
  // firmware.bin appearing without the user having to back out and re-enter.
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last > 1000) {
    last = now;
    bool was_have = s_have_file;
    scan();
    if (s_have_file != was_have) {
      // label text already updated in scan()
    }
  }
}

} // namespace screen_usb_test
