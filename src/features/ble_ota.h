// BLE file drop. Adds a custom GATT service to the existing NimBLE server
// (the one ble_hid owns) so a host can stream a file onto /firmware.bin on
// the SD card over BLE — no USB-MSC roundtrip. The user then taps INSTALL
// in the USB INSTALL app to flash from SD (existing flow).
//
// Protocol (one connection, sequential):
//   write CTL "S" + u32 LE size      -> open /firmware.bin for write
//   write DATA  <chunk>... (no-resp) -> append chunk
//   write CTL "E"                    -> close file, OK to flash
//   write CTL "A"                    -> abort + delete partial file
// Status notifications on CTL:
//   "O"                              -> ack
//   "P" + u32 LE bytesWritten        -> progress (every ~16 KB)
//   "E<errstr>"                      -> error (and aborts)
#pragma once
class NimBLEServer;

namespace ble_ota {
  // Attach the OTA service to an existing server. Safe to call once after
  // NimBLEDevice::init + createServer.
  void begin(NimBLEServer* server);

  // True while a transfer is in progress.
  bool active();
}
