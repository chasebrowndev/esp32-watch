#include "ui_theme.h"
#include "ui.h"
#include <Preferences.h>

namespace {
  const theme::Palette PALETTES[] = {
    // name           accent     bg         fg         dim        panel
    { "amber-crt",   0xFFB000, 0x080300, 0xFFE4A8, 0x805020, 0x1A0E00 },
    { "blood",       0xFF0030, 0x100000, 0xFFC4C4, 0x803030, 0x2A0408 },
    { "matrix",      0x00FF66, 0x000000, 0xC8FFD8, 0x208030, 0x002808 },
    { "abyss",       0xFFFFFF, 0x000000, 0xFFFFFF, 0x808080, 0x181818 },
    { "radiance",    0x000000, 0xFFFFFF, 0x000000, 0x707070, 0xE0E0E0 },
  };
  constexpr uint8_t N = sizeof(PALETTES) / sizeof(PALETTES[0]);
  uint8_t s_cur = 0;
}

namespace theme {

uint8_t        count()           { return N; }
const Palette& at(uint8_t i)     { return PALETTES[i < N ? i : 0]; }
const Palette& cur()             { return PALETTES[s_cur]; }
uint8_t        current()         { return s_cur; }

void init() {
  Preferences p;
  if (p.begin("theme", true)) {
    uint8_t v = p.getUChar("idx", 0);
    if (v < N) s_cur = v;
    p.end();
  }
}

void setCurrent(uint8_t i) {
  if (i >= N || i == s_cur) return;
  s_cur = i;
  Preferences p;
  if (p.begin("theme", false)) { p.putUChar("idx", s_cur); p.end(); }
  ui::rebuildFace();
}

} // namespace theme
