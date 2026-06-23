// BLE HID remote: a Consumer-Control device the phone / smart TV pairs with.
// Sends media + navigation keys. Shares the radio with ble_spam — only one of
// the two should advertise at a time (ble_spam pauses HID advertising).
#pragma once
#include <stdbool.h>
#include <stdint.h>

namespace ble_hid {
  void begin(const char* name);   // init NimBLE HID + start connectable advertising
  void tick();                    // housekeeping (re-advertise on disconnect)
  bool connected();

  void startAdvertising();
  void stopAdvertising();
  // Disconnect all peers and block auto-readvertise so another subsystem
  // (e.g. ble_spam) can own the radio. Pair with resume().
  void suspend();
  void resume();

  // Bond management — used by the Pair screen.
  // Strings are owned by ble_hid (single internal buffer); copy before next call.
  const char* localAddr();             // this watch's own BD address
  const char* peerAddr();              // currently connected peer ("" if none)
  bool        peerBlacklisted();       // true if a connected peer is being kicked
  uint8_t     numBonds();
  const char* bondAddr(uint8_t i);     // "" on out-of-range
  const char* bondName(uint8_t i);     // user-assigned label, "" if unset
  void        setBondName(uint8_t i, const char* name);
  void        forget(uint8_t i);
  void        forgetAll();

  // Bond gating (blacklist model): bonds default to "checked" = allowed to
  // connect. Toggling a bond off blacklists it — any incoming connection
  // matching its identity address is dropped by the tick() watchdog. PAIR
  // NEW blacklists every existing bond so a fresh device can pair without
  // an old phone racing in first. State persists in NVS.
  bool        isSelected(uint8_t i);          // true = checked / allowed
  void        selectBond(uint8_t i);          // check (un-blacklist) bond[i]
  void        deselectBond(uint8_t i);        // uncheck (blacklist) bond[i]
  void        clearSelection();               // PAIR NEW: blacklist ALL bonds
  bool        pairNewMode();                  // true when every bond is blacklisted
  void        exitPairNewMode();              // un-blacklist all bonds

  // Media + nav actions (press+release one report).
  void playPause();
  void next();
  void prev();
  void volUp();
  void volDown();
  void mute();
  void home();
  void back();

  // Keyboard nav (separate HID report). Useful for Android TV / Apple TV nav.
  void up();
  void down();
  void left();
  void right();
  void select();   // Enter
  void escape();

  // Trackpad / mouse. Button bits: 1=left, 2=right, 4=middle.
  void mouseMove(int8_t dx, int8_t dy);
  void mouseScroll(int8_t wheel);
  void mouseClick(uint8_t button);    // press + auto-release ~20 ms later
  void mousePress(uint8_t button);    // press; caller must call mouseRelease()
  void mouseRelease();
}
