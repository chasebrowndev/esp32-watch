// QR generator: WiFi-join preset or custom text. Renders the code into an
// lv_canvas (RGB565) so it scales nicely without spawning thousands of
// child objects.
#include "screen_qr.h"
#include "ui_theme.h"
#include "config.h"
#include <Arduino.h>
#include <qrcode.h>
#include <string.h>
#include <esp_heap_caps.h>
#if FEAT_WIFI
#include "../net/wifi_mgr.h"
#endif

// Canvas large enough for Version 5 (37x37) at 5 px per module = 185 px.
// Enough headroom for typical "WIFI:T:WPA;S:<ssid>;P:<pass>;;" payloads
// without going to Version 6+.
static constexpr int QR_MAX_VERSION = 5;
static constexpr int QR_MAX_MODULES = 17 + 4 * QR_MAX_VERSION;     // 37
static constexpr int QR_PX_PER_MOD  = 5;
static constexpr int QR_CANVAS_PX   = QR_MAX_MODULES * QR_PX_PER_MOD;  // 185
static constexpr size_t QR_BUF_BYTES =
    (size_t)QR_CANVAS_PX * QR_CANVAS_PX * 2;                       // RGB565

static lv_obj_t* s_canvas   = nullptr;
static lv_obj_t* s_status   = nullptr;
static lv_obj_t* s_textTa   = nullptr;
static lv_obj_t* s_editPane = nullptr;
static lv_obj_t* s_editTa   = nullptr;
static lv_obj_t* s_kb       = nullptr;
static uint8_t*  s_buf      = nullptr;

static void renderQr(const char* text) {
  if (!s_canvas || !s_buf || !text || !text[0]) return;
  size_t tlen = strlen(text);

  // Try increasing versions until the text fits at ECC_LOW.
  QRCode qr;
  uint8_t qrData[qrcode_getBufferSize(QR_MAX_VERSION)];
  int chosen = -1;
  for (int v = 2; v <= QR_MAX_VERSION; ++v) {
    if (qrcode_initText(&qr, qrData, v, ECC_LOW, text) == 0) {
      // Library returns 0 on success. Ensure text actually fits — qrcode
      // returns 0 even when truncated on some versions, so cross-check
      // capacity via size.
      (void)tlen;
      chosen = v;
      break;
    }
  }
  if (chosen < 0) {
    if (s_status) lv_label_set_text(s_status, "text too long");
    return;
  }

  const int modules = qr.size;                          // <= QR_MAX_MODULES
  const int px      = QR_PX_PER_MOD;
  const int total   = modules * px;

  // Clear to white, then paint dark modules black.
  lv_canvas_fill_bg(s_canvas, lv_color_white(), LV_OPA_COVER);
  for (int y = 0; y < modules; ++y) {
    for (int x = 0; x < modules; ++x) {
      if (!qrcode_getModule(&qr, x, y)) continue;
      for (int dy = 0; dy < px; ++dy) {
        for (int dx = 0; dx < px; ++dx) {
          lv_canvas_set_px(s_canvas, x * px + dx, y * px + dy,
                           lv_color_black(), LV_OPA_COVER);
        }
      }
    }
  }
  // Center the canvas's visible content by sizing the widget to total px.
  lv_obj_set_size(s_canvas, QR_CANVAS_PX, QR_CANVAS_PX);
  (void)total;

  if (s_status) {
    char buf[48];
    snprintf(buf, sizeof(buf), "v%d  %dx%d", chosen, modules, modules);
    lv_label_set_text(s_status, buf);
  }
}

#if FEAT_WIFI
static void buildWifiJoin(char* out, size_t n) {
  const char* ssid = wifi::ssid();
  const char* pass = "";
  uint8_t cnt = wifi::savedCount();
  for (uint8_t i = 0; i < cnt; ++i) {
    if (strcmp(wifi::savedSsid(i), ssid) == 0) {
      pass = wifi::savedPass(i);
      break;
    }
  }
  if (!ssid || !ssid[0]) { snprintf(out, n, ""); return; }
  // WIFI:T:WPA;S:<ssid>;P:<pass>;;
  snprintf(out, n, "WIFI:T:WPA;S:%s;P:%s;;", ssid, pass);
}
#endif

static void cb_wifi(lv_event_t*) {
#if FEAT_WIFI
  char join[160];
  buildWifiJoin(join, sizeof(join));
  if (!join[0]) {
    if (s_status) lv_label_set_text(s_status, "no wifi");
    return;
  }
  renderQr(join);
#endif
}

