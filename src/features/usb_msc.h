// USB Mass Storage: presents the microSD card as a USB drive to a connected host.
// When enabled, the firmware blocks SD card FATFS access to avoid corruption.
// After the host ejects (or the user disables MSC), FATFS is remounted.
//
// Requires ARDUINO_USB_MODE=0 (TinyUSB) and FEAT_USB_MSC=1.
#pragma once

namespace usb_msc {
  void begin();    // call after sdcard::init() in setup()
  void tick();     // call every loop; handles eject / remount sequencing
  bool enabled();
  void enable();
  void disable();
}
