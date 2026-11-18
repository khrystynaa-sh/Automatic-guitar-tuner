#include "audio.h"
#include "pitch.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include "dsps_fft2r.h"

#define MIC_SD   1
#define MIC_SCK  2
#define MIC_WS   41

static const int   I2S_FS     = 44100;
static const int   FFT_N      = 4096;   // >= 2*window, so the autocorrelation doesn't wrap
static const float SILENCE_DB = -75;    // don't analyse below this

// One hop = 2*AUDIO_HOP stereo frames at 44.1 kHz.
static int32_t raw[AUDIO_HOP * 2 * 2];
static float   win[AUDIO_WINDOW];       // sliding window, 22.05 kHz
static float   xd[AUDIO_WINDOW];        // window with the mean removed
static float   fftBuf[FFT_N * 2] __attribute__((aligned(16)));  // interleaved re, im
static float   corr[AUDIO_WINDOW / 2 + 2];
static float   nsdf[AUDIO_WINDOW / 2 + 2];

static volatile float rangeMin = 60, rangeMax = 600;
static QueueHandle_t  frameQueue;
static int            channel = -1;

static void setupI2S() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = I2S_FS;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 512;

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = MIC_SCK;
  pins.ws_io_num    = MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num  = MIC_SD;

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
}

// Read one hop of new audio and slide it into the window.
static void readHop() {
  size_t br;
  i2s_read(I2S_NUM_0, raw, sizeof(raw), &br, portMAX_DELAY);
  const int frames = AUDIO_HOP * 2;

  // The mic only talks on one of the two stereo slots. Pick the one with
  // the most energy, and only switch if the other is clearly stronger.
  float e0 = 0, e1 = 0;
  for (int i = 0; i < frames; i++) {
    float a = (float)(raw[2 * i] >> 8), b = (float)(raw[2 * i + 1] >> 8);
    e0 += a * a;
    e1 += b * b;
  }
  if (channel < 0)                      channel = (e0 >= e1) ? 0 : 1;
  else if (channel == 0 && e1 > 4 * e0) channel = 1;
  else if (channel == 1 && e0 > 4 * e1) channel = 0;

  memmove(win, win + AUDIO_HOP, (AUDIO_WINDOW - AUDIO_HOP) * sizeof(float));
  float* dst = win + AUDIO_WINDOW - AUDIO_HOP;
  for (int i = 0; i < AUDIO_HOP; i++) {
    int32_t s0 = raw[4 * i + channel] >> 8;       // 24-bit sample
    int32_t s1 = raw[4 * i + 2 + channel] >> 8;
    dst[i] = 0.5f * (float)(s0 + s1);             // average pairs -> 22.05 kHz
  }
}

static void analyse(AudioFrame& fr) {
  const int W = AUDIO_WINDOW;
  float mean = 0;
  for (int i = 0; i < W; i++) mean += win[i];
  mean /= W;
  float energy = 0;
  for (int i = 0; i < W; i++) { xd[i] = win[i] - mean; energy += xd[i] * xd[i]; }
  float rms = sqrtf(energy / W);
  fr.dbfs = 20 * log10f(rms / 8388608.0f + 1e-12f);   // 8388608 = 24-bit full scale
  fr.freq = 0;
  fr.clarity = 0;
  if (fr.dbfs < SILENCE_DB) return;

  // Autocorrelation via FFT: r = IFFT(|FFT(x)|^2). Much faster than
  // multiplying the window with every shifted copy of itself.
  for (int i = 0; i < FFT_N; i++) {
    fftBuf[2 * i]     = (i < W) ? xd[i] : 0;
    fftBuf[2 * i + 1] = 0;
  }
  dsps_fft2r_fc32(fftBuf, FFT_N);
  dsps_bit_rev_fc32(fftBuf, FFT_N);
  for (int k = 0; k < FFT_N; k++) {
    float re = fftBuf[2 * k], im = fftBuf[2 * k + 1];
    fftBuf[2 * k]     = re * re + im * im;
    fftBuf[2 * k + 1] = 0;
  }
  // The power spectrum is real and symmetric, so a forward FFT gives the
  // same result as the inverse one, just scaled by FFT_N.
  dsps_fft2r_fc32(fftBuf, FFT_N);
  dsps_bit_rev_fc32(fftBuf, FFT_N);
  for (int t = 0; t < W / 2 + 2; t++) corr[t] = fftBuf[2 * t] / FFT_N;

  fr.freq = mpmPitch(xd, corr, nsdf, W, AUDIO_FS, rangeMin, rangeMax, &fr.clarity);
}

static void audioTask(void*) {
  for (;;) {
    readHop();
    AudioFrame fr;
    analyse(fr);
    fr.t = millis();
    xQueueOverwrite(frameQueue, &fr);   // main loop always gets the newest
  }
}

void audioInit() {
  setupI2S();
  size_t br;
  for (int i = 0; i < 5; i++) i2s_read(I2S_NUM_0, raw, sizeof(raw), &br, portMAX_DELAY);
  dsps_fft2r_init_fc32(NULL, FFT_N);
  frameQueue = xQueueCreate(1, sizeof(AudioFrame));
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, NULL, 1, NULL, 0);
}

void audioSetRange(float minHz, float maxHz) {
  rangeMin = minHz;
  rangeMax = maxHz;
}

bool audioGetFrame(AudioFrame& out, uint32_t waitMs) {
  return xQueueReceive(frameQueue, &out, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}
