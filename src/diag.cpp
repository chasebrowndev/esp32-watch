// Silicon diagnostic — FLASH THIS FIRST.
//   pio run -e diag -t upload && pio device monitor
//
// Reports chip / PSRAM / flash, scans the touch I2C bus, and reads the LCD
// panel ID. Use the output to set PANEL_* / TOUCH_* in include/config.h before
// building the watch firmware. The two known revisions of this board are:
//   - ILI9341V panel + FT6336G touch (I2C 0x38)   [lcdwiki ES3C28P]
//   - ST7789T3 panel + CST816D touch (I2C 0x15)    [community ESPHome]
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "board_pins.h"

static void scanTouchBus() {
  Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);
  // pulse touch reset so the controller is awake for the scan
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);  delay(10);
  digitalWrite(PIN_TP_RST, HIGH); delay(50);

  Serial.println(F("\n-- I2C scan (touch bus) --"));
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device @ 0x%02X", a);
      if (a == TP_ADDR_FT6336) Serial.print(F("  <- FT6336G  => use PANEL_ILI9341 + TOUCH_FT6336"));
      if (a == TP_ADDR_CST816) Serial.print(F("  <- CST816D   => use PANEL_ST7789  + TOUCH_CST816"));
      Serial.println();
      found++;
    }
  }
  if (!found) Serial.println(F("  (none found — check wiring / RST pin)"));
}

static void readPanelId() {
  // Best-effort SPI read of the LCD ID register (cmd 0x04, RDDID).
  // ILI9341 typically -> 0x00 0x93 0x41 ; ST7789 -> different signature.
  SPI.begin(PIN_LCD_SCK, PIN_LCD_MISO, PIN_LCD_MOSI, PIN_LCD_CS);
  pinMode(PIN_LCD_CS, OUTPUT);
  pinMode(PIN_LCD_DC, OUTPUT);
  digitalWrite(PIN_LCD_CS, HIGH);

  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_LCD_CS, LOW);
  digitalWrite(PIN_LCD_DC, LOW);   // command
  SPI.transfer(0x04);
  digitalWrite(PIN_LCD_DC, HIGH);  // data/read
  uint8_t id[4];
  SPI.transfer(0x00);              // dummy
  for (int i = 0; i < 3; i++) id[i] = SPI.transfer(0x00);
  digitalWrite(PIN_LCD_CS, HIGH);
  SPI.endTransaction();

  Serial.println(F("\n-- LCD panel ID (cmd 0x04) --"));
  Serial.printf("  raw: %02X %02X %02X\n", id[0], id[1], id[2]);
  if (id[1] == 0x93 && id[2] == 0x41) Serial.println(F("  looks like ILI9341"));
  else Serial.println(F("  not an ILI9341 signature — likely ST7789 (trust the I2C touch result)"));
}

static void report() {
  Serial.println(F("\n================ SMARTWATCH DIAG ================"));
  Serial.printf("Chip      : %s rev %d, %d core(s)\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("CPU freq  : %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("Flash     : %u bytes (%.0f MB)\n",
                ESP.getFlashChipSize(), ESP.getFlashChipSize() / 1048576.0);
  size_t psram = ESP.getPsramSize();
  Serial.printf("PSRAM     : %u bytes (%s)\n", psram,
                psram ? "present -> keep BOARD_HAS_PSRAM" : "NONE -> drop OPI PSRAM in watch env");
  Serial.printf("Free heap : %u bytes\n", ESP.getFreeHeap());

  scanTouchBus();
  readPanelId();

  Serial.println(F("\n=> Update include/config.h PANEL_*/TOUCH_* to match, then build env:watch"));
  Serial.println(F("================================================\n"));
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(300);
}

void loop() { report(); delay(3000); }
