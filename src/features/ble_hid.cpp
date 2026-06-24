#include "ble_hid.h"
#include "ble_ota.h"
#include "ble_radio.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include <lvgl.h>

namespace {
  NimBLEServer*       s_server  = nullptr;
  NimBLEHIDDevice*    s_hid     = nullptr;
  NimBLECharacteristic* s_input = nullptr;
  NimBLECharacteristic* s_kbInput = nullptr;
  NimBLECharacteristic* s_mouseInput = nullptr;
  volatile bool       s_connected = false;
  volatile uint16_t   s_connHandle = 0xFFFF;
  bool                s_init    = false;
  bool                s_suspended = false;

  // ------- Reconnect state machine -------
  // The watch is a peripheral. "Connect to host" = undirected connectable
  // advertising with a connection-filter whitelist: the watch is visible to
  // every scanner (so Android/iOS surface it normally and HID auto-reconnect
  // logic kicks in), but only the bonded peer in the whitelist can actually
  // open a link. PAIR NEW = undirected, no whitelist, any peer can connect.
  //
  // Directed advertising (ADV_DIRECT_IND) was tried first but most phones'
  // general scanners skip it, so the watch looked invisible from the user's
  // POV — even when the directed adv was on-air. Undirected + whitelist is
  // the standard pattern for HID-over-GATT reconnect.
  enum AdvMode : uint8_t { ADV_IDLE, ADV_WHITELIST, ADV_OPEN };
  AdvMode       s_currentAdv = ADV_IDLE;
  bool          s_pairNew = false;            // PAIR NEW: open, accept any peer
  bool          s_hasSelection = false;       // s_selectedAddr is valid
  NimBLEAddress s_selectedAddr;               // whitelisted peer
  // Status-only deadline so the UI can say "still trying" vs "no response
  // yet, maybe pick a different host." Adv itself runs continuously while a
  // selection is active — the deadline does NOT stop advertising.
  uint32_t      s_selectStartedMs = 0;
  const uint32_t SELECT_TIMEOUT_MS = 45000;

  // ------- HID report map (unchanged) -------
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
    0x19, 0xE0,
    0x29, 0xE7,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,
    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,
    0x95, 0x06,
    0x75, 0x08,
    0x15, 0x00,
    0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x65,
    0x81, 0x00,
    0xC0,

    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,
    0x85, 0x03,        //   Report ID (3)
    0x09, 0x01,
    0xA1, 0x00,
    0x05, 0x09,
    0x19, 0x01, 0x29, 0x03,
    0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01,
    0x81, 0x02,
    0x95, 0x01, 0x75, 0x05,
    0x81, 0x03,
    0x05, 0x01,
    0x09, 0x30,
    0x09, 0x31,
    0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F,
    0x75, 0x08, 0x95, 0x03,
    0x81, 0x06,
    0xC0,
    0xC0
  };

  enum Bit : uint8_t {
    B_PLAY = 1 << 0, B_NEXT = 1 << 1, B_PREV = 1 << 2, B_VOLUP = 1 << 3,
    B_VOLDN = 1 << 4, B_MUTE = 1 << 5, B_HOME = 1 << 6, B_BACK = 1 << 7,
  };

  static void dumpBonds(const char* tag) {
    int n = NimBLEDevice::getNumBonds();
    Serial.printf("[hid] bonds@%s count=%d cap=%d\n",
                  tag, n, CONFIG_BT_NIMBLE_MAX_BONDS);
    for (int i = 0; i < n; ++i) {
      NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
      Serial.printf("  [%d] %s\n", i, a.toString().c_str());
    }
  }

  // GAP callbacks run in the NimBLE host task. The rule here is: NEVER call
  // startAdvertising / stopAdvertising from inside one — set flags and let
  // tick() (main task) converge the adv state. Doing the work inline races
  // with the host's own teardown and was the source of "advertising never
  // restarts after disconnect" stalls.
  class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, ble_gap_conn_desc* desc) override {
      // Single active connection: if we already have a peer, drop the new
      // one. By the time this fires for the 2nd peer the 1st is established,
      // so we kick the newcomer rather than disrupt the existing link.
      if (s_connected && desc->conn_handle != s_connHandle) {
        ble_gap_terminate(desc->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
      }
      s_connected  = true;
      s_connHandle = desc->conn_handle;
      s_selectStartedMs = 0;
      s_currentAdv = ADV_IDLE;   // NimBLE auto-stops adv on connect
    }
    void onDisconnect(NimBLEServer*, ble_gap_conn_desc*) override {
      s_connected = false;
      s_connHandle = 0xFFFF;
      // Don't touch the radio here. tick() will see s_connected=false and
      // converge advertising back to whatever state matches the selection.
    }
  };
  ServerCB s_cb;

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

