#pragma once
#include <stdint.h>

// The mic runs at 44.1 kHz; pairs of samples are averaged to 22.05 kHz,
// which is plenty for notes up to ~600 Hz and halves the work.
constexpr int AUDIO_FS        = 22050;
constexpr int AUDIO_WINDOW    = 2048;   // samples analysed per frame (~93 ms)
constexpr int AUDIO_HOP       = 1024;   // new samples per frame (~46 ms)
constexpr int AUDIO_WINDOW_MS = AUDIO_WINDOW * 1000 / AUDIO_FS;

struct AudioFrame {
  float    dbfs;     // loudness: 0 = loudest possible, more negative = quieter
  float    freq;     // detected pitch in Hz, 0 = none
  float    clarity;  // 0..1, how clean the pitch is
  uint32_t t;        // millis() when the frame's last sample arrived
};

// Start the mic and a background task (core 0) that analyses audio
// continuously, so the main loop never waits for it.
void audioInit();

// Pitch search range; set from the current tuning.
void audioSetRange(float minHz, float maxHz);

// Get the newest frame, waiting up to waitMs. False if none arrived.
bool audioGetFrame(AudioFrame& out, uint32_t waitMs);
