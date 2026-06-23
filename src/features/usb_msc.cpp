#include "usb_msc.h"
#include "sdcard.h"
#include "../../include/config.h"
#include <Arduino.h>
#include <atomic>

#if FEAT_USB_MSC
#include <USBMSC.h>
#include <SD_MMC.h>
#include <sdmmc_cmd.h>

static USBMSC s_msc;
static bool   s_enabled        = false;
static bool   s_started        = false;
// set from TinyUSB task on core 0, read from LVGL loop on core 1.
// std::atomic gives us the cross-core ordering volatile alone doesn't.
static std::atomic<bool> s_eject{false};
static bool   s_need_remount   = false;

static int32_t onRead(uint32_t lba, uint32_t offset, void* buf, uint32_t bufsize) {
  if (!s_enabled || offset != 0 || bufsize % 512 != 0) return -1;
  sdmmc_card_t* card = sdcard::rawCard();
  if (!card) return -1;
  return sdmmc_read_sectors(card, buf, lba, bufsize / 512) == ESP_OK
         ? (int32_t)bufsize : -1;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buf, uint32_t bufsize) {
  if (!s_enabled || offset != 0 || bufsize % 512 != 0) return -1;
  sdmmc_card_t* card = sdcard::rawCard();
  if (!card) return -1;
  return sdmmc_write_sectors(card, buf, lba, bufsize / 512) == ESP_OK
         ? (int32_t)bufsize : -1;
}

static bool onStartStop(uint8_t /*power_condition*/, bool start, bool load_eject) {
  if (load_eject && !start) s_eject.store(true);   // host ejected; handle in tick()
  return true;
}

namespace usb_msc {

void begin() {
  if (!sdcard::mounted()) {
    Serial.println(F("[usb_msc] no sd, skipping"));
    return;
  }
  uint64_t sectors = SD_MMC.cardSize() / 512;
  s_msc.vendorID("Watch");
  s_msc.productID("MicroSD");
  s_msc.productRevision("1.0");
  s_msc.onRead(onRead);
  s_msc.onWrite(onWrite);
  s_msc.onStartStop(onStartStop);
  s_msc.mediaPresent(false);
  if (!s_msc.begin(sectors, 512)) {
    Serial.println(F("[usb_msc] begin failed"));
    return;
  }
  s_started = true;
  Serial.printf("[usb_msc] ready  %llu sectors\n", (unsigned long long)sectors);
}

void enable() {
  if (!s_started || !sdcard::mounted()) return;
  // Guard against a stale-mounted state where the raw card pointer is null
  // (can happen after a remount cycle if init partially failed). Without this
  // check, every host read returns -1 and the user sees I/O errors.
  if (!sdcard::rawCard()) {
    Serial.println(F("[usb_msc] no raw card, refusing to present"));
    return;
  }
  s_enabled       = true;
  s_need_remount  = false;
  s_eject.store(false);
  s_msc.mediaPresent(true);
  Serial.println(F("[usb_msc] presenting sd card"));
}

void disable() {
  if (!s_enabled) return;
  s_enabled = false;
  s_msc.mediaPresent(false);
  s_need_remount = true;
  Serial.println(F("[usb_msc] stopped, will remount fatfs"));
}

bool enabled() { return s_enabled; }

void tick() {
  // Handle host eject (signalled from TinyUSB interrupt context via flag).
  if (s_eject.load()) {
    s_eject.store(false);
    if (s_enabled) disable();
  }
  // Remount FATFS after MSC session ends so firmware can use SD again.
  if (s_need_remount && !s_enabled) {
    s_need_remount = false;
    SD_MMC.end();
    sdcard::init();
    Serial.println(F("[usb_msc] fatfs remounted"));
  }
}

} // namespace usb_msc

#else  // FEAT_USB_MSC disabled stub

namespace usb_msc {
  void begin()   {}
  void tick()    {}
  bool enabled() { return false; }
  void enable()  {}
  void disable() {}
}

#endif
