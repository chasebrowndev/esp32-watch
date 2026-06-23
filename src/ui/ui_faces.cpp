#include "ui_faces.h"
#include "ui.h"
#include "faces/face_digital.h"
#include "faces/face_analog.h"
#include <Preferences.h>

namespace {
  const faces::Face FACES[] = {
    { "digital",  face_digital::create,  face_digital::tick  },
    { "analog",   face_analog::create,   face_analog::tick   },
  };
  constexpr uint8_t N = sizeof(FACES) / sizeof(FACES[0]);
  uint8_t s_cur = 0;
}

namespace faces {

uint8_t      count()         { return N; }
const Face&  at(uint8_t i)   { return FACES[i < N ? i : 0]; }
const Face&  cur()           { return FACES[s_cur]; }
uint8_t      current()       { return s_cur; }

void init() {
  Preferences p;
  if (p.begin("face", true)) {
    uint8_t v = p.getUChar("idx", 0);
    if (v < N) s_cur = v;
    p.end();
  }
}

void setCurrent(uint8_t i) {
  if (i >= N || i == s_cur) return;
  s_cur = i;
  Preferences p;
  if (p.begin("face", false)) { p.putUChar("idx", s_cur); p.end(); }
  ui::rebuildFace();
}

} // namespace faces
