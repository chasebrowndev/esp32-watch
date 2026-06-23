#include "display.h"
#include "lgfx_board.h"
#include <lvgl.h>
#include <Wire.h>
#include "board_pins.h"

static LGFX_Hosyond  gfx;
static uint8_t       s_bl = 0;

// Landscape: physical panel is 240x320, rotation 1 → effective 320x240.
static const uint32_t DISP_W = LCD_HEIGHT;   // 320
static const uint32_t DISP_H = LCD_WIDTH;    // 240

// Partial-render buffers: 1/10 screen each, double-buffered. RGB565 = 2 B/px,
// ~15 KB per buffer — fits in internal RAM even without PSRAM (silicon-safe).
static const uint32_t BUF_LINES = DISP_H / 10;
static const uint32_t BUF_SZ    = DISP_W * BUF_LINES * 2;
static uint8_t buf0[BUF_SZ];
static uint8_t buf1[BUF_SZ];

static void flush_cb(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  gfx.startWrite();
  gfx.setAddrWindow(area->x1, area->y1, w, h);
  gfx.writePixels(reinterpret_cast<uint16_t*>(px), w * h, true);
  gfx.endWrite();
  lv_display_flush_ready(d);
}

namespace disp {

static void lv_log_cb(lv_log_level_t, const char* buf) {
  Serial.print(F("[lvgl] ")); Serial.print(buf); Serial.flush();
}

// One-shot raw probe of the FT6336G over Arduino Wire (before LovyanGFX claims
// the bus). Confirms the controller answers and is the expected silicon.
static void probe_touch() {
  Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);  delay(5);
  digitalWrite(PIN_TP_RST, HIGH); delay(50);
  auto rd = [](uint8_t reg, uint8_t& out) -> bool {
    Wire.beginTransmission(TP_ADDR_FT6336);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)TP_ADDR_FT6336, 1) != 1) return false;
    out = Wire.read();
    return true;
  };
  uint8_t chip = 0, vend = 0, npts = 0;
  bool ok_c = rd(0xA3, chip), ok_v = rd(0xA8, vend), ok_n = rd(0x02, npts);
  Serial.printf("[tp] chipID(0xA3)=0x%02X[%d] vendID(0xA8)=0x%02X[%d] pts(0x02)=0x%02X[%d]\n",
                chip, ok_c, vend, ok_v, npts, ok_n);
  Serial.flush();
  Wire.end();
}

void init() {
  lv_log_register_print_cb(lv_log_cb);
  probe_touch();
  gfx.init();
  gfx.setRotation(1);          // landscape 320x240
  gfx.fillScreen(0x0000);
  s_bl = 255;
  gfx.setBrightness(s_bl);

  lv_init();
  // LVGL 9 removed LV_TICK_CUSTOM — without a tick source, all lv_timers
  // (including the indev read poll) silently never fire.
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  lv_display_t* d = lv_display_create(DISP_W, DISP_H);
  lv_display_set_flush_cb(d, flush_cb);

  // Use external draw-buf structs + the low-level setter. The one-shot
  // lv_display_set_buffers() writes into disp->_static_buf{1,2} and hangs on
  // this build; this path is equivalent and verified working on hardware.
  static lv_draw_buf_t dbuf0, dbuf1;
  lv_color_format_t cf = lv_display_get_color_format(d);
  uint32_t stride = lv_draw_buf_width_to_stride(DISP_W, cf);
  uint32_t bh = BUF_SZ / stride;
  lv_draw_buf_init(&dbuf0, DISP_W, bh, cf, stride, buf0, BUF_SZ);
  lv_draw_buf_init(&dbuf1, DISP_W, bh, cf, stride, buf1, BUF_SZ);
  lv_display_set_draw_buffers(d, &dbuf0, &dbuf1);
  lv_display_set_render_mode(d, LV_DISPLAY_RENDER_MODE_PARTIAL);
}

bool readTouch(int16_t& x, int16_t& y) {
  uint16_t tx, ty;
  if (gfx.getTouch(&tx, &ty)) { x = tx; y = ty; return true; }
  return false;
}

uint8_t readTouches(uint16_t* xs, uint16_t* ys, uint8_t cap) {
  if (cap == 0) return 0;
  lgfx::touch_point_t tp[2];
  uint8_t want = cap > 2 ? 2 : cap;
  int n = gfx.getTouch(tp, want);
  if (n < 0) n = 0;
  if ((uint8_t)n > want) n = want;
  for (int i = 0; i < n; ++i) { xs[i] = tp[i].x; ys[i] = tp[i].y; }
  return (uint8_t)n;
}

void setBacklight(uint8_t level) { s_bl = level; gfx.setBrightness(level); }
uint8_t backlight() { return s_bl; }

void sleepPanel() {
  gfx.setBrightness(0);
  gfx.sleep();
}
void wakePanel() {
  gfx.wakeup();
  gfx.setBrightness(s_bl ? s_bl : 255);
}

} // namespace disp