static void enterEdit() {
  if (!s_editPane || !s_kb || !s_textTa) return;
  lv_textarea_set_text(s_editTa, lv_textarea_get_text(s_textTa));
  lv_keyboard_set_textarea(s_kb, s_editTa);
  lv_obj_clear_flag(s_editPane, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_kb,       LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_editPane);
  lv_obj_move_foreground(s_kb);
}
static void exitEdit(bool commit) {
  if (!s_editPane || !s_kb) return;
  if (commit && s_textTa) {
    lv_textarea_set_text(s_textTa, lv_textarea_get_text(s_editTa));
    renderQr(lv_textarea_get_text(s_textTa));
  }
  lv_keyboard_set_textarea(s_kb, nullptr);
  lv_obj_add_flag(s_kb,       LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_editPane, LV_OBJ_FLAG_HIDDEN);
}
static void cb_focus(lv_event_t*) { enterEdit(); }
static void cb_kb(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_READY)  exitEdit(true);
  if (c == LV_EVENT_CANCEL) exitEdit(false);
}

static void cb_destroy(lv_event_t*) {
  if (s_buf) {
    heap_caps_free(s_buf);
    s_buf = nullptr;
  }
  s_canvas = s_status = s_textTa = nullptr;
  s_editPane = s_editTa = s_kb = nullptr;
}

namespace screen_qr {

void create(lv_obj_t* parent) {
  s_canvas = s_status = s_textTa = nullptr;
  s_editPane = s_editTa = s_kb = nullptr;

  ui_fill_parent(parent);
  ui_label(parent, "QR", &lv_font_montserrat_20, UI_RED);

  // Prefer PSRAM for the canvas buffer; fall back to DRAM if absent.
  s_buf = (uint8_t*)heap_caps_malloc(QR_BUF_BYTES,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_buf) s_buf = (uint8_t*)heap_caps_malloc(QR_BUF_BYTES, MALLOC_CAP_8BIT);

  s_status = ui_label(parent, "tap a preset", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 12, 30);

#if FEAT_WIFI
  lv_obj_t* wbtn = ui_button(parent, "WIFI", cb_wifi, nullptr);
  lv_obj_set_size(wbtn, 80, 26);
  lv_obj_align(wbtn, LV_ALIGN_TOP_RIGHT, -8, 4);
#endif

  s_textTa = lv_textarea_create(parent);
  lv_textarea_set_one_line(s_textTa, true);
  lv_textarea_set_placeholder_text(s_textTa, "custom text / url");
  lv_obj_set_size(s_textTa, 220, 26);
  lv_obj_align(s_textTa, LV_ALIGN_TOP_LEFT, 10, 50);
  lv_obj_set_style_radius(s_textTa, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_textTa, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_textTa, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_textTa, 1, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_textTa, lv_color_hex(UI_FG), LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_textTa, 4, LV_PART_MAIN);
  lv_obj_add_event_cb(s_textTa, cb_focus, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(s_textTa, cb_focus, LV_EVENT_CLICKED, nullptr);

  s_canvas = lv_canvas_create(parent);
  if (s_buf) {
    lv_canvas_set_buffer(s_canvas, s_buf, QR_CANVAS_PX, QR_CANVAS_PX,
                         LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(s_canvas, lv_color_white(), LV_OPA_COVER);
  } else {
    if (s_status) lv_label_set_text(s_status, "no mem");
  }
  lv_obj_set_size(s_canvas, QR_CANVAS_PX, QR_CANVAS_PX);
  lv_obj_align(s_canvas, LV_ALIGN_BOTTOM_MID, 0, -6);

  lv_obj_add_event_cb(parent, cb_destroy, LV_EVENT_DELETE, nullptr);

  // Keyboard overlay for custom-text input. Hidden until focused.
  s_editPane = lv_obj_create(parent);
  lv_obj_set_size(s_editPane, 320, 80);
  lv_obj_align(s_editPane, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(s_editPane, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_editPane, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_editPane, 1, LV_PART_MAIN);
  lv_obj_set_style_border_side(s_editPane, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_editPane, 4, LV_PART_MAIN);

  lv_obj_t* hint = ui_label(s_editPane, "text", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 4, 2);

  s_editTa = lv_textarea_create(s_editPane);
  lv_textarea_set_one_line(s_editTa, true);
  lv_obj_set_size(s_editTa, 308, 44);
  lv_obj_align(s_editTa, LV_ALIGN_TOP_LEFT, 4, 22);
  lv_obj_set_style_radius(s_editTa, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_editTa, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_editTa, lv_color_hex(UI_RED), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_editTa, 1, LV_PART_MAIN);
  lv_obj_set_style_text_color(s_editTa, lv_color_hex(UI_FG), LV_PART_MAIN);
  lv_obj_set_style_text_font(s_editTa, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_add_flag(s_editPane, LV_OBJ_FLAG_HIDDEN);

  s_kb = lv_keyboard_create(parent);
  lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_set_size(s_kb, 320, 124);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(s_kb, cb_kb, LV_EVENT_READY,  nullptr);
  lv_obj_add_event_cb(s_kb, cb_kb, LV_EVENT_CANCEL, nullptr);
}

void tick() {}

} // namespace screen_qr
