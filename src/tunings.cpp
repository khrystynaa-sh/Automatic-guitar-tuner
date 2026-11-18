#include "tunings.h"

#define COUNT(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

// MIDI numbers: C4 = 60, each semitone +1, each octave +12.

static const Tuning GUITAR[] = {
  {"Standard",     6, {{"E2", 40}, {"A2", 45}, {"D3", 50}, {"G3", 55}, {"B3", 59}, {"E4", 64}}},
  {"Drop D",       6, {{"D2", 38}, {"A2", 45}, {"D3", 50}, {"G3", 55}, {"B3", 59}, {"E4", 64}}},
  {"Half-step dn", 6, {{"Eb2", 39}, {"Ab2", 44}, {"Db3", 49}, {"Gb3", 54}, {"Bb3", 58}, {"Eb4", 63}}},
  {"Open G",       6, {{"D2", 38}, {"G2", 43}, {"D3", 50}, {"G3", 55}, {"B3", 59}, {"D4", 62}}},
  {"DADGAD",       6, {{"D2", 38}, {"A2", 45}, {"D3", 50}, {"G3", 55}, {"A3", 57}, {"D4", 62}}},
};

static const Tuning BASS[] = {
  {"Standard",     4, {{"E1", 28}, {"A1", 33}, {"D2", 38}, {"G2", 43}}},
  {"Drop D",       4, {{"D1", 26}, {"A1", 33}, {"D2", 38}, {"G2", 43}}},
  {"Half-step dn", 4, {{"Eb1", 27}, {"Ab1", 32}, {"Db2", 37}, {"Gb2", 42}}},
};

// Ukulele standard is re-entrant: string 4 (G4) is higher than string 3 (C4).
static const Tuning UKULELE[] = {
  {"Standard",     4, {{"G4", 67}, {"C4", 60}, {"E4", 64}, {"A4", 69}}},
  {"Low G",        4, {{"G3", 55}, {"C4", 60}, {"E4", 64}, {"A4", 69}}},
  {"Baritone",     4, {{"D3", 50}, {"G3", 55}, {"B3", 59}, {"E4", 64}}},
};

const Instrument INSTRUMENTS[] = {
  {"Guitar",  GUITAR,  COUNT(GUITAR)},
  {"Bass",    BASS,    COUNT(BASS)},
  {"Ukulele", UKULELE, COUNT(UKULELE)},
};

const uint8_t INSTRUMENT_COUNT = COUNT(INSTRUMENTS);
