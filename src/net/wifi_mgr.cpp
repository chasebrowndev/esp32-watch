#include "wifi_mgr.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

namespace {
  struct Net { String ssid, pass; };
  Net      s_nets[WIFI_MAX_SAVED];
  uint8_t  s_count    = 0;
  uint8_t  s_tryIdx   = 0;       // which saved net we're currently attempting
  uint32_t s_lastTry  = 0;
  bool     s_suspended = false;
  const uint32_t RETRY_MS = 8000;

  void keySsid(char* out, uint8_t i) { snprintf(out, 8, "s%u", i); }
  void keyPass(char* out, uint8_t i) { snprintf(out, 8, "p%u", i); }

  // Open the NVS namespace transiently per call so we don't hold an open
  // Preferences handle for the firmware's lifetime (which would block any
  // other module from opening "wifi" in a different mode).
  void load() {
    Preferences p;
    if (!p.begin("wifi", true)) { s_count = 0; return; }
    s_count = p.getUChar("count", 0);
    if (s_count > WIFI_MAX_SAVED) s_count = WIFI_MAX_SAVED;
    char k[8];
    for (uint8_t i = 0; i < s_count; ++i) {
      keySsid(k, i); s_nets[i].ssid = p.getString(k, "");
      keyPass(k, i); s_nets[i].pass = p.getString(k, "");
    }
    p.end();
  }

  void persistOne(uint8_t i) {
    Preferences p;
    if (!p.begin("wifi", false)) return;
    char k[8];
    keySsid(k, i); p.putString(k, s_nets[i].ssid);
    keyPass(k, i); p.putString(k, s_nets[i].pass);
    p.end();
  }

  void persistCount() {
    Preferences p;
    if (!p.begin("wifi", false)) return;
    p.putUChar("count", s_count);
    p.end();
  }

  void removeSlotKeys(uint8_t i) {
    Preferences p;
    if (!p.begin("wifi", false)) return;
    char k[8];
    keySsid(k, i); p.remove(k);
    keyPass(k, i); p.remove(k);
    p.end();
  }

  // First-boot convenience: if NVS has no nets, try /wifi.json on LittleFS.
  void seedFromFsIfEmpty() {
    if (s_count) return;
    if (!LittleFS.begin(true)) return;
    File f = LittleFS.open("/wifi.json", "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
      const char* ss = doc["ssid"]  | "";
      const char* pw = doc["password"] | "";
      if (ss[0]) {
        s_nets[0].ssid = ss;
        s_nets[0].pass = pw;
        s_count = 1;
        persistOne(0);
        persistCount();
      }
    }
    f.close();
  }

  void beginIndex(uint8_t i) {
    if (i >= s_count) return;
    // disconnect(wifioff=false, eraseap=true) wipes the driver's cached AP so
    // auto-reconnect doesn't keep retrying the previous network.
    WiFi.disconnect(false, true);
    delay(20);
    WiFi.begin(s_nets[i].ssid.c_str(), s_nets[i].pass.c_str());
    s_lastTry = millis();
  }
}

