#pragma once
#include <stdint.h>
#include <math.h>

// One string: its note name and MIDI note number (A4 = 69 = 440 Hz).
struct Note {
  const char* name;
  uint8_t     midi;
};

// A tuning: notes listed from the thickest string (highest string number)
// to the thinnest (string 1). String number of notes[i] = count - i.
struct Tuning {
  const char* name;
  uint8_t     count;
  Note        notes[6];
};

struct Instrument {
  const char*   name;
  const Tuning* tunings;
  uint8_t       count;
};

extern const Instrument INSTRUMENTS[];
extern const uint8_t    INSTRUMENT_COUNT;

inline float noteFreq(const Note& n) {
  return 440.0f * powf(2.0f, (n.midi - 69) / 12.0f);
}
