// SNTP time sync. Starts once WiFi is up; sets the system clock + local TZ.
#pragma once
#include <stdbool.h>

namespace ntp {
  void init();      // configure SNTP + TZ (call once after wifi::init)
  void tick();      // (re)start sync when WiFi transitions to connected
  bool synced();    // true once the clock has a real (post-2020) time

  // Runtime DST override. When on, TZ is set to permanent daylight time
  // (clock +1h from standard); off uses permanent standard time.
  // Persisted in NVS; takes effect immediately.
  bool dst();
  void setDst(bool on);
}
