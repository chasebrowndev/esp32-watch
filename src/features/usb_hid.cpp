#include "usb_hid.h"
#include "usb_msc.h"
#include "../../include/config.h"
#include <Arduino.h>

#if FEAT_USB_HID
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <SD_MMC.h>

// Declared at global scope so TinyUSB registers it before USB.begin() is called
// (which happens inside Serial.begin() in main.cpp setup()).
static USBHIDKeyboard s_kb;

namespace {
  bool     s_active        = false;
  File     s_file;
  uint32_t s_delay_until   = 0;
  uint32_t s_def_delay     = 0;
  uint32_t s_release_at    = 0;   // deferred releaseAll() deadline; 0 = none
  char     s_name[64]      = {};
  unsigned s_cur_line      = 0;
  unsigned s_total_lines   = 0;

  // Reads the next non-empty, non-whitespace-only line into buf. Returns false
  // when the file is exhausted.
  bool nextLine(char* buf, size_t cap) {
    while (s_file.available()) {
      size_t n = 0;
      while (s_file.available() && n < cap - 1) {
        char c = (char)s_file.read();
        if (c == '\n') break;
        if (c != '\r') buf[n++] = c;
      }
      buf[n] = '\0';
      if (n > 0) return true;
    }
    return false;
  }

  // Map a DuckyScript token to a USB HID keycode (Arduino USB HID constants).
  uint8_t specialKey(const char* t) {
    if (!strcmp(t, "ENTER"))                             return KEY_RETURN;
    if (!strcmp(t, "TAB"))                               return KEY_TAB;
    if (!strcmp(t, "SPACE"))                             return ' ';
    if (!strcmp(t, "BACKSPACE"))                         return KEY_BACKSPACE;
    if (!strcmp(t, "DELETE") || !strcmp(t, "DEL"))       return KEY_DELETE;
    if (!strcmp(t, "ESCAPE") || !strcmp(t, "ESC"))       return KEY_ESC;
    if (!strcmp(t, "UP"))                                return KEY_UP_ARROW;
    if (!strcmp(t, "DOWN"))                              return KEY_DOWN_ARROW;
    if (!strcmp(t, "LEFT"))                              return KEY_LEFT_ARROW;
    if (!strcmp(t, "RIGHT"))                             return KEY_RIGHT_ARROW;
    if (!strcmp(t, "HOME"))                              return KEY_HOME;
    if (!strcmp(t, "END"))                               return KEY_END;
    if (!strcmp(t, "PAGEUP"))                            return KEY_PAGE_UP;
    if (!strcmp(t, "PAGEDOWN"))                          return KEY_PAGE_DOWN;
    if (!strcmp(t, "INSERT"))                            return KEY_INSERT;
    if (!strcmp(t, "CAPSLOCK"))                          return KEY_CAPS_LOCK;
    if (!strcmp(t, "F1"))  return KEY_F1;
    if (!strcmp(t, "F2"))  return KEY_F2;
    if (!strcmp(t, "F3"))  return KEY_F3;
    if (!strcmp(t, "F4"))  return KEY_F4;
    if (!strcmp(t, "F5"))  return KEY_F5;
    if (!strcmp(t, "F6"))  return KEY_F6;
    if (!strcmp(t, "F7"))  return KEY_F7;
    if (!strcmp(t, "F8"))  return KEY_F8;
    if (!strcmp(t, "F9"))  return KEY_F9;
    if (!strcmp(t, "F10")) return KEY_F10;
    if (!strcmp(t, "F11")) return KEY_F11;
    if (!strcmp(t, "F12")) return KEY_F12;
    if (t[1] == '\0')      return (uint8_t)t[0];
    return 0;
  }

