#include "screen_trackpad.h"
#include "ui_theme.h"
#include "config.h"
#include "../hal/touch.h"
#if FEAT_BLE_HID
#include "../features/ble_hid.h"
#endif
#include <stdio.h>
#include <stdlib.h>

// Gesture state machine. The whole content panel is the touch surface; we
// read from the multi-touch cache populated by touch::read_cb each LVGL
// poll, so we don't double-tap the FT6336G's I2C bus.
namespace {
  lv_obj_t* s_status = nullptr;

  enum class Phase : uint8_t { Idle, Moving, Scrolling };
  Phase    s_phase     = Phase::Idle;
  uint8_t  s_lastN     = 0;          // last poll's finger count
  uint8_t  s_maxN      = 0;          // peak fingers during this touch sequence
  uint32_t s_startMs   = 0;
  uint32_t s_accumPx   = 0;          // movement budget for tap vs drag classification
  int16_t  s_startX    = 0, s_startY = 0;
  int16_t  s_lastX     = 0, s_lastY = 0;   // single-finger reference
  int16_t  s_midStartY = 0;                 // 2-finger midpoint baseline
  int16_t  s_midLastY  = 0;

  // Quadratic acceleration: small deltas pass through ~1:1; bigger deltas
  // scale up so flicks cross the screen quickly without losing fine-grain
  // pointing. Clamp to the int8_t range the HID report uses.
  int8_t accel(int delta) {
    int sgn = delta < 0 ? -1 : 1;
    int a = delta < 0 ? -delta : delta;
    int out = a + (a * a) / 16;
    if (out > 127) out = 127;
    return (int8_t)(sgn * out);
  }

  void resetSession() {
    s_phase   = Phase::Idle;
    s_maxN    = 0;
    s_accumPx = 0;
  }
}

namespace screen_trackpad {

void create(lv_obj_t* parent) {
  s_status = nullptr;
  resetSession();
  ui_fill_parent(parent);
  ui_label(parent, "TRACKPAD", &lv_font_montserrat_20, UI_RED);

  s_status = ui_label(parent, "--", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 12, 28);

  lv_obj_t* hint = ui_label(parent,
    "drag = move\ntap = left click\n2-finger tap = right\n2-finger drag = scroll",
    &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(hint, LV_ALIGN_CENTER, 0, 18);
}

void tick() {
  if (!s_status) return;

#if FEAT_BLE_HID
  // Status line refresh, ~4Hz so it isn't a hot path for every gesture poll.
  static uint32_t lastStatus = 0;
  uint32_t now = lv_tick_get();
  if (now - lastStatus > 250) {
    lastStatus = now;
    lv_label_set_text(s_status, ble_hid::connected() ? "connected" : "not connected");
  }

  // If we aren't paired, eat any pending press so we don't accumulate state.
  if (!ble_hid::connected()) { resetSession(); s_lastN = 0; return; }

  uint16_t xs[2], ys[2];
  uint8_t n = touch::readMulti(xs, ys);

  // --- finger-count transitions ---
  if (n > s_lastN) {
    // New finger landed.
    if (s_lastN == 0) {
      // Start of a touch sequence.
      s_startMs = now;
      s_accumPx = 0;
      s_maxN    = n;
      s_startX  = s_lastX = xs[0];
      s_startY  = s_lastY = ys[0];
      s_phase   = Phase::Idle;
    }
    if (n == 2 && s_maxN < 2) {
      s_maxN     = 2;
      // Baseline midpoint for scroll delta.
      int16_t mx = (xs[0] + xs[1]) / 2;
      int16_t my = (ys[0] + ys[1]) / 2;
      (void)mx;
      s_midStartY = s_midLastY = my;
      s_phase     = Phase::Idle;   // re-decide tap vs scroll
    }
  } else if (n < s_lastN && n == 0) {
    // All fingers lifted — classify the sequence.
    uint32_t dur = now - s_startMs;
    bool wasTap = (dur < 280) && (s_accumPx < 12);
    if (wasTap) {
      if (s_maxN == 1)      ble_hid::mouseClick(0x01);   // left
      else if (s_maxN == 2) ble_hid::mouseClick(0x02);   // right
    }
    resetSession();
  }

  // --- motion handling while at least one finger is down ---
  if (n == 1 && s_maxN == 1) {
    int dx = (int)xs[0] - (int)s_lastX;
    int dy = (int)ys[0] - (int)s_lastY;
    s_lastX = xs[0]; s_lastY = ys[0];
    int absd = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    s_accumPx += (uint32_t)absd;
    // Past the tap threshold, every poll's delta becomes a mouse move.
    if (s_accumPx >= 4 || s_phase == Phase::Moving) {
      s_phase = Phase::Moving;
      if (dx || dy) ble_hid::mouseMove(accel(dx), accel(dy));
    }
  } else if (n == 2) {
    int16_t my = (ys[0] + ys[1]) / 2;
    int dy = (int)my - (int)s_midLastY;
    s_midLastY = my;
    int absd = dy < 0 ? -dy : dy;
    s_accumPx += (uint32_t)absd;
    if (s_accumPx >= 6 || s_phase == Phase::Scrolling) {
      s_phase = Phase::Scrolling;
      // Drag up = wheel up (positive). 4 px / wheel detent feels close to a
      // real mac trackpad in casual testing; tune in screen_trackpad.cpp.
      int wheel = -dy / 4;
      if (wheel > 8) wheel = 8;
      if (wheel < -8) wheel = -8;
      if (wheel) ble_hid::mouseScroll((int8_t)wheel);
    }
  }

  s_lastN = n;
#endif
}

} // namespace screen_trackpad
