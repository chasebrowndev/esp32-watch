// File-server screen. Brings up the AP + HTTP via files_http on open; tears
// down on close. Displays SSID / password / IP / client count + a small
// activity log + an on-screen QR code so a phone can join in one tap.
#include "screen_files.h"
#include "ui_theme.h"
#include "../../include/config.h"
#include <Arduino.h>

#if FEAT_WIFI
#include "../net/files_http.h"
#include "../net/wifi_mgr.h"
#include "../net/captive.h"
#include "../features/sdcard.h"
#include <WiFi.h>
#include <Preferences.h>
#include <esp_random.h>
#include <qrcode.h>
#endif

static lv_obj_t* s_ssidLbl  = nullptr;
static lv_obj_t* s_passLbl  = nullptr;
static lv_obj_t* s_ipLbl    = nullptr;
static lv_obj_t* s_cliLbl   = nullptr;
static lv_obj_t* s_log      = nullptr;
static lv_obj_t* s_canvas   = nullptr;
static lv_obj_t* s_regenBtn = nullptr;
static uint8_t*  s_qrBuf    = nullptr;
static char      s_ssid[24] = {0};
static char      s_pass[16] = {0};

#if FEAT_WIFI

// Canvas sized for a Version-3 QR (29x29 modules) at 3 px/mod = 87 px.
// Plenty of room for "WIFI:T:WPA;S:<ssid>;P:<pass>;;" payloads at this length.
static constexpr int QR_VERSION  = 3;
static constexpr int QR_MOD_MAX  = 17 + 4 * QR_VERSION;     // 29
static constexpr int QR_PX_PER   = 3;
static constexpr int QR_CANVAS_PX = QR_MOD_MAX * QR_PX_PER; // 87

static void genPassword(char* out, size_t n) {
  // 8-char alphanum, lowercase. Avoid '1/l/0/o' for legibility.
  const char* alphabet = "abcdefghijkmnpqrstuvwxyz23456789";
  size_t alen = strlen(alphabet);
  size_t want = (n > 9 ? 8 : (n - 1));
  for (size_t i = 0; i < want; ++i) out[i] = alphabet[esp_random() % alen];
  out[want] = 0;
}

static void loadOrGenPassword() {
  Preferences p;
  if (p.begin("apfiles", true)) {
    p.getString("pass", s_pass, sizeof(s_pass));
    p.end();
  }
  if (strlen(s_pass) < 8) {
    genPassword(s_pass, sizeof(s_pass));
    if (p.begin("apfiles", false)) {
      p.putString("pass", s_pass);
      p.end();
    }
  }
}

static void buildSsid() {
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  snprintf(s_ssid, sizeof(s_ssid), "watch-files-%02X%02X",
           mac[4], mac[5]);
}

static void renderJoinQr() {
  if (!s_canvas || !s_qrBuf) return;
  char join[160];
  snprintf(join, sizeof(join), "WIFI:T:WPA;S:%s;P:%s;;", s_ssid, s_pass);

  QRCode qr;
  uint8_t qrData[qrcode_getBufferSize(QR_VERSION)];
  if (qrcode_initText(&qr, qrData, QR_VERSION, ECC_LOW, join) != 0) return;

  lv_canvas_fill_bg(s_canvas, lv_color_white(), LV_OPA_COVER);
  const int mods = qr.size;
  const int px   = QR_PX_PER;
  for (int y = 0; y < mods; ++y) {
    for (int x = 0; x < mods; ++x) {
      if (!qrcode_getModule(&qr, x, y)) continue;
      for (int dy = 0; dy < px; ++dy) {
        for (int dx = 0; dx < px; ++dx) {
          lv_canvas_set_px(s_canvas, x * px + dx, y * px + dy,
                           lv_color_black(), LV_OPA_COVER);
        }
      }
    }
  }
}

static void startServer() {
  if (files_http::running()) return;
  files_http::start(s_ssid, s_pass);
  if (s_ipLbl) lv_label_set_text_fmt(s_ipLbl, "ip: %s", WiFi.softAPIP().toString().c_str());
}

static void cb_regen(lv_event_t*) {
  // Generate a new password, persist, restart the AP with new creds, redraw QR.
  genPassword(s_pass, sizeof(s_pass));
  Preferences p;
  if (p.begin("apfiles", false)) { p.putString("pass", s_pass); p.end(); }
  files_http::stop();
  startServer();
  if (s_passLbl) lv_label_set_text_fmt(s_passLbl, "pass: %s", s_pass);
  renderJoinQr();
}

#endif // FEAT_WIFI

static void cb_destroy(lv_event_t*) {
#if FEAT_WIFI
  files_http::stop();
  wifi::resume();
#endif
  if (s_qrBuf) { free(s_qrBuf); s_qrBuf = nullptr; }
  s_ssidLbl = s_passLbl = s_ipLbl = s_cliLbl = nullptr;
  s_log = s_canvas = s_regenBtn = nullptr;
  s_ssid[0] = s_pass[0] = 0;
}

