#include "touch.h"
#include "display.h"
#include <lvgl.h>
#include <Arduino.h>

static uint32_t s_lastActivity = 0;
static uint32_t s_ignoreUntil  = 0;   // suppress reads during this window (post-wake debounce)

// Cached multi-touch state from the most recent poll. The LVGL indev
// callback is the single I2C reader for the FT6336G — having the trackpad
// screen poll independently would race the read_cb's I2C transaction and
// the controller gets flaky under contention on this board.
static uint16_t s_mxs[2] = {0, 0};
static uint16_t s_mys[2] = {0, 0};
static uint8_t  s_mn = 0;

static void read_cb(lv_indev_t*, lv_indev_data_t* data) {
  if (s_ignoreUntil && (int32_t)(millis() - s_ignoreUntil) < 0) {
    data->state = LV_INDEV_STATE_RELEASED;
    s_mn = 0;
    return;
  }
  uint16_t xs[2], ys[2];
  uint8_t n = disp::readTouches(xs, ys, 2);
  s_mn = n;
  for (uint8_t i = 0; i < n; ++i) { s_mxs[i] = xs[i]; s_mys[i] = ys[i]; }
  if (n > 0) {
    data->point.x = (int32_t)xs[0];
    data->point.y = (int32_t)ys[0];
    data->state   = LV_INDEV_STATE_PRESSED;
    s_lastActivity = millis();
#ifdef DEBUG_TOUCH
    static uint32_t s_lastLog = 0;       // throttled coord dump for orientation check
    if (millis() - s_lastLog > 150) { s_lastLog = millis();
      Serial.print(F("[t] ")); Serial.print(xs[0]); Serial.print(','); Serial.println(ys[0]); }
#endif
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

namespace touch {
  void init() {
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);
    s_lastActivity = millis();
  }
  uint32_t lastActivityMs() { return s_lastActivity; }
  void ignoreUntil(uint32_t deadline) { s_ignoreUntil = deadline; }

  uint8_t readMulti(uint16_t* xs, uint16_t* ys) {
    if (!xs || !ys) return 0;
    xs[0] = s_mxs[0]; ys[0] = s_mys[0];
    xs[1] = s_mxs[1]; ys[1] = s_mys[1];
    return s_mn;
  }
}
