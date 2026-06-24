// Screen manager: vertical tileview (drawer above, watchface below) + app
// overlay launcher. Apps open as a full-screen panel on top of the tileview
// with a back-arrow bar; tap back or call closeApp() to dismiss.
#pragma once
#include <stdint.h>

namespace ui {
  enum App : uint8_t {
    APP_NONE = 0,
    APP_REMOTE,
    APP_TRACKPAD,
    APP_PHONE,
    APP_PAIR,
    APP_SPAM,
    APP_WIFI,
    APP_SETTINGS,
    APP_DUCKY,
    APP_MSC,
    APP_FOLDER_BT,
    APP_FOLDER_USB,
    APP_FOLDER_WIFI,
    APP_FOLDER_REMOTE,
    APP_WIFI_AP,
    APP_FOLDER_UTIL,
    APP_FLASHLIGHT,
    APP_STOPWATCH,
    APP_QR,
    APP_FILES,
    APP_USB_TEST,
  };

  void init();
  void tick();
  void openApp(App a);
  // Pop one level off the navigation back-stack. If the stack is empty,
  // closes the overlay entirely. Used by the back-arrow button.
  void goBack();
  void closeApp();

  // Destroy and recreate the watchface tile content. Called by theme::/faces::
  // setCurrent() so users see the new selection live.
  void rebuildFace();
}
