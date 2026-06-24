#include "ble_ota.h"
#include "sdcard.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <SD_MMC.h>

namespace {
  // 128-bit UUIDs; the 16-bit "f00d" prefix is just a marker so it's easy
  // to pick out in scanners.
  const char* SVC_UUID  = "f00d0000-1c8f-4e0a-9d3c-4b6e4f5a8b00";
  const char* CTL_UUID  = "f00d0001-1c8f-4e0a-9d3c-4b6e4f5a8b00";
  const char* DATA_UUID = "f00d0002-1c8f-4e0a-9d3c-4b6e4f5a8b00";

  // Same path the USB install screen looks for. Lands the bin on SD; user
  // then taps INSTALL in the USB INSTALL app to actually flash.
  const char* FW_PATH = "/firmware.bin";

  NimBLECharacteristic* s_ctl  = nullptr;
  NimBLECharacteristic* s_data = nullptr;

  bool     s_active   = false;
  uint32_t s_total    = 0;
  uint32_t s_written  = 0;
  uint32_t s_lastNote = 0;
  File     s_file;

  void notifyOk()                  { uint8_t b = 'O'; s_ctl->setValue(&b, 1); s_ctl->notify(); }
  void notifyErr(const char* what) {
    char buf[48]; buf[0] = 'E';
    size_t n = strlen(what); if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
    memcpy(buf + 1, what, n);
    s_ctl->setValue((uint8_t*)buf, n + 1); s_ctl->notify();
  }
  void notifyProgress() {
    uint8_t buf[5]; buf[0] = 'P';
    memcpy(buf + 1, &s_written, 4);
    s_ctl->setValue(buf, 5); s_ctl->notify();
  }

  void abortXfer() {
    if (s_file) s_file.close();
    if (s_active) SD_MMC.remove(FW_PATH);   // don't leave a partial bin
    s_active = false;
    s_total = s_written = s_lastNote = 0;
  }
  void resetState() {
    if (s_file) s_file.close();
    s_active = false;
    s_total = s_written = s_lastNote = 0;
  }

  class CtlCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
      std::string v = c->getValue();
      if (v.empty()) return;
      char op = v[0];
      if (op == 'S') {
        if (v.size() < 5) { notifyErr("short start"); return; }
        if (s_active) abortXfer();
        if (!sdcard::mounted()) { notifyErr("no sd"); return; }
        uint32_t sz;
        memcpy(&sz, v.data() + 1, 4);
        if (sz == 0 || sz > 0x00C00000) { notifyErr("bad size"); return; }
        if (SD_MMC.exists(FW_PATH)) SD_MMC.remove(FW_PATH);
        s_file = SD_MMC.open(FW_PATH, FILE_WRITE);
        if (!s_file) { notifyErr("open fail"); return; }
        s_active = true;
        s_total  = sz;
        s_written = 0;
        s_lastNote = 0;
        Serial.printf("[ota] start size=%u -> %s\n", (unsigned)sz, FW_PATH);
        notifyOk();
      } else if (op == 'E') {
        if (!s_active) { notifyErr("not active"); return; }
        if (s_written != s_total) {
          char m[40]; snprintf(m, sizeof(m), "short %u/%u",
                               (unsigned)s_written, (unsigned)s_total);
          abortXfer(); notifyErr(m); return;
        }
        if (s_file) { s_file.flush(); s_file.close(); }
        resetState();
        Serial.println("[ota] file written — tap USB INSTALL to flash");
        notifyOk();
      } else if (op == 'A') {
        if (s_active) abortXfer();
        Serial.println("[ota] abort");
        notifyOk();
      } else {
        notifyErr("bad op");
      }
    }
  };

  class DataCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
      if (!s_active || !s_file) return;
      std::string v = c->getValue();
      if (v.empty()) return;
      size_t w = s_file.write((uint8_t*)v.data(), v.size());
      if (w != v.size()) {
        abortXfer();
        notifyErr("sd write fail");
        return;
      }
      s_written += w;
      // Progress notification every ~16 KB to avoid drowning the host.
      if (s_written - s_lastNote >= 16384 || s_written == s_total) {
        s_lastNote = s_written;
        notifyProgress();
      }
    }
  };

  CtlCB  s_ctlCb;
  DataCB s_dataCb;
}

namespace ble_ota {

void begin(NimBLEServer* server) {
  if (!server || s_ctl) return;
  NimBLEService* svc = server->createService(SVC_UUID);
  s_ctl = svc->createCharacteristic(
      CTL_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  s_ctl->setCallbacks(&s_ctlCb);
  s_data = svc->createCharacteristic(
      DATA_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  s_data->setCallbacks(&s_dataCb);
  svc->start();
  // Bigger MTU = bigger chunks = faster transfer. 517 is the max NimBLE
  // negotiates; the host still has to agree on the actual MTU at connect.
  NimBLEDevice::setMTU(517);
}

bool active() { return s_active; }

} // namespace ble_ota
