#include "sd_config.h"
#include "sdcard.h"
#include "usb_msc.h"
#include "../../include/config.h"
#include <SD_MMC.h>

namespace sd_config {

bool available() {
  if (!sdcard::mounted()) return false;
#if FEAT_USB_MSC
  if (usb_msc::enabled()) return false;
#endif
  return true;
}

bool loadJson(const char* path, JsonDocument& doc) {
  if (!available() || !path) return false;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  return err == DeserializationError::Ok;
}

bool loadText(const char* path, String& out) {
  if (!available() || !path) return false;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  out.reserve(f.size());
  while (f.available()) out += (char)f.read();
  f.close();
  return true;
}

static void ensureParentDir(const char* path) {
  // Walk from left to right; mkdir each intermediate component.
  char buf[96];
  size_t n = strlen(path);
  if (n >= sizeof(buf)) return;
  memcpy(buf, path, n + 1);
  for (size_t i = 1; i < n; ++i) {
    if (buf[i] == '/') {
      buf[i] = 0;
      if (!SD_MMC.exists(buf)) SD_MMC.mkdir(buf);
      buf[i] = '/';
    }
  }
}

bool appendLine(const char* path, const String& line) {
  if (!available() || !path) return false;
  ensureParentDir(path);
  File f = SD_MMC.open(path, FILE_APPEND);
  if (!f) {
    f = SD_MMC.open(path, FILE_WRITE);
    if (!f) return false;
  }
  f.print(line);
  f.print('\n');
  f.close();
  return true;
}

} // namespace sd_config
