#include "sdcard.h"
#include "../../include/board_pins.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <driver/sdmmc_types.h>

// SDMMCFS::_card is protected. This accessor subclass exposes it without
// modifying the library. The reinterpret_cast is safe because Accessor adds
// no data members or virtual functions, so layouts are identical.
namespace {
  struct Accessor : public fs::SDMMCFS {
    sdmmc_card_t* card() { return _card; }
  };
}

namespace {
  bool s_mounted = false;
  const char* s_type = "";
}

namespace sdcard {

void init() {
  // Reset state up front so a failed re-init can't leave a stale `mounted`
  // flag from a prior successful mount. usb_msc reads s_mounted to decide
  // whether to expose media to the host; if it's stale, MSC reads hit a
  // dead card pointer and the host sees I/O errors on every sector.
  s_mounted = false;
  s_type    = "";

  // 4-bit SDMMC on the dedicated card-detect pins (38/40/39/41/48/47).
  if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD,
                      PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3)) {
    Serial.println("[sd] setPins failed");
    return;
  }
  if (!SD_MMC.begin("/sd", false /*1-bit*/, false /*format*/)) {
    Serial.println("[sd] no card");
    return;
  }
  uint8_t t = SD_MMC.cardType();
  switch (t) {
    case CARD_MMC:  s_type = "MMC";  break;
    case CARD_SD:   s_type = "SDSC"; break;
    case CARD_SDHC: s_type = "SDHC"; break;
    default:        s_type = "";     SD_MMC.end(); return;
  }
  s_mounted = true;
  Serial.printf("[sd] %s  %llu MB\n", s_type, SD_MMC.cardSize() / (1024ULL*1024ULL));
}

bool mounted()              { return s_mounted; }
uint64_t totalBytes()       { return s_mounted ? SD_MMC.totalBytes() : 0; }
uint64_t usedBytes()        { return s_mounted ? SD_MMC.usedBytes()  : 0; }
const char* typeStr()       { return s_type; }
sdmmc_card_t* rawCard()     { return reinterpret_cast<Accessor*>(&SD_MMC)->card(); }

} // namespace sdcard