  void execLine(const char* line) {
    if (!line[0] || strncmp(line, "REM", 3) == 0) return;

    if (strncmp(line, "DELAY ", 6) == 0) {
      s_delay_until = millis() + (uint32_t)atoi(line + 6);
      return;
    }
    if (strncmp(line, "DEFAULTDELAY ", 13) == 0) {
      s_def_delay = (uint32_t)atoi(line + 13); return;
    }
    if (strncmp(line, "DEFAULT_DELAY ", 14) == 0) {
      s_def_delay = (uint32_t)atoi(line + 14); return;
    }
    if (strncmp(line, "STRING ", 7) == 0) {
      s_kb.print(line + 7);
      if (s_def_delay) s_delay_until = millis() + s_def_delay;
      return;
    }
    if (strncmp(line, "STRINGLN ", 9) == 0) {
      s_kb.println(line + 9);
      if (s_def_delay) s_delay_until = millis() + s_def_delay;
      return;
    }

    // Key combo: any combination of [CTRL][SHIFT][ALT][GUI] + one key token.
    char buf[128];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    uint8_t mods[4] = {};
    uint8_t key     = 0;
    int     nm      = 0;

    for (char* tok = strtok(buf, " "); tok; tok = strtok(nullptr, " ")) {
      uint8_t mod = 0;
      if (!strcmp(tok, "CTRL") || !strcmp(tok, "CONTROL")) mod = KEY_LEFT_CTRL;
      else if (!strcmp(tok, "SHIFT"))                       mod = KEY_LEFT_SHIFT;
      else if (!strcmp(tok, "ALT"))                         mod = KEY_LEFT_ALT;
      else if (!strcmp(tok, "GUI") || !strcmp(tok, "WINDOWS") ||
               !strcmp(tok, "COMMAND"))                     mod = KEY_LEFT_GUI;

      if (mod) { if (nm < 4) mods[nm++] = mod; }
      else      { uint8_t k = specialKey(tok); if (k) key = k; }
    }

    for (int i = 0; i < nm; ++i) s_kb.press(mods[i]);
    if (key) s_kb.press(key);
    // Defer releaseAll() to a future tick so we don't block the LVGL loop.
    // Next-line execution is gated behind s_delay_until, which we set to
    // STRICTLY AFTER the release deadline so the release always fires
    // before the next combo can press its modifiers (otherwise CTRL bleeds
    // from one shortcut into the next, dropping Tab into Ctrl-Tab etc.).
    s_release_at  = millis() + 15;
    s_delay_until = s_release_at + 5 + s_def_delay;
  }
}

namespace usb_hid {

void begin() {
  s_kb.begin();
  Serial.println(F("[usb_hid] keyboard ready"));
}

void tick() {
  if (!s_active) return;
  // Flush any pending key release before reading the next line.
  if (s_release_at && millis() >= s_release_at) {
    s_kb.releaseAll();
    s_release_at = 0;
  }
  if (millis() < s_delay_until) return;

  char line[256];
  if (!nextLine(line, sizeof(line))) {
    s_file.close();
    s_active     = false;
    s_name[0]    = '\0';
    s_cur_line   = 0;
    s_total_lines = 0;
    Serial.println(F("[usb_hid] script done"));
    return;
  }
  s_cur_line++;
  execLine(line);
}

bool        active()      { return s_active; }
const char* scriptName()  { return s_name; }
unsigned    currentLine() { return s_cur_line; }
unsigned    totalLines()  { return s_total_lines; }

void runScript(const char* sdPath) {
  if (usb_msc::enabled()) {
    Serial.println(F("[usb_hid] blocked: MSC session active"));
    return;
  }
  if (s_active) { s_kb.releaseAll(); s_file.close(); s_active = false; s_release_at = 0; }

  s_file = SD_MMC.open(sdPath, "r");
  if (!s_file) {
    Serial.printf("[usb_hid] open failed: %s\n", sdPath);
    return;
  }

  // Pre-scan to count non-empty lines (same filter as nextLine) for progress
  // display. Cheap — ducky scripts are typically well under 4 KB. Then seek
  // back to 0 for execution.
  s_total_lines = 0;
  s_cur_line    = 0;
  {
    size_t n = 0;
    bool   seen = false;
    while (s_file.available()) {
      char c = (char)s_file.read();
      if (c == '\n') {
        if (seen) s_total_lines++;
        seen = false;
        n = 0;
      } else if (c != '\r') {
        seen = true;
        n++;
      }
    }
    if (seen) s_total_lines++;
    s_file.seek(0);
  }
  s_def_delay   = 0;
  s_delay_until = millis() + 500;   // 500 ms grace so user can move their hand
  s_active      = true;

  const char* slash = strrchr(sdPath, '/');
  strncpy(s_name, slash ? slash + 1 : sdPath, sizeof(s_name) - 1);
  s_name[sizeof(s_name) - 1] = '\0';
  Serial.printf("[usb_hid] running: %s\n", s_name);
}

void cancel() {
  if (!s_active) return;
  s_kb.releaseAll();
  s_file.close();
  s_active      = false;
  s_release_at  = 0;
  s_name[0]     = '\0';
  s_cur_line    = 0;
  s_total_lines = 0;
  Serial.println(F("[usb_hid] cancelled"));
}

} // namespace usb_hid

#else  // FEAT_USB_HID disabled stub

namespace usb_hid {
  void begin()              {}
  void tick()               {}
  bool active()             { return false; }
  const char* scriptName()  { return ""; }
  void runScript(const char*) {}
  void cancel()             {}
  unsigned currentLine()    { return 0; }
  unsigned totalLines()     { return 0; }
}

#endif
