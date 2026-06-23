#include "ble_hid.h"
#include "ble_radio.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include <lvgl.h>
#include <vector>

namespace {
  NimBLEServer*       s_server  = nullptr;
  NimBLEHIDDevice*    s_hid     = nullptr;
  NimBLECharacteristic* s_input = nullptr;
  NimBLECharacteristic* s_kbInput = nullptr;
  NimBLECharacteristic* s_mouseInput = nullptr;
  volatile bool       s_connected = false;
  bool                s_init    = false;
  bool                s_suspended = false;

  // HID report map: report ID 1 = consumer control (1 byte, 8 bits),
  //                 report ID 2 = boot keyboard (8 mods + 1 reserved + 6 keys).
  const uint8_t REPORT_MAP[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x09, 0xCD,        //   Play/Pause
    0x09, 0xB5,        //   Scan Next
    0x09, 0xB6,        //   Scan Previous
    0x09, 0xE9,        //   Volume Up
    0x09, 0xEA,        //   Volume Down
    0x09, 0xE2,        //   Mute
    0x0A, 0x23, 0x02,  //   AC Home
    0x0A, 0x24, 0x02,  //   AC Back
    0x81, 0x02,
    0xC0,

    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Min (Left Ctrl)
    0x29, 0xE7,        //   Usage Max (Right GUI)
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,        //   modifiers
    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,        //   reserved
    0x95, 0x06,
    0x75, 0x08,
    0x15, 0x00,
    0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x65,
    0x81, 0x00,        //   6 keycodes
    0xC0,

    // Report ID 3 = relative mouse (3 buttons + dx + dy + wheel).
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Buttons)
    0x19, 0x01, 0x29, 0x03,
    0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01,
    0x81, 0x02,        //     3 button bits
    0x95, 0x01, 0x75, 0x05,
    0x81, 0x03,        //     5 bits padding
    0x05, 0x01,
    0x09, 0x30,        //     X
    0x09, 0x31,        //     Y
    0x09, 0x38,        //     Wheel
    0x15, 0x81, 0x25, 0x7F,
    0x75, 0x08, 0x95, 0x03,
    0x81, 0x06,        //     X,Y,Wheel relative
    0xC0,
    0xC0
  };

  enum Bit : uint8_t {
    B_PLAY = 1 << 0, B_NEXT = 1 << 1, B_PREV = 1 << 2, B_VOLUP = 1 << 3,
    B_VOLDN = 1 << 4, B_MUTE = 1 << 5, B_HOME = 1 << 6, B_BACK = 1 << 7,
  };

  // Dumps the current bond list to serial. Called on connect/disconnect and
  // after pair-related operations so we can tell from a serial capture
  // whether bonds are actually persisting vs. silently being evicted.
  static void dumpBonds(const char* tag) {
    int n = NimBLEDevice::getNumBonds();
    Serial.printf("[hid] bonds@%s count=%d cap=%d\n",
                  tag, n, CONFIG_BT_NIMBLE_MAX_BONDS);
    for (int i = 0; i < n; ++i) {
      NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
      Serial.printf("  [%d] %s\n", i, a.toString().c_str());
    }
  }

  class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*) override    {
      s_connected = true;
      dumpBonds("connect");
    }
    void onDisconnect(NimBLEServer*) override {
      s_connected = false;
      // Only re-advertise if HID still owns the radio. If spam or scan
      // took over, restarting advertising here would clobber their state
      // and put the device in the "non-connectable spoof leftover"
      // failure mode that motivated this whole refactor.
      if (!s_suspended && ble_radio::current() != ble_radio::MODE_SPAM
                       && ble_radio::current() != ble_radio::MODE_SCAN) {
        ble_hid::startAdvertising();
      }
    }
  };
  ServerCB s_cb;

  // Single-slot pending release. A key-down is sent immediately and the
  // matching key-up is dispatched ~20 ms later via an lv_timer so the LVGL
  // event loop (and therefore the touch indev) isn't blocked by delay().
  // If a new key arrives while one is still pending, the previous release is
  // flushed immediately so the host never sees two simultaneous presses.
  enum class Pending : uint8_t { None, Consumer, Keyboard, Mouse };
  volatile Pending s_pending = Pending::None;
  lv_timer_t*      s_release = nullptr;

  void flushRelease() {
    if (s_pending == Pending::Consumer && s_input) {
      uint8_t v = 0;
      s_input->setValue(&v, 1); s_input->notify();
    } else if (s_pending == Pending::Keyboard && s_kbInput) {
      uint8_t r[8] = {0};
      s_kbInput->setValue(r, 8); s_kbInput->notify();
    } else if (s_pending == Pending::Mouse && s_mouseInput) {
      uint8_t r[4] = {0, 0, 0, 0};
      s_mouseInput->setValue(r, 4); s_mouseInput->notify();
    }
    s_pending = Pending::None;
  }

  void releaseTimerCb(lv_timer_t* t) {
    flushRelease();
    lv_timer_delete(t);
    s_release = nullptr;
  }

  void scheduleRelease(Pending p) {
    if (s_release) { lv_timer_delete(s_release); s_release = nullptr; flushRelease(); }
    s_pending = p;
    s_release = lv_timer_create(releaseTimerCb, 20, nullptr);
    lv_timer_set_repeat_count(s_release, 1);
  }

  void sendBit(uint8_t bit) {
    if (!s_connected || !s_input) return;
    if (s_release) { lv_timer_delete(s_release); s_release = nullptr; flushRelease(); }
    uint8_t v = bit;
    s_input->setValue(&v, 1); s_input->notify();
    scheduleRelease(Pending::Consumer);
  }

  // Boot-keyboard input: 8 bytes [mods, reserved, keycode0..5].
  void sendKey(uint8_t keycode) {
    if (!s_connected || !s_kbInput) return;
    if (s_release) { lv_timer_delete(s_release); s_release = nullptr; flushRelease(); }
    uint8_t r[8] = { 0, 0, keycode, 0, 0, 0, 0, 0 };
    s_kbInput->setValue(r, 8); s_kbInput->notify();
    scheduleRelease(Pending::Keyboard);
  }
}