// Install the connectable HID advertisement payload + intervals. Caller
// drives conn_mode (DIR vs UND) and start params; this only fills payload.
static void installHidAdvData() {
  if (!s_init || !s_hid) return;
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (!adv) return;

  NimBLEAdvertisementData d;
  d.setFlags(0x06);
  d.setName(s_name);
  d.setAppearance(0x03C1);
  d.setCompleteServices(s_hid->hidService()->getUUID());
  adv->setAdvertisementData(d);

  NimBLEAdvertisementData sr;
  sr.setName(s_name);
  adv->setScanResponseData(sr);

  adv->setAppearance(0x03C1);
  adv->setName(s_name);
  adv->setMinInterval(0x30);
  adv->setMaxInterval(0x60);
  adv->setScanFilter(false, false);
}

// ---------- bond name + selection persistence ----------
namespace {
  // NVS key from address: strip colons -> 12 hex chars (fits 15-char limit).
  void addrKey(const char* addr, char out[16]) {
    int j = 0;
    for (int k = 0; addr[k] && j < 15; ++k) if (addr[k] != ':') out[j++] = addr[k];
    out[j] = 0;
  }

  // Selected-host persistence: namespace "blesel", key "sel" = full MAC
  // string. Survives reboot so battery swaps don't lose the active host.
  void persistSelected() {
    Preferences p;
    if (!p.begin("blesel", false)) return;
    if (s_hasSelection) p.putString("sel", s_selectedAddr.toString().c_str());
    else                p.remove("sel");
    p.end();
  }
  void loadSelected() {
    Preferences p;
    if (!p.begin("blesel", true)) return;
    String v = p.getString("sel", "");
    p.end();
    if (v.length() == 0) { s_hasSelection = false; return; }
    // NimBLEAddress(string) accepts xx:xx:xx:xx:xx:xx.
    NimBLEAddress a(std::string(v.c_str()));
    // Only honor the saved selection if the bond still exists.
    int n = NimBLEDevice::getNumBonds();
    for (int i = 0; i < n; ++i) {
      if (NimBLEDevice::getBondedAddress(i) == a) {
        s_selectedAddr = a;
        s_hasSelection = true;
        return;
      }
    }
    s_hasSelection = false;
  }
}

// ---------- advertising state machine ----------
namespace {
  AdvMode desiredAdvMode() {
    if (!s_init || s_suspended) return ADV_IDLE;
    if (s_connected) return ADV_IDLE;
    if (ble_radio::current() != ble_radio::MODE_HID &&
        ble_radio::current() != ble_radio::MODE_NONE) return ADV_IDLE;
    if (s_pairNew) return ADV_OPEN;
    if (s_hasSelection) return ADV_WHITELIST;
    return ADV_IDLE;
  }

  // Push the selected peer (or none) into the controller's whitelist.
  // ble_gap_wl_set replaces the entire list, so calling with count=0 clears.
  void applyWhitelist() {
    if (s_hasSelection) {
      ble_addr_t a;
      memcpy(a.val, s_selectedAddr.getNative(), 6);
      a.type = s_selectedAddr.getType();
      int rc = ble_gap_wl_set(&a, 1);
      Serial.printf("[hid] wl set %s type=%u -> %d\n",
                    s_selectedAddr.toString().c_str(),
                    (unsigned)a.type, rc);
    } else {
      ble_gap_wl_set(nullptr, 0);
    }
  }

  void applyAdvState() {
    if (!s_init) return;
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv) return;

    AdvMode want = desiredAdvMode();
    if (want == s_currentAdv && want == ADV_IDLE) return;

    if (ble_gap_adv_active()) adv->stop();
    if (want == ADV_IDLE) { s_currentAdv = ADV_IDLE; return; }

