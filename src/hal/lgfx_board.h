// LovyanGFX device definition for the Hosyond ESP32-S3 2.8" board.
// Panel + touch are selected by PANEL_*/TOUCH_* in include/config.h, which the
// diag build tells you how to set. Both known revisions are handled here.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"
#include "board_pins.h"

class LGFX_Hosyond : public lgfx::LGFX_Device {
#if defined(PANEL_ST7789)
  lgfx::Panel_ST7789  _panel;
#else
  lgfx::Panel_ILI9341 _panel;
#endif
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
#if defined(TOUCH_CST816)
  lgfx::Touch_CST816S _touch;
#else
  lgfx::Touch_FT5x06  _touch;   // FT6336G is FT5x06-protocol compatible
#endif

public:
  LGFX_Hosyond() {
    { auto c = _bus.config();
      c.spi_host    = SPI2_HOST;     // FSPI on ESP32-S3
      c.spi_mode    = 0;
      c.freq_write  = 40000000;
      c.freq_read   = 16000000;
      c.spi_3wire   = false;
      c.use_lock    = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk    = PIN_LCD_SCK;
      c.pin_mosi    = PIN_LCD_MOSI;
      c.pin_miso    = PIN_LCD_MISO;
      c.pin_dc      = PIN_LCD_DC;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    { auto c = _panel.config();
      c.pin_cs         = PIN_LCD_CS;
      c.pin_rst        = PIN_LCD_RST;   // -1, tied to chip reset
      c.pin_busy       = -1;
      c.panel_width    = LCD_WIDTH;
      c.panel_height   = LCD_HEIGHT;
      c.offset_x       = 0;
      c.offset_y       = 0;
      c.offset_rotation= 0;
      c.readable       = true;
      c.invert         = true;    // this ILI9341 unit boots inverted (black<->white); INVON corrects it
      c.rgb_order      = false;
      c.dlen_16bit     = false;
      c.bus_shared     = true;
      _panel.config(c);
    }
    { auto c = _light.config();
      c.pin_bl      = PIN_LCD_BL;
      c.invert      = false;
      c.freq        = 12000;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    { auto c = _touch.config();
      c.x_min   = 0;   c.x_max = LCD_WIDTH  - 1;
      c.y_min   = 0;   c.y_max = LCD_HEIGHT - 1;
      c.pin_int = -1;          // poll touch-count reg directly; FT6336 INT gating
                               // (driver _flg_released on GPIO17) was suppressing all reads
      c.pin_rst = PIN_TP_RST;
      c.bus_shared = false;
      c.offset_rotation = 2;   // touch X mirrored vs display
      c.i2c_port = 0;
      c.pin_sda  = PIN_TP_SDA;
      c.pin_scl  = PIN_TP_SCL;
      c.freq     = 400000;
#if defined(TOUCH_CST816)
      c.i2c_addr = TP_ADDR_CST816;
#else
      c.i2c_addr = TP_ADDR_FT6336;
#endif
      _touch.config(c);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
