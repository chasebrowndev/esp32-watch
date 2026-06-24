// Screen manager.
// Layout: vertical tileview with [watchface at (0,0), drawer at (0,1)],
// initial position = watchface. Swipe up → drawer; swipe down → watchface.
// Tapping an app icon in the drawer creates a full-screen overlay panel on
// top of the tileview with a back-arrow bar; the overlay swallows gestures so
// tileview doesn't react. closeApp() destroys the overlay.
#include "ui.h"
#include "ui_theme.h"
#include "ui_faces.h"
#include "config.h"
#include <lvgl.h>

#include "screen_drawer.h"
#include "screen_remote.h"
#include "screen_pair.h"
#include "screen_spam.h"
#include "screen_wifi.h"
#include "screen_settings.h"
#include "screen_ducky.h"
#include "screen_msc.h"
#include "screen_folder.h"
#include "screen_trackpad.h"
#include "screen_phone.h"
#include "screen_wifi_ap.h"
#include "screen_flashlight.h"
#include "screen_stopwatch.h"
#include "screen_qr.h"
#include "screen_files.h"
#include "screen_usb_test.h"

static lv_obj_t* s_tv      = nullptr;
static lv_obj_t* s_faceTile = nullptr;
static lv_obj_t* s_overlay = nullptr;
static ui::App   s_active  = ui::APP_NONE;

// Navigation back-stack. Pushed by openApp() when called while another app
// is already on screen (e.g. tapping a folder entry); popped by goBack().
// Reset on closeApp() so a fresh drawer→app journey starts empty.
static ui::App s_stack[8];
static uint8_t s_stackTop = 0;

// One-shot deferred app switch. Tearing down the current overlay
// synchronously inside an LVGL event handler frees a parent that LVGL still
// has on its dispatch stack, so the swap is scheduled on the next tick.
static ui::App s_pendingNext = ui::APP_NONE;
static bool    s_pendingPush = false;

static void buildOverlay(ui::App a);
static void teardownOverlay();

static void deferred_switch(lv_timer_t* t) {
  ui::App next = s_pendingNext;
  bool    push = s_pendingPush;
  s_pendingNext = ui::APP_NONE;
  if (push && s_active != ui::APP_NONE && s_stackTop < 8) {
    s_stack[s_stackTop++] = s_active;
  }
  teardownOverlay();
  buildOverlay(next);
  lv_timer_delete(t);
}

static void scheduleSwitch(ui::App next, bool push) {
  s_pendingNext = next;
  s_pendingPush = push;
  lv_timer_t* t = lv_timer_create(deferred_switch, 0, nullptr);
  lv_timer_set_repeat_count(t, 1);
}

static void cb_back(lv_event_t*) { ui::goBack(); }

namespace ui {

void rebuildFace() {
  if (!s_faceTile) return;
  // Repaint the screen background in case the new theme changed it.
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_tv, lv_color_hex(UI_BG), LV_PART_MAIN);

  lv_obj_clean(s_faceTile);
  faces::cur().create(s_faceTile);
}

void init() {
  theme::init();
  faces::init();

  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_BG), LV_PART_MAIN);

  s_tv = lv_tileview_create(scr);
  lv_obj_set_style_bg_color(s_tv, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);

  s_faceTile         = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_BOTTOM);
  lv_obj_t* t_drawer = lv_tileview_add_tile(s_tv, 0, 1, LV_DIR_TOP);

  faces::cur().create(s_faceTile);
  screen_drawer::create(t_drawer);

  // Start on the watchface. Drawer is below (row 1) — swipe up to open
  // (LVGL scrolls content up to reveal the lower tile), swipe down to return.
  lv_obj_set_tile_id(s_tv, 0, 0, LV_ANIM_OFF);
}

} // namespace ui

