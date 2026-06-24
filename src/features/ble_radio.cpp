#include "ble_radio.h"
#include <Arduino.h>

namespace {
  ble_radio::Mode      s_owner   = ble_radio::MODE_NONE;
  ble_radio::RestoreFn s_hidRestore = nullptr;
}

namespace ble_radio {

void init() { s_owner = MODE_NONE; }

Mode current() { return s_owner; }

void setHidRestore(RestoreFn fn) { s_hidRestore = fn; }

bool acquire(Mode m) {
  if (m == MODE_NONE) return false;
  if (s_owner == m)   return true;
  // The previous owner's teardown happens inside their own start() path
  // before they call acquire(). We just record the new owner here so
  // re-entrant restore() calls see the correct state.
  s_owner = m;
  return true;
}

void release(Mode m) {
  if (s_owner != m) return;
  s_owner = MODE_NONE;
  // Hand back to HID if registered. Mark HID as owner BEFORE calling the
  // restore fn so any acquire(MODE_HID) it makes synchronously is a no-op.
  if (s_hidRestore) {
    s_owner = MODE_HID;
    s_hidRestore();
  }
}

} // namespace ble_radio
