// Hosyond ESP32-S3 2.8" 240x320 IPS module (ES3C28P / ES3N28P)
// Pin map from lcdwiki.com/2.8inch_ESP32-S3_Display_E32C28P/E32N28P
// NOTE: this board ships in two silicon revisions under one listing.
//   Panel:  ILI9341V  OR  ST7789T3
//   Touch:  FT6336G (I2C 0x38)  OR  CST816D (I2C 0x15)
// Confirm with the diag build before trusting PANEL_*/TOUCH_* defaults.
#pragma once

// ---- LCD (4-wire SPI) ----
#define PIN_LCD_SCK   12
#define PIN_LCD_MOSI  11
#define PIN_LCD_MISO  13
#define PIN_LCD_CS    10
#define PIN_LCD_DC    46
#define PIN_LCD_RST   -1   // tied to chip EN/reset, no dedicated GPIO
#define PIN_LCD_BL    45   // backlight (PWM-capable)

#define LCD_WIDTH     240
#define LCD_HEIGHT    320

// ---- Capacitive touch (I2C) ----
#define PIN_TP_SDA    16
#define PIN_TP_SCL    15
#define PIN_TP_RST    18
#define PIN_TP_INT    17   // also used as wake source from sleep

#define TP_ADDR_FT6336 0x38
#define TP_ADDR_CST816 0x15

// ---- Onboard peripherals ----
#define PIN_BAT_ADC   9
#define PIN_BTN_BOOT  0
#define PIN_RGB_LED   42

// ---- SD card (SDMMC) ----
#define PIN_SD_CLK    38
#define PIN_SD_CMD    40
#define PIN_SD_D0     39
#define PIN_SD_D1     41
#define PIN_SD_D2     48
#define PIN_SD_D3     47