static void buildOverlay(ui::App a) {
  using namespace ui;
  lv_obj_t* scr = lv_screen_active();

  s_overlay = lv_obj_create(scr);
  lv_obj_set_size(s_overlay, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_pos(s_overlay, 0, 0);
  ui_fill_parent(s_overlay);

  // Back bar (40px tall). Tap left chevron to close the app.
  lv_obj_t* bar = lv_obj_create(s_overlay);
  ui_fill_parent(bar);
  lv_obj_set_size(bar, LV_HOR_RES, 36);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);

  lv_obj_t* back = ui_button(bar, LV_SYMBOL_LEFT, cb_back, nullptr);
  lv_obj_set_size(back, 44, 28);
  lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);

  // App content fills the rest.
  lv_obj_t* content = lv_obj_create(s_overlay);
  ui_fill_parent(content);
  lv_obj_set_size(content, LV_HOR_RES, LV_VER_RES - 36);
  lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, 36);

  s_active = a;
  switch (a) {
    case APP_REMOTE:  screen_remote::create(content);  break;
    case APP_TRACKPAD:screen_trackpad::create(content); break;
    case APP_PHONE:   screen_phone::create(content);   break;
    case APP_PAIR:    screen_pair::create(content);    break;
    case APP_SPAM:    screen_spam::create(content);    break;
    case APP_WIFI:    screen_wifi::create(content);    break;
    case APP_SETTINGS:screen_settings::create(content); break;
    case APP_DUCKY:   screen_ducky::create(content);   break;
    case APP_MSC:     screen_msc::create(content);     break;
    case APP_FOLDER_BT: {
      static const screen_folder::Entry bt[] = {
        { LV_SYMBOL_AUDIO,     "REMOTE", APP_FOLDER_REMOTE },
        { LV_SYMBOL_WARNING,   "SPAM",   APP_SPAM          },
        { LV_SYMBOL_BLUETOOTH, "PAIR",   APP_PAIR          },
      };
      screen_folder::create(content, "BLUETOOTH", bt, 3);
      break;
    }
    case APP_FOLDER_REMOTE: {
      static const screen_folder::Entry r[] = {
        { LV_SYMBOL_VIDEO,    "TV",       APP_REMOTE   },
        { LV_SYMBOL_KEYBOARD, "TRACKPAD", APP_TRACKPAD },
        { LV_SYMBOL_CALL,     "PHONE",    APP_PHONE    },
      };
      screen_folder::create(content, "REMOTE", r, 3);
      break;
    }
    case APP_FOLDER_WIFI: {
      static const screen_folder::Entry w[] = {
        { LV_SYMBOL_WIFI,      "SAVED", APP_WIFI    },
        { LV_SYMBOL_UPLOAD,    "AP",    APP_WIFI_AP },
        { LV_SYMBOL_DIRECTORY, "FILES", APP_FILES   },
      };
      screen_folder::create(content, "WIFI", w, 3);
      break;
    }
    case APP_FOLDER_USB: {
      static const screen_folder::Entry usb[] = {
        { LV_SYMBOL_USB,     "DUCKY",   APP_DUCKY    },
        { LV_SYMBOL_SD_CARD, "SD CARD", APP_MSC      },
        { LV_SYMBOL_DOWNLOAD,"INSTALL", APP_USB_TEST },
      };
      screen_folder::create(content, "USB", usb, 3);
      break;
    }
    case APP_USB_TEST: screen_usb_test::create(content); break;
    // tick wired below
    case APP_WIFI_AP: screen_wifi_ap::create(content); break;
    case APP_FLASHLIGHT: screen_flashlight::create(content); break;
    case APP_STOPWATCH:  screen_stopwatch::create(content);  break;
    case APP_QR:         screen_qr::create(content);         break;
    case APP_FILES:      screen_files::create(content);      break;
    case APP_FOLDER_UTIL: {
      static const screen_folder::Entry u[] = {
        { LV_SYMBOL_EYE_OPEN, "LIGHT", APP_FLASHLIGHT },
        { LV_SYMBOL_REFRESH,  "STOP",  APP_STOPWATCH  },
        { LV_SYMBOL_IMAGE,    "QR",    APP_QR         },
      };
      screen_folder::create(content, "UTIL", u, 3);
      break;
    }
    default: break;
  }
}

static void teardownOverlay() {
  if (!s_overlay) return;
  lv_obj_delete(s_overlay);
  s_overlay = nullptr;
  s_active = ui::APP_NONE;
}

namespace ui {

void openApp(App a) {
  // When called while an overlay is already up (e.g. tapping a folder
  // entry), treat it as a navigation push: remember the current app on the
  // back-stack and rebuild for the new one on the next LVGL tick.
  if (s_overlay) {
    scheduleSwitch(a, /*push=*/true);
    return;
  }
  buildOverlay(a);
}

void goBack() {
  if (s_stackTop == 0) {
    closeApp();
    return;
  }
  App prev = s_stack[--s_stackTop];
  if (s_overlay) {
    scheduleSwitch(prev, /*push=*/false);
  } else {
    buildOverlay(prev);
  }
}

void closeApp() {
  s_stackTop = 0;
  teardownOverlay();
}

void tick() {
  faces::cur().tick();
  if (!s_overlay) return;
  switch (s_active) {
    case APP_REMOTE:  screen_remote::tick();  break;
    case APP_TRACKPAD:screen_trackpad::tick(); break;
    case APP_PHONE:   screen_phone::tick();   break;
    case APP_PAIR:    screen_pair::tick();    break;
    case APP_SPAM:    screen_spam::tick();    break;
    case APP_WIFI:    screen_wifi::tick();    break;
    case APP_SETTINGS:screen_settings::tick(); break;
    case APP_DUCKY:   screen_ducky::tick();   break;
    case APP_MSC:     screen_msc::tick();     break;
    case APP_WIFI_AP: screen_wifi_ap::tick(); break;
    case APP_FLASHLIGHT: screen_flashlight::tick(); break;
    case APP_STOPWATCH:  screen_stopwatch::tick();  break;
    case APP_QR:         screen_qr::tick();         break;
    case APP_FILES:      screen_files::tick();      break;
    case APP_USB_TEST:   screen_usb_test::tick();   break;
    default: break;
  }
}

} // namespace ui
