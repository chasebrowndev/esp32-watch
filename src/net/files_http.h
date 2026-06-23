// AP-hosted file server for the SD card. Brings up a WPA2 softAP and serves
// a small web UI on port 80 for browsing / uploading / downloading /
// deleting files under /sd. Lifecycle is screen-driven: open the screen ->
// start(); leave -> stop(). Idempotent.
//
// Requires FEAT_WIFI. SD must be mounted (and not exported via USB-MSC)
// for file operations to succeed; the UI surfaces "no sd" otherwise.
#pragma once
#include <stdbool.h>
#include <stdint.h>

namespace files_http {
  // Bring up softAP with the given SSID + WPA2 password (>=8 chars) and
  // start the HTTP server. Returns false if AP failed to come up.
  bool start(const char* ssid, const char* pass);

  // Tear everything down — HTTP server, AP. Safe to call when not running.
  void stop();

  bool running();

  // Number of currently-associated clients on the AP.
  uint8_t clientCount();

  // Lightweight activity log surfaced by the UI: ring of the last N events
  // (uploads/downloads/deletes). UI calls snapshot(); writes happen inside
  // the request handlers.
  struct Hit {
    uint32_t ms;          // millis() at log time
    char     op[4];       // "UP" / "DL" / "RM" / "MK"
    char     path[64];
  };
  uint8_t snapshot(Hit* out, uint8_t max);
}
