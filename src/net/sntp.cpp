#include "sntp.h"
#include "wifi_mgr.h"
#include "config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include <stdio.h>

namespace {
  bool s_configured = false;
  bool s_wasConnected = false;
  bool s_dst = false;

  // Build the TZ string handed to newlib. When the runtime DST toggle is on
  // we use TZ_STRING from config.h as-is so newlib applies the embedded
  // start/end rule. When off, we strip the DST half so the clock stays on
  // permanent standard time. The previous `<NAME>offset` angle-bracket form
  // was off by one hour on this newlib build (offset parsed without an
  // explicit sign), which is what produced the "1 h behind w/ DST on, 2 h
  // behind w/ DST off" symptom.
  const char* tzString() {
    static char s_buf[40];
    if (s_dst) {
      snprintf(s_buf, sizeof(s_buf), "%s", TZ_STRING);
      return s_buf;
    }
    // Copy only the leading "STDoffset" portion (everything up to the DST
    // abbreviation that follows the digits). For "CST6CDT,..." that yields
    // "CST6", which newlib treats as permanent standard time.
    const char* p = TZ_STRING;
    size_t n = 0;
    while (*p && !(*p == '-' || (*p >= '0' && *p <= '9')) && n < sizeof(s_buf) - 1)
      s_buf[n++] = *p++;
    while (*p && ((*p == '-' || *p == '+') || (*p >= '0' && *p <= '9') || *p == ':')
           && n < sizeof(s_buf) - 1)
      s_buf[n++] = *p++;
    s_buf[n] = 0;
    return s_buf;
  }

  void configure() {
    Serial.printf("[ntp] TZ=%s\n", tzString());
    configTzTime(tzString(), SNTP_SERVER_1, SNTP_SERVER_2);
    setenv("TZ", tzString(), 1);
    tzset();
    s_configured = true;
  }
}

namespace ntp {

void init() {
  Preferences p;
  if (p.begin("tz", true)) { s_dst = p.getBool("dst", false); p.end(); }
  // configure() deferred until WiFi connects (see tick).
}

void tick() {
  bool c = wifi::connected();
  if (c && !s_wasConnected) configure();   // (re)start sync on each connect
  s_wasConnected = c;
}

bool synced() {
  if (!s_configured) return false;
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  return lt.tm_year > 120;   // year >= 2020
}

bool dst() { return s_dst; }

void setDst(bool on) {
  if (s_dst == on) return;
  s_dst = on;
  Preferences p;
  if (p.begin("tz", false)) { p.putBool("dst", s_dst); p.end(); }
  // Force a full re-init (not just setenv) so configTzTime re-applies the
  // string to the underlying SNTP/newlib state. The lighter setenv+tzset
  // dance was observed to silently no-op on this runtime.
  if (wifi::connected()) configure();
  else { setenv("TZ", tzString(), 1); tzset(); }
}

} // namespace ntp