namespace ble_hid {

namespace { char s_name[24] = "smartwatch"; }

void applySelection();   // fwd; defined below in this namespace
namespace { bool inBlacklist(const char* addr); } // fwd for tick()

// Re-install the HID advertisement payload + connectable mode + intervals.
// This is the single source of truth for "what HID advertising looks like."
// Called from begin(), resume(), applySelection(), and via the radio
// arbiter when another subsystem releases the radio. Without this, the
// shared NimBLEAdvertising singleton retains whatever spam/beacon stuffed
// into it and HID re-advertises as a non-connectable spoof packet.
static void reinstallAdvData() {
  if (!s_init || !s_hid) return;
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (!adv) return;

  NimBLEAdvertisementData d;
  d.setFlags(0x06);                       // LE General Discoverable + BR/EDR not supported
  d.setName(s_name);
  d.setAppearance(0x03C1);                // HID Keyboard
  d.setCompleteServices(s_hid->hidService()->getUUID());
  adv->setAdvertisementData(d);

  NimBLEAdvertisementData sr;             // scan response carries the name too
  sr.setName(s_name);
  adv->setScanResponseData(sr);

  adv->setAppearance(0x03C1);
  adv->setName(s_name);
  adv->setAdvertisementType(BLE_GAP_CONN_MODE_UND);   // connectable, undirected
  adv->setMinInterval(0x30);              // 30 ms
  adv->setMaxInterval(0x60);              // 60 ms
}

// Restore callback used by ble_radio when the radio is handed back to HID.
static void radioRestoreHid() {
  if (!s_init) return;
  reinstallAdvData();
  if (!s_suspended) NimBLEDevice::startAdvertising();
}

void begin(const char* name) {
  if (s_init) return;
  if (name) { strncpy(s_name, name, sizeof(s_name) - 1); s_name[sizeof(s_name) - 1] = 0; }
  NimBLEDevice::init(s_name);
  // bond=yes, MITM=no, SC=no. Requiring SC here was rejecting BlueZ peers
  // that negotiated LE Legacy → "Authentication failure" reconnect loops.
  // Letting SMP pick its own mode is broader-compat without losing bonding.
  NimBLEDevice::setSecurityAuth(true, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);   // Just Works

  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(&s_cb);

  s_hid = new NimBLEHIDDevice(s_server);
  s_hid->manufacturer()->setValue("smartwatch");
  s_hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  s_hid->hidInfo(0x00, 0x01);
  s_hid->reportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  s_input      = s_hid->inputReport(1);
  s_kbInput    = s_hid->inputReport(2);
  s_mouseInput = s_hid->inputReport(3);
  s_hid->startServices();

  s_init = true;
  ble_radio::init();
  ble_radio::setHidRestore(&radioRestoreHid);
  ble_radio::acquire(ble_radio::MODE_HID);
  reinstallAdvData();   // canonical HID advertisement state
  applySelection();     // configure whitelist + advertise
}

namespace { uint32_t s_advCooldownUntil = 0; }
void tick() {
  if (!s_init || !s_server) return;
  uint32_t now = millis();

  // End-of-cooldown: resume advertising once a blacklisted peer has had time
  // to back off. Without this the phone auto-reconnects faster than the
  // watchdog can kick it and the user sees a flapping "paired/pair-new".
  if (s_advCooldownUntil && (int32_t)(now - s_advCooldownUntil) >= 0
      && !s_suspended) {
    s_advCooldownUntil = 0;
    NimBLEDevice::startAdvertising();
  }

  if (!s_connected) return;
  static uint32_t lastMs = 0;
  if (now - lastMs < 100) return;
  lastMs = now;

  // Build a NimBLEAddress set of blacklisted bonds *fresh each tick*: the
  // string-based inBlacklist() was missing matches when toString()
  // formatting differed between the bond store and live peer info. Compare
  // via NimBLEAddress equality (operator==) which checks the underlying
  // 6-byte value, not the rendered text.
  int nb = NimBLEDevice::getNumBonds();
  std::vector<NimBLEAddress> blAddrs;
  for (int i = 0; i < nb; ++i) {
    NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
    if (inBlacklist(a.toString().c_str())) blAddrs.push_back(a);
  }

  for (auto h : s_server->getPeerDevices()) {
    NimBLEConnInfo info = s_server->getPeerInfo(h);
    NimBLEAddress rpa = info.getAddress();
    NimBLEAddress id  = info.getIdAddress();
    bool match = false;
    for (auto& b : blAddrs) {
      if (b == rpa || b == id) { match = true; break; }
    }
    Serial.printf("[hid] watchdog peer rpa=%s id=%s match=%d bl=%u\n",
                  rpa.toString().c_str(), id.toString().c_str(),
                  (int)match, (unsigned)blAddrs.size());
    if (match) {
      s_server->disconnect(h);
      NimBLEDevice::stopAdvertising();
      s_advCooldownUntil = now + 3000;
    }
  }
}
bool connected() { return s_connected; }

void startAdvertising() { if (s_init) NimBLEDevice::startAdvertising(); }
void stopAdvertising()  { if (s_init) NimBLEDevice::stopAdvertising(); }

void suspend() {
  if (!s_init) return;
  s_suspended = true;
  NimBLEDevice::stopAdvertising();
  if (s_server) {
    for (auto h : s_server->getPeerDevices()) s_server->disconnect(h);
  }
  // 2 ms scheduler yield (NOT a no-op) so the NimBLE host task gets a
  // chance to actually process the disconnect before the next caller
  // touches the radio. Callers that share the radio (e.g. BLE spam) go
  // through ble_radio::acquire(), which is safe even mid-teardown.
  vTaskDelay(pdMS_TO_TICKS(2));
}

void resume() {
  if (!s_init) return;
  s_suspended = false;
  // Hand the radio back to HID. The arbiter calls radioRestoreHid, which
  // reinstalls the connectable HID advertisement and starts advertising.
  ble_radio::acquire(ble_radio::MODE_HID);
  reinstallAdvData();
  NimBLEDevice::startAdvertising();
}

namespace {
  // Single shared buffer for any addr/string returned to UI code. UI must
  // copy before calling another bond accessor.
  char s_addrBuf[20] = {0};
}

const char* localAddr() {
  if (!s_init) { s_addrBuf[0] = 0; return s_addrBuf; }
  std::string a = NimBLEDevice::getAddress().toString();
  strncpy(s_addrBuf, a.c_str(), sizeof(s_addrBuf) - 1);
  s_addrBuf[sizeof(s_addrBuf) - 1] = 0;
  return s_addrBuf;
}

const char* peerAddr() {
  if (!s_init || !s_connected || !s_server) { s_addrBuf[0] = 0; return s_addrBuf; }
  auto peers = s_server->getPeerDevices();
  if (peers.empty()) { s_addrBuf[0] = 0; return s_addrBuf; }
  // 1.4.3: getPeerDevices returns std::vector<uint16_t> of conn handles
  NimBLEConnInfo info = s_server->getPeerInfo(peers[0]);
  std::string a = info.getAddress().toString();
  strncpy(s_addrBuf, a.c_str(), sizeof(s_addrBuf) - 1);
  s_addrBuf[sizeof(s_addrBuf) - 1] = 0;
  return s_addrBuf;
}

bool peerBlacklisted() {
  if (!s_init || !s_connected || !s_server) return false;
  for (auto h : s_server->getPeerDevices()) {
    NimBLEConnInfo info = s_server->getPeerInfo(h);
    std::string idAddr = info.getIdAddress().toString();
    std::string addr   = info.getAddress().toString();
    if ((!idAddr.empty() && inBlacklist(idAddr.c_str())) ||
        (!addr.empty()   && inBlacklist(addr.c_str()))) return true;
  }
  return false;
}

uint8_t numBonds() {
  if (!s_init) return 0;
  int n = NimBLEDevice::getNumBonds();
  return n < 0 ? 0 : (uint8_t)n;
}

const char* bondAddr(uint8_t i) {
  if (!s_init || i >= numBonds()) { s_addrBuf[0] = 0; return s_addrBuf; }
  NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
  std::string s = a.toString();
  strncpy(s_addrBuf, s.c_str(), sizeof(s_addrBuf) - 1);
  s_addrBuf[sizeof(s_addrBuf) - 1] = 0;
  return s_addrBuf;
}

namespace {
  // NVS key from address: strip colons -> 12 hex chars (fits NVS 15-char limit).
  void addrKey(const char* addr, char out[16]) {
    int j = 0;
    for (int k = 0; addr[k] && j < 15; ++k) if (addr[k] != ':') out[j++] = addr[k];
    out[j] = 0;
  }
}

const char* bondName(uint8_t i) {
  s_addrBuf[0] = 0;
  if (!s_init || i >= numBonds()) return s_addrBuf;
  NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
  char k[16]; addrKey(a.toString().c_str(), k);
  Preferences p;
  if (!p.begin("bondnm", true)) return s_addrBuf;
  p.getString(k, s_addrBuf, sizeof(s_addrBuf));
  p.end();
  return s_addrBuf;
}

void setBondName(uint8_t i, const char* name) {
  if (!s_init || i >= numBonds() || !name) return;
  NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
  char k[16]; addrKey(a.toString().c_str(), k);
  Preferences p;
  if (!p.begin("bondnm", false)) return;
  if (name[0]) p.putString(k, name);
  else         p.remove(k);
  p.end();
}

void forget(uint8_t i) {
  if (!s_init || i >= numBonds()) return;
  NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
  std::string addrStr = a.toString();
  if (s_server) {
    for (auto h : s_server->getPeerDevices()) {
      NimBLEConnInfo info = s_server->getPeerInfo(h);
      if (info.getAddress() == a) s_server->disconnect(h);
    }
  }
  NimBLEDevice::deleteBond(a);
  // applySelection() prunes blacklist entries whose bond no longer exists
  // and persists the result, so dropping this addr from the blacklist
  // happens automatically inside it.
  applySelection();
  dumpBonds("forget");
}

void forgetAll() {
  if (!s_init) return;
  if (s_server) {
    for (auto h : s_server->getPeerDevices()) s_server->disconnect(h);
  }
  NimBLEDevice::deleteAllBonds();
  clearSelection();   // also calls applySelection()
  dumpBonds("forgetAll");
}

namespace {
  // Blacklist: addresses of bonded peers the user has unchecked. A bond not
  // in this set is "allowed" (checked) — default state for any newly-bonded
  // device. Persisted in NVS as a newline-separated string under key "bl".
  std::vector<std::string> s_blacklist;
  bool s_blLoaded = false;