    installHidAdvData();
    adv->setAdvertisementType(BLE_GAP_CONN_MODE_UND);
    if (want == ADV_WHITELIST) {
      applyWhitelist();
      // setScanFilter(scanReqWl, connReqWl): only the whitelisted peer can
      // open a connection. Scanners still see the device, so HID auto-reconnect
      // on the host side picks it up normally.
      adv->setScanFilter(false, true);
    } else {  // ADV_OPEN (PAIR NEW)
      ble_gap_wl_set(nullptr, 0);
      adv->setScanFilter(false, false);
    }
    bool ok = adv->start();
    Serial.printf("[hid] adv %s -> %d\n",
                  want == ADV_WHITELIST ? "WL" : "OPEN", (int)ok);
    s_currentAdv = want;
  }
}

// Called by ble_radio when the radio returns to HID (e.g. ble_spam::stop).
// Don't call NimBLE adv APIs synchronously from arbitrary contexts; just
// trigger a reconverge on the next tick.
static void radioRestoreHid() {
  if (!s_init) return;
  s_currentAdv = ADV_IDLE;   // force tick() to re-emit
}

void begin(const char* name) {
  if (s_init) return;
  if (name) { strncpy(s_name, name, sizeof(s_name) - 1); s_name[sizeof(s_name) - 1] = 0; }
  NimBLEDevice::init(s_name);
  // bond=yes, MITM=no, SC=no. SC=yes rejected BlueZ peers that fell back to
  // LE Legacy → auth-failure reconnect loops. Letting SMP negotiate the mode
  // is broader-compat without losing bonding.
  NimBLEDevice::setSecurityAuth(true, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(&s_cb);
  // Drive re-advertising from tick(), not from inside the disconnect handler.
  // NimBLE's built-in auto-restart races with our state machine.
  s_server->advertiseOnDisconnect(false);

  s_hid = new NimBLEHIDDevice(s_server);
  s_hid->manufacturer()->setValue("smartwatch");
  s_hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  s_hid->hidInfo(0x00, 0x01);
  s_hid->reportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  s_input      = s_hid->inputReport(1);
  s_kbInput    = s_hid->inputReport(2);
  s_mouseInput = s_hid->inputReport(3);
  ble_ota::begin(s_server);
  s_hid->startServices();

  s_init = true;
  ble_radio::init();
  ble_radio::setHidRestore(&radioRestoreHid);
  ble_radio::acquire(ble_radio::MODE_HID);
  installHidAdvData();
  dumpBonds("boot");

  // Restore the last selected host. If the bond still exists, start the
  // status timer so the UI shows "no response yet" after SELECT_TIMEOUT_MS.
  loadSelected();
  if (s_hasSelection) {
    s_selectStartedMs = millis();
    Serial.printf("[hid] restoring selection %s\n",
                  s_selectedAddr.toString().c_str());
  }
  applyAdvState();
}

void tick() {
  if (!s_init || !s_server) return;
  applyAdvState();
}

bool connected() { return s_connected; }

void startAdvertising() {
  // Public hook is kept for backward compat with screen_pair's PAIR NEW
  // button. Internally we just request a state-machine reconverge.
  if (!s_init) return;
  s_currentAdv = ADV_IDLE;
  applyAdvState();
}
void stopAdvertising() {
  if (!s_init) return;
  if (ble_gap_adv_active()) NimBLEDevice::stopAdvertising();
  s_currentAdv = ADV_IDLE;
}

void suspend() {
  if (!s_init) return;
  s_suspended = true;
  if (ble_gap_adv_active()) NimBLEDevice::stopAdvertising();
  s_currentAdv = ADV_IDLE;
  if (s_server) {
    for (auto h : s_server->getPeerDevices()) s_server->disconnect(h);
  }
  // Yield so NimBLE host can actually process the disconnect.
  vTaskDelay(pdMS_TO_TICKS(2));
}

void resume() {
  if (!s_init) return;
  s_suspended = false;
  ble_radio::acquire(ble_radio::MODE_HID);
  // Reset the status timer so the UI shows "still trying" again rather than
  // an immediately-failed state after spam returns the radio.
  if (s_hasSelection && !s_connected) s_selectStartedMs = millis();
  applyAdvState();
}

namespace {
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
  NimBLEConnInfo info = s_server->getPeerInfo(peers[0]);
  std::string a = info.getAddress().toString();
  strncpy(s_addrBuf, a.c_str(), sizeof(s_addrBuf) - 1);
  s_addrBuf[sizeof(s_addrBuf) - 1] = 0;
  return s_addrBuf;
}

// Retained for API compatibility with screen_pair. In the new model there is
// no blacklist — single-selected-host means only the selected peer can
// connect (via directed adv) or anyone can (pair-new). Always false.
bool peerBlacklisted() { return false; }

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
  if (s_server) {
    for (auto h : s_server->getPeerDevices()) {
      NimBLEConnInfo info = s_server->getPeerInfo(h);
      if (info.getAddress() == a || info.getIdAddress() == a) {
        s_server->disconnect(h);
      }
    }
  }
  // If this was the selected host, clear selection.
  if (s_hasSelection && s_selectedAddr == a) {
    s_hasSelection = false;
    s_selectStartedMs = 0;
    persistSelected();
  }
  NimBLEDevice::deleteBond(a);
  dumpBonds("forget");
  s_currentAdv = ADV_IDLE;
  applyAdvState();
}

