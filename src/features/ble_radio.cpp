#include "ble_radio.h"
#include <Arduino.h>

namespace {
  ble_radio::Mode      s_owner   = ble_radio::MODE_NONE;
  ble_radio::RestoreFn s_hidRestore = nullptr;
  bool                 s_inTransition = false;   // re-entry guard
}

namespace ble_radio {

void init() { s_owner = MODE_NONE; }

Mode current() { return s_owner; }

void setHidRestore(RestoreFn fn) { s_hidRestore = fn; }

bool acquire(Mode m) {
  if (m == MODE_NONE) return false;
  if (s_owner == m)   return true;
  if (s_inTransition) return false;
  s_inTransition = true;
  // The previous owner's teardown happens inside their own start() path
  // before they call acquire(). We just record the new owner here so
  // re-entrant restore() calls (e.g. HID auto-readvertise on disconnect
  // during a spam start) see the correct state.
  s_owner = m;
  s_inTransition = false;
  return true;
}

void release(Mode m) {
  if (s_owner != m) return;
  if (s_inTransition) return;
  s_inTransition = true;
  s_owner = MODE_NONE;
  s_inTransition = false;
  // Hand back to HID if registered. HID is the default owner.
  if (s_hidRestore) {
    s_inTransition = true;
    s_owner = MODE_HID;
    s_inTransition = false;
    s_hidRestore();
  }
}

} // namespace ble_radio
