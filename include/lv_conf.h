// Minimal LVGL 9 config for the smartwatch. Enabled via -DLV_CONF_INCLUDE_SIMPLE.
#pragma once
#if 1  // set to 0 to disable this config

#define LV_COLOR_DEPTH        16
#define LV_COLOR_16_SWAP      0          // LovyanGFX handles byte order

// ---- Memory ----
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE           (64U * 1024U)
#define LV_MEM_POOL_INCLUDE   <stdlib.h>

// ---- HAL / tick ----
#define LV_USE_OS             LV_OS_NONE
#define LV_TICK_CUSTOM        1
#define LV_TICK_CUSTOM_INCLUDE   "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#define LV_DEF_REFR_PERIOD    16
#define LV_DPI_DEF            130

// ---- Drawing ----
#define LV_DRAW_BUF_ALIGN     4
#define LV_USE_DRAW_SW        1

// ---- Logging ----
#define LV_USE_LOG            1
#define LV_LOG_LEVEL          LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF         0

// ---- Widgets / extras used by the watch UI ----
#define LV_USE_LABEL          1
#define LV_USE_BUTTON         1
#define LV_USE_IMAGE          1
#define LV_USE_ARC            1
#define LV_USE_BAR            1
#define LV_USE_SLIDER         1
#define LV_USE_SWITCH         1
#define LV_USE_CANVAS         1
#define LV_USE_LIST           1
#define LV_USE_ANIMIMG        1

// ---- Fonts ----
#define LV_FONT_MONTSERRAT_8  1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_UNSCII_8      1
#define LV_FONT_UNSCII_16     1
#define LV_FONT_DEFAULT       &lv_font_montserrat_20

// ---- Image decoders (album art is decoded to canvas via TJpg_Decoder) ----
#define LV_USE_GESTURE        1

#endif // config enable