  void loadBlacklist() {
    s_blacklist.clear();
    Preferences p;
    if (!p.begin("bondsel", true)) { s_blLoaded = true; return; }
    String s = p.getString("bl", "");
    p.end();
    int start = 0;
    for (int i = 0; i <= s.length(); ++i) {
      if (i == s.length() || s[i] == '\n') {
        if (i > start) s_blacklist.emplace_back(s.c_str() + start, i - start);
        start = i + 1;
      }
    }
    s_blLoaded = true;
  }
  void storeBlacklist() {
    Preferences p;
    if (!p.begin("bondsel", false)) return;
    if (s_blacklist.empty()) {
      p.remove("bl");
    } else {
      String joined;
      for (size_t i = 0; i < s_blacklist.size(); ++i) {
        if (i) joined += '\n';
        joined += s_blacklist[i].c_str();
      }
      p.putString("bl", joined);
    }
    p.end();
  }
  bool inBlacklist(const char* addr) {
    if (!addr || !addr[0]) return false;
    for (auto& s : s_blacklist) if (s == addr) return true;
    return false;
  }
  void blacklistAdd(const char* addr) {
    if (!addr || !addr[0]) return;
    if (inBlacklist(addr)) return;
    s_blacklist.emplace_back(addr);
    storeBlacklist();
  }
  void blacklistRemove(const char* addr) {
    if (!addr || !addr[0]) return;
    for (auto it = s_blacklist.begin(); it != s_blacklist.end(); ++it) {
      if (*it == addr) { s_blacklist.erase(it); storeBlacklist(); return; }
    }
  }
}

// Prune blacklist entries whose bond no longer exists, then re-advertise.
// Connection gating is done in tick() so it can wait for IRK resolution.
void applySelection() {
  if (!s_init) return;
  if (!s_blLoaded) loadBlacklist();

  // Drop stale blacklist entries (bonds that have since been forgotten).
  std::vector<std::string> live;
  int n = NimBLEDevice::getNumBonds();
  for (int i = 0; i < n; ++i) {
    std::string a = NimBLEDevice::getBondedAddress(i).toString();
    if (inBlacklist(a.c_str())) live.push_back(a);
  }
  if (live.size() != s_blacklist.size()) {
    s_blacklist = std::move(live);
    storeBlacklist();
  }
  Serial.printf("[hid] applySelection bonds=%d blacklist=%u suspended=%d\n",
                n, (unsigned)s_blacklist.size(), (int)s_suspended);

  NimBLEDevice::stopAdvertising();
  reinstallAdvData();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  // No whitelist filter — we want any device (including the brand-new one
  // PAIR NEW is about to talk to) to be able to initiate. Blacklist
  // enforcement happens in tick() after the connection is authenticated.
  adv->setScanFilter(false, false);

  // Kick any currently-connected peer whose identity matches a blacklist.
  if (s_server) {
    for (auto h : s_server->getPeerDevices()) {
      NimBLEConnInfo info = s_server->getPeerInfo(h);
      std::string idAddr = info.getIdAddress().toString();
      std::string addr   = info.getAddress().toString();
      if (inBlacklist(idAddr.c_str()) || inBlacklist(addr.c_str())) {
        s_server->disconnect(h);
      }
    }
  }

  if (!s_suspended) {
    bool ok = NimBLEDevice::startAdvertising();
    Serial.printf("[hid] startAdvertising -> %d\n", (int)ok);
  } else {
    Serial.println("[hid] advertising NOT started (suspended)");
  }
}

bool isSelected(uint8_t i) {
  if (!s_init || i >= numBonds()) return false;
  if (!s_blLoaded) loadBlacklist();
  std::string a = NimBLEDevice::getBondedAddress(i).toString();
  return !inBlacklist(a.c_str());
}

void selectBond(uint8_t i) {
  if (!s_init || i >= numBonds()) return;
  if (!s_blLoaded) loadBlacklist();
  std::string a = NimBLEDevice::getBondedAddress(i).toString();
  blacklistRemove(a.c_str());
  applySelection();
}

void deselectBond(uint8_t i) {
  if (!s_init || i >= numBonds()) return;
  if (!s_blLoaded) loadBlacklist();
  std::string a = NimBLEDevice::getBondedAddress(i).toString();
  blacklistAdd(a.c_str());
  applySelection();   // also kicks the peer if it's currently connected
}

bool pairNewMode() {
  if (!s_init) return false;
  if (!s_blLoaded) loadBlacklist();
  int n = NimBLEDevice::getNumBonds();
  return n > 0 && (int)s_blacklist.size() >= n;
}

void exitPairNewMode() {
  if (!s_init) return;
  if (!s_blLoaded) loadBlacklist();
  s_blacklist.clear();
  storeBlacklist();
  applySelection();
}

// PAIR NEW: blacklist every existing bond. The new device (not yet bonded)
// can still connect because it isn't on the list. Once paired, the new
// bond is allowed by default (not in blacklist).
void clearSelection() {
  if (!s_init) return;
  if (!s_blLoaded) loadBlacklist();
  s_blacklist.clear();
  int n = NimBLEDevice::getNumBonds();
  for (int i = 0; i < n; ++i) {
    std::string a = NimBLEDevice::getBondedAddress(i).toString();
    s_blacklist.push_back(a);
  }
  storeBlacklist();
  applySelection();
}


void playPause() { sendBit(B_PLAY); }
void next()      { sendBit(B_NEXT); }
void prev()      { sendBit(B_PREV); }
void volUp()     { sendBit(B_VOLUP); }
void volDown()   { sendBit(B_VOLDN); }
void mute()      { sendBit(B_MUTE); }
void home()      { sendBit(B_HOME); }
void back()      { sendBit(B_BACK); }

void up()     { sendKey(0x52); }
void down()   { sendKey(0x51); }
void left()   { sendKey(0x50); }
void right()  { sendKey(0x4F); }
void select() { sendKey(0x28); }
void escape() { sendKey(0x29); }

// Trackpad: motion + click + scroll. The trackpad screen sends a stream of
// these every poll, so we DON'T schedule auto-releases for move/scroll —
// those are inherently transient (zero is the resting state). Clicks DO
// schedule a release so a tap on the pad lands a clean press+release on
// the host without the screen having to manage timing.
static void sendMouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
  if (!s_connected || !s_mouseInput) return;
  uint8_t r[4] = { buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel };
  s_mouseInput->setValue(r, 4); s_mouseInput->notify();
}

void mouseMove(int8_t dx, int8_t dy) {
  if (dx == 0 && dy == 0) return;
  sendMouse(0, dx, dy, 0);
}
void mouseScroll(int8_t wheel) {
  if (wheel == 0) return;
  sendMouse(0, 0, 0, wheel);
}
void mouseClick(uint8_t button) {
  if (!s_connected || !s_mouseInput) return;
  if (s_release) { lv_timer_delete(s_release); s_release = nullptr; flushRelease(); }
  sendMouse(button, 0, 0, 0);
  scheduleRelease(Pending::Mouse);
}
void mousePress(uint8_t button) {
  if (!s_connected || !s_mouseInput) return;
  if (s_release) { lv_timer_delete(s_release); s_release = nullptr; }
  sendMouse(button, 0, 0, 0);
  s_pending = Pending::Mouse;   // release deferred until mouseRelease()
}
void mouseRelease() {
  if (s_pending == Pending::Mouse) flushRelease();
}

} // namespace ble_hid