namespace wifi {

void init() {
  load();
  seedFromFsIfEmpty();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  // Manage reconnection ourselves in tick() so we can cycle through saved
  // networks. Driver-level auto-reconnect would fight that loop and either
  // thrash between the current and next net or override an explicit
  // beginIndex() request.
  WiFi.setAutoReconnect(false);
  if (s_count) {
    s_tryIdx = 0;
    beginIndex(s_tryIdx);
  }
}

void tick() {
  if (s_suspended) return;
  if (!s_count) return;
  // When connected, remember which slot succeeded so reconnect after a
  // drop restarts there instead of cycling from index 0 again (a flaky
  // earlier slot would otherwise starve the working network).
  if (WiFi.status() == WL_CONNECTED) {
    String cur = WiFi.SSID();
    for (uint8_t i = 0; i < s_count; ++i) {
      if (s_nets[i].ssid == cur) { s_tryIdx = i; break; }
    }
    return;
  }
  if (millis() - s_lastTry < RETRY_MS) return;
  // Cycle to the next saved network and try it.
  s_tryIdx = (s_tryIdx + 1) % s_count;
  beginIndex(s_tryIdx);
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

const char* ssid() {
  if (connected()) {
    static String s; s = WiFi.SSID(); return s.c_str();
  }
  if (s_count && s_tryIdx < s_count) return s_nets[s_tryIdx].ssid.c_str();
  return "";
}

int8_t rssi() { return connected() ? (int8_t)WiFi.RSSI() : 0; }

uint8_t savedCount() { return s_count; }

const char* savedSsid(uint8_t i) {
  return (i < s_count) ? s_nets[i].ssid.c_str() : "";
}

const char* savedPass(uint8_t i) {
  return (i < s_count) ? s_nets[i].pass.c_str() : "";
}

bool addNetwork(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0]) return false;
  // Update if SSID already saved.
  for (uint8_t i = 0; i < s_count; ++i) {
    if (s_nets[i].ssid == ssid) {
      s_nets[i].pass = pass ? pass : "";
      persistOne(i);
      s_tryIdx = i;
      beginIndex(i);
      return true;
    }
  }
  if (s_count >= WIFI_MAX_SAVED) return false;
  s_nets[s_count].ssid = ssid;
  s_nets[s_count].pass = pass ? pass : "";
  persistOne(s_count);
  s_count++;
  persistCount();
  s_tryIdx = s_count - 1;
  beginIndex(s_tryIdx);
  return true;
}

void removeNetwork(uint8_t i) {
  if (i >= s_count) return;
  for (uint8_t j = i; j + 1 < s_count; ++j) s_nets[j] = s_nets[j + 1];
  s_nets[s_count - 1] = Net{};
  s_count--;
  // Rewrite all slots so we don't leave stale keys past the new count.
  for (uint8_t j = 0; j < s_count; ++j) persistOne(j);
  removeSlotKeys(s_count);
  persistCount();
  if (s_tryIdx >= s_count) s_tryIdx = 0;
  if (WiFi.status() != WL_CONNECTED && s_count) beginIndex(s_tryIdx);
}

void scanStart() {
  // Clear any stale result so scanComplete() returns RUNNING immediately
  // even if the previous attempt failed; without this, scanRunning() would
  // briefly report false and the screen would re-kick mid-init.
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false);
}
// Blocking variant. Returns the count or -2 on failure. Useful when async
// scans hang under BLE+OTA coexistence — accepts ~3s of UI freeze in
// exchange for guaranteed completion.
int16_t scanBlocking() {
  WiFi.scanDelete();
  return WiFi.scanNetworks(false, false);
}
bool scanRunning() { return WiFi.scanComplete() == WIFI_SCAN_RUNNING; }
int16_t scanCount() {
  int16_t n = WiFi.scanComplete();
  return n < 0 ? 0 : n;
}
int16_t scanRaw() { return WiFi.scanComplete(); }

static String s_scanSsid;
const char* scanSsid(int16_t i) {
  if (i < 0 || i >= WiFi.scanComplete()) return "";
  s_scanSsid = WiFi.SSID(i);
  return s_scanSsid.c_str();
}
int8_t scanRssi(int16_t i) {
  if (i < 0 || i >= WiFi.scanComplete()) return 0;
  return (int8_t)WiFi.RSSI(i);
}
uint8_t scanAuth(int16_t i) {
  if (i < 0 || i >= WiFi.scanComplete()) return 0;
  return (uint8_t)WiFi.encryptionType(i);
}

void suspend() {
  if (s_suspended) return;
  s_suspended = true;
  // Drop the current association so the next driver caller starts clean,
  // but keep the radio powered on — the scanner stays in STA mode and
  // an immediate WiFi.scanNetworks() would otherwise fail with no radio.
  WiFi.disconnect(false, true);
  delay(100);
}
void resume() {
  if (!s_suspended) return;
  s_suspended = false;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(false);
  if (s_count) beginIndex(s_tryIdx);
}
bool suspended() { return s_suspended; }

} // namespace wifi
