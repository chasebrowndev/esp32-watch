// Small helpers for loading config blobs from the microSD card. Every loader
// returns false (leaving the caller's defaults intact) if the card isn't
// mounted, the file is missing, or parsing fails. Writers refuse while MSC
// has exclusive ownership of the card.
#pragma once
#include <ArduinoJson.h>
#include <Arduino.h>

namespace sd_config {
  // True if SD is mounted AND MSC is not currently exporting the card.
  bool available();

  // Load a JSON file from SD into `doc`. Returns true on success.
  bool loadJson(const char* path, JsonDocument& doc);

  // Load a text file (HTML, list, etc.) into `out`. Returns true on success.
  bool loadText(const char* path, String& out);

  // Append a single line to a file on SD (creates the file + parent dir if
  // needed). Returns false when SD is not writable (no card or MSC active).
  bool appendLine(const char* path, const String& line);
}
