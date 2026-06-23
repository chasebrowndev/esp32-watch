// USB HID keyboard emulation over the USB-C port. Executes DuckyScript payloads
// stored as .txt files under /ducky/ on the SD card. Non-blocking: call tick()
// every loop iteration; each call processes one script line (or waits out a DELAY).
//
// Requires ARDUINO_USB_MODE=0 (TinyUSB) and FEAT_USB_HID=1.
#pragma once

namespace usb_hid {
  void begin();
  void tick();

  bool        active();
  const char* scriptName();          // basename of running script, "" when idle
  void        runScript(const char* sdPath);  // e.g. "/ducky/payload.txt"
  void        cancel();

  // Progress for the currently running script. currentLine() counts executed
  // non-empty lines; totalLines() is pre-scanned at runScript() time. Both 0
  // when idle.
  unsigned    currentLine();
  unsigned    totalLines();
}