namespace screen_files {

void create(lv_obj_t* parent) {
  s_ssidLbl = s_passLbl = s_ipLbl = s_cliLbl = nullptr;
  s_log = s_canvas = s_regenBtn = nullptr;
  ui_fill_parent(parent);
  ui_label(parent, "FILES", &lv_font_montserrat_20, UI_RED);

#if FEAT_WIFI
  // Prep AP creds. Yield STA so AP comes up cleanly, and also defensively
  // stop captive in case wifi_ap was running.
  buildSsid();
  loadOrGenPassword();
  wifi::suspend();
  captive::stop();

  s_qrBuf = (uint8_t*)malloc((size_t)QR_CANVAS_PX * QR_CANVAS_PX * 2);
  s_canvas = lv_canvas_create(parent);
  if (s_qrBuf) {
    lv_canvas_set_buffer(s_canvas, s_qrBuf, QR_CANVAS_PX, QR_CANVAS_PX,
                         LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(s_canvas, lv_color_white(), LV_OPA_COVER);
  }
  lv_obj_set_size(s_canvas, QR_CANVAS_PX, QR_CANVAS_PX);
  lv_obj_align(s_canvas, LV_ALIGN_TOP_RIGHT, -8, 4);

  s_ssidLbl = ui_label(parent, "ssid: ...", &lv_font_montserrat_14, UI_FG);
  lv_obj_align(s_ssidLbl, LV_ALIGN_TOP_LEFT, 12, 30);
  lv_label_set_text_fmt(s_ssidLbl, "ssid: %s", s_ssid);

  s_passLbl = ui_label(parent, "pass: ...", &lv_font_montserrat_14, UI_FG);
  lv_obj_align(s_passLbl, LV_ALIGN_TOP_LEFT, 12, 48);
  lv_label_set_text_fmt(s_passLbl, "pass: %s", s_pass);

  s_regenBtn = ui_button(parent, "REGEN", cb_regen, nullptr);
  lv_obj_set_size(s_regenBtn, 70, 22);
  lv_obj_align(s_regenBtn, LV_ALIGN_TOP_LEFT, 12, 68);

  s_ipLbl = ui_label(parent, "ip: ...", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_ipLbl, LV_ALIGN_TOP_LEFT, 12, 96);

  s_cliLbl = ui_label(parent, "clients: 0", &lv_font_montserrat_14, UI_DIM);
  lv_obj_align(s_cliLbl, LV_ALIGN_TOP_LEFT, 12, 114);

  s_log = lv_obj_create(parent);
  lv_obj_set_size(s_log, 318, 70);
  lv_obj_align(s_log, LV_ALIGN_TOP_LEFT, 1, 134);
  lv_obj_set_style_bg_color(s_log, lv_color_hex(UI_BG), LV_PART_MAIN);
  lv_obj_set_style_border_color(s_log, lv_color_hex(UI_PANEL), LV_PART_MAIN);
  lv_obj_set_style_border_width(s_log, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(s_log, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_log, 4, LV_PART_MAIN);
  lv_obj_set_scroll_dir(s_log, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_log, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_layout(s_log, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_log, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_log, 1, LV_PART_MAIN);

  startServer();
  renderJoinQr();
#else
  ui_label(parent, "FEAT_WIFI disabled", &lv_font_montserrat_14, UI_DIM);
#endif

  lv_obj_add_event_cb(parent, cb_destroy, LV_EVENT_DELETE, nullptr);
}

void tick() {
#if FEAT_WIFI
  if (!s_cliLbl || !s_log) return;
  static uint32_t last = 0;
  uint32_t now = lv_tick_get();
  if (now - last < 500) return;
  last = now;

  lv_label_set_text_fmt(s_cliLbl, "clients: %u", (unsigned)files_http::clientCount());

  files_http::Hit hits[10];
  uint8_t n = files_http::snapshot(hits, 10);
  uint32_t childCount = lv_obj_get_child_cnt(s_log);
  while (childCount > n) {
    lv_obj_delete(lv_obj_get_child(s_log, childCount - 1));
    childCount--;
  }
  for (uint8_t i = 0; i < n; ++i) {
    char tmp[80];
    snprintf(tmp, sizeof(tmp), "%6lus  %s  %s",
             (unsigned long)(hits[i].ms / 1000), hits[i].op, hits[i].path);
    if (i < childCount) {
      lv_label_set_text(lv_obj_get_child(s_log, i), tmp);
    } else {
      lv_obj_t* row = ui_label(s_log, tmp, &lv_font_montserrat_14, UI_FG);
      lv_obj_set_width(row, 300);
    }
  }
#endif
}

} // namespace screen_files