void forgetAll() {
  if (!s_init) return;
  if (s_server) {
    for (auto h : s_server->getPeerDevices()) s_server->disconnect(h);
  }
  NimBLEDevice::deleteAllBonds();
  s_hasSelection = false;
  s_selectStartedMs = 0;
  s_pairNew = false;
  persistSelected();
  dumpBonds("forgetAll");
  s_currentAdv = ADV_IDLE;
  applyAdvState();
}

// ---------- selection API ----------
// "Selected" bond = the one host the watch is currently advertising to with
// a connection-filter whitelist. At most one at a time.

bool isSelected(uint8_t i) {
  if (!s_init || i >= numBonds() || !s_hasSelection) return false;
  return NimBLEDevice::getBondedAddress(i) == s_selectedAddr;
}

void selectBond(uint8_t i) {
  if (!s_init || i >= numBonds()) return;
  NimBLEAddress a = NimBLEDevice::getBondedAddress(i);

  // If currently connected to a different peer, hang up so the new
  // whitelist takes effect cleanly.
  if (s_connected && s_server) {
    for (auto h : s_server->getPeerDevices()) {
      NimBLEConnInfo info = s_server->getPeerInfo(h);
      if (!(info.getAddress() == a || info.getIdAddress() == a)) {
        s_server->disconnect(h);
      }
    }
  }

  s_selectedAddr = a;
  s_hasSelection = true;
  s_pairNew = false;
  s_selectStartedMs = millis();
  persistSelected();
  s_currentAdv = ADV_IDLE;   // force re-apply with the new whitelist
  applyAdvState();
}

void deselectBond(uint8_t i) {
  if (!s_init || i >= numBonds()) return;
  NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
  if (!s_hasSelection || !(s_selectedAddr == a)) return;
  s_hasSelection = false;
  s_selectStartedMs = 0;
  persistSelected();
  // If currently connected to this peer, leave the link alone — the user
  // unchecking a row means "stop trying", not "kick the active host".
  s_currentAdv = ADV_IDLE;
  applyAdvState();
}

bool pairNewMode() { return s_pairNew; }

bool reconnecting() {
  if (!s_hasSelection || s_connected) return false;
  uint32_t elapsed = millis() - s_selectStartedMs;
  return elapsed < SELECT_TIMEOUT_MS;
}

bool reconnectFailed() {
  if (!s_hasSelection || s_connected) return false;
  return (millis() - s_selectStartedMs) >= SELECT_TIMEOUT_MS;
}

void retrySelection() {
  if (!s_init || !s_hasSelection) return;
  s_selectStartedMs = millis();
  s_currentAdv = ADV_IDLE;
  applyAdvState();
}

void exitPairNewMode() {
  if (!s_init) return;
  s_pairNew = false;
  s_currentAdv = ADV_IDLE;
  applyAdvState();
}

// PAIR NEW: open advertising (no whitelist filter), accept any peer. Clears
// selection so a fresh peer doesn't get blocked by the connection filter.
void clearSelection() {
  if (!s_init) return;
  // If currently connected to a known bond, hang up — the user wants a
  // fresh pairing and the existing host would otherwise lock the radio.
  if (s_connected && s_server) {
    for (auto h : s_server->getPeerDevices()) s_server->disconnect(h);
  }
  s_pairNew = true;
  s_hasSelection = false;
  s_selectStartedMs = 0;
  persistSelected();
  s_currentAdv = ADV_IDLE;
  applyAdvState();
}

// ---------- HID input methods ----------
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
  s_pending = Pending::Mouse;
}
void mouseRelease() {
  if (s_pending == Pending::Mouse) flushRelease();
}

} // namespace ble_hid
