// MicroSD card (SDMMC 4-bit). Best-effort mount at boot; status surfaced by
// the Settings screen. Safe to call init() even if no card is present.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include <driver/sdmmc_types.h>

namespace sdcard {
  void init();                  // attempt mount; logs result
  bool mounted();
  uint64_t totalBytes();
  uint64_t usedBytes();
  const char* typeStr();        // "MMC" / "SDSC" / "SDHC" / "" if not mounted
  sdmmc_card_t* rawCard();      // ESP-IDF card handle for raw sector I/O (MSC)
}
