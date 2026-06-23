// WiFi connection manager. Saved networks live in NVS (Preferences); up to
// WIFI_MAX_SAVED entries. tick() cycles through them when not connected.
// First-boot can seed entry 0 from LittleFS /wifi.json.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WIFI_MAX_SAVED 8

namespace wifi {
  void init();
  void tick();
  bool connected();
  const char* ssid();                // SSID we're currently connected to / trying
  int8_t rssi();

  // Saved-network list. Index 0..savedCount()-1.
  uint8_t     savedCount();
  const char* savedSsid(uint8_t i);
  const char* savedPass(uint8_t i);
  // Add or update (matches by SSID). Triggers an immediate connect attempt.
  // Returns false if the list is full and the SSID is new.
  bool        addNetwork(const char* ssid, const char* pass);
  void        removeNetwork(uint8_t i);

  // Async scan.
  void        scanStart();
  // Synchronous scan — blocks ~3 s. Returns count or -2.
  int16_t     scanBlocking();
  bool        scanRunning();
  int16_t     scanCount();
  const char* scanSsid(int16_t i);
  int8_t      scanRssi(int16_t i);
  // 0 = open, otherwise WIFI_AUTH_* enum value from esp_wifi.
  uint8_t     scanAuth(int16_t i);
  // Raw WiFi.scanComplete() value: >=0 result count, -1 running, -2 failed.
  int16_t     scanRaw();

  // Pause the STA reconnect loop and yield the radio so callers can switch
  // to AP/promiscuous mode. resume() restores STA mode and re-attempts the
  // last working slot. Idempotent.
  void        suspend();
  void        resume();
  bool        suspended();
}
