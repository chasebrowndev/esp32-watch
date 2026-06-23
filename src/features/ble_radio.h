// Arbiter for the single NimBLEAdvertising / address singleton.
// Every subsystem that touches advertising goes through here so we
// don't end up with spam payloads stuck on the HID adv data, mismatched
// connection modes, or contention with active scanning.
//
// Lifecycle: the owning subsystem calls acquire(MODE_X). Only one owner
// at a time; an existing owner is asked (via the installed restore-fn)
// to release. When done, the owner calls release(MODE_X). The HID layer
// registers its restore fn at boot so any caller can hand the radio back
// to HID without knowing about it.
#pragma once
#include <stdbool.h>
#include <stdint.h>

namespace ble_radio {
  enum Mode : uint8_t {
    MODE_NONE = 0,
    MODE_HID,
    MODE_SPAM,
    MODE_SCAN,
  };

  // Called by ble_hid::begin() once NimBLEDevice::init has run.
  void init();

  Mode current();

  // Try to take the radio for `m`. If a different owner holds it, its
  // restore fn is invoked (typically tearing down its advertising/scan
  // state) and ownership transfers. Always succeeds unless m == MODE_NONE.
  bool acquire(Mode m);

  // Release ownership if and only if `m` is the current owner. Hands the
  // radio back to the default owner (HID) via its registered restore fn.
  void release(Mode m);

  // HID installs a restore fn at boot that re-installs the connectable
  // HID advertisement data + connectable type + intervals.
  using RestoreFn = void(*)();
  void setHidRestore(RestoreFn fn);
}
