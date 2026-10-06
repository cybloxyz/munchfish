#pragma once
#include <stdint.h>

// Tipe global yang dipakai oleh prototype fungsi otomatis Arduino.
struct Note {
  uint16_t f;
  uint16_t ms;
};

enum PomoPhase {
  PH_FOCUS = 0,
  PH_BREAK = 1,
  PH_LONG = 2
};
