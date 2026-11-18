#include <Arduino.h>
#include <SPI.h>
#include <driver/i2s.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#define MIC_SD   1
#define MIC_SCK  2
#define MIC_WS   41

#define TFT_SCL  16
#define TFT_SDA  15
#define TFT_RST  7
#define TFT_DC   6
#define TFT_CS   5
#define TFT_BL   4

const int MOTOR_PINS[4] = {10, 11, 12, 13};

const int   FS = 44100;
const int   N  = 2048;
const float TH_ON  = -55;
const float TH_OFF = -65;
const int   CONFIRM_FRAMES = 3;
const int   QUIET_FRAMES   = 5;
const float IN_TUNE_CENTS  = 5;
const int   STEPS_PER_REV  = 4096;
const int   STEP_DELAY_US  = 1200;

const int      DISPLAY_CONFIRM     = 4;
const float    SMOOTH              = 0.3f;
const uint32_t DISPLAY_INTERVAL_MS = 150;

const bool DEBUG = true;

struct GString { int num; const char* name; float freq; };
const GString STRINGS[6] = {
  {6, "E2",  82.41f}, {5, "A2", 110.00f}, {4, "D3", 146.83f},
  {3, "G3", 196.00f}, {2, "B3", 246.94f}, {1, "E4", 329.63f}
};

Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);
static int32_t raw[N * 2];
static float   x[N];

volatile bool motorBusy = false;
TaskHandle_t  motorTaskHandle;

void setupI2S() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = FS;
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

float readAudio() {
  size_t br;
  i2s_read(I2S_NUM_0, raw, sizeof(raw), &br, portMAX_DELAY);

  int32_t p0 = 0, p1 = 0;
  for (int i = 0; i < N; i++) {
    int32_t a = raw[2 * i] >> 8;     if (a < 0) a = -a; if (a > p0) p0 = a;
    int32_t b = raw[2 * i + 1] >> 8; if (b < 0) b = -b; if (b > p1) p1 = b;
  }
  int ch = (p0 >= p1) ? 0 : 1;

  float mean = 0;
  for (int i = 0; i < N; i++) { x[i] = (float)(raw[2 * i + ch] >> 8); mean += x[i]; }
  mean /= N;
  float rms = 0;
  for (int i = 0; i < N; i++) { x[i] -= mean; rms += x[i] * x[i]; }
  rms = sqrtf(rms / N);
  return 20 * log10f(rms / 8388608.0f + 1e-12f);
}

float detectFreq() {
  const int minLag = FS / 1000, maxLag = FS / 70;
  static float r[FS / 70 + 2];
  float best = 0;
  for (int lag = minLag; lag <= maxLag + 1; lag++) {
    float s = 0, a = 0, b = 0;
    for (int i = 0; i < N - lag; i++) {
      s += x[i] * x[i + lag];
      a += x[i] * x[i];
      b += x[i + lag] * x[i + lag];
    }
    r[lag] = (a > 0 && b > 0) ? s / sqrtf(a * b) : 0;
    if (lag <= maxLag && r[lag] > best) best = r[lag];
  }
  if (best < 0.6f) return 0;
  for (int lag = minLag + 1; lag <= maxLag; lag++) {
    if (r[lag] > 0.9f * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) {
      float d = r[lag - 1] - 2 * r[lag] + r[lag + 1];
      float shift = (d != 0) ? 0.5f * (r[lag - 1] - r[lag + 1]) / d : 0;
      return FS / (lag + shift);
    }
  }
  return 0;
}

void motorRelease() { for (int i = 0; i < 4; i++) digitalWrite(MOTOR_PINS[i], LOW); }

void motorTask(void*) {
  const uint8_t SEQ[8][4] = {
    {1,0,0,0},{1,1,0,0},{0,1,0,0},{0,1,1,0},
    {0,0,1,0},{0,0,1,1},{0,0,0,1},{1,0,0,1}
  };
  int phase = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (int s = 0; s < STEPS_PER_REV; s++) {
      phase = (phase + 1) % 8;
      for (int i = 0; i < 4; i++) digitalWrite(MOTOR_PINS[i], SEQ[phase][i]);
      delayMicroseconds(STEP_DELAY_US);
      if (s % 32 == 0) vTaskDelay(1);
    }
    motorRelease();
    motorBusy = false;
  }
}

int lastString = -1;

void drawScale(int cents, bool showNeedle, uint16_t color) {
  tft.fillRect(0, 148, 240, 56, ST77XX_BLACK);
  tft.drawFastHLine(10, 176, 220, ST77XX_WHITE);
  for (int c = -50; c <= 50; c += 10) {
    int xx = 120 + c * 22 / 10;
    tft.drawFastVLine(xx, 170, 13, c == 0 ? ST77XX_GREEN : ST77XX_WHITE);
  }
  if (showNeedle) {
    int xx = 120 + constrain(cents, -50, 50) * 22 / 10;
    tft.fillRect(xx - 3, 150, 6, 52, color);
  }
}

void showStatus(const char* text, uint16_t color) {
  tft.fillRect(0, 210, 240, 22, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(color);
  tft.setCursor(120 - strlen(text) * 6, 213);
  tft.print(text);
}

void showReading(int idx, float f, float cents) {
  if (idx != lastString) {
    tft.fillRect(0, 28, 240, 90, ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(7);
    tft.setCursor(120 - strlen(STRINGS[idx].name) * 21, 32);
    tft.print(STRINGS[idx].name);
    tft.setTextSize(2);
    tft.setCursor(72, 96);
    tft.printf("String %d", STRINGS[idx].num);
    lastString = idx;
  }

  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f Hz  %+.0f c", f, cents);
  tft.fillRect(0, 122, 240, 20, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(120 - strlen(buf) * 6, 124);
  tft.print(buf);

  bool ok = fabsf(cents) < IN_TUNE_CENTS;
  uint16_t col = ok ? ST77XX_GREEN : ST77XX_RED;
  drawScale((int)cents, true, col);
  if (ok)             showStatus("IN TUNE", ST77XX_GREEN);
  else if (cents < 0) showStatus("LOW - tighten", ST77XX_RED);
  else                showStatus("HIGH - loosen", ST77XX_RED);
}

void showIdle() {
  drawScale(0, false, 0);
  showStatus("Play a string", ST77XX_WHITE);
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) pinMode(MOTOR_PINS[i], OUTPUT);
  motorRelease();

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  SPI.begin(TFT_SCL, -1, TFT_SDA, TFT_CS);
  tft.init(240, 240);
  tft.setSPISpeed(40000000);
  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(48, 4);
  tft.print("Guitar Tuner");
  showIdle();

  setupI2S();
  size_t br;
  for (int i = 0; i < 5; i++) i2s_read(I2S_NUM_0, raw, sizeof(raw), &br, portMAX_DELAY);

  xTaskCreatePinnedToCore(motorTask, "motor", 4096, NULL, 1, &motorTaskHandle, 0);
  Serial.println("Tuner started");
}

enum State { ARMED, PLAYING };
State state = ARMED;
int candString = -1, candCount = 0, quietCount = 0;

int   shownString = -1, dispCand = -1, dispCount = 0;
float smoothCents = 0, smoothF = 0;
uint32_t lastDraw = 0;

void loop() {
  float dbfs = readAudio();
  float f = (dbfs > TH_ON && !motorBusy) ? detectFreq() : 0;

  if (DEBUG) {
    static int dbg = 0;
    if (++dbg % 3 == 0)
      Serial.printf("dB:%6.1f  f:%6.1f  state:%s  cand:%d(%d)  busy:%d\n",
                    dbfs, f, state == ARMED ? "ARMED" : "PLAYING",
                    candString >= 0 ? STRINGS[candString].num : 0, candCount, motorBusy);
  }

  if (f > 0) {
    int best = 0; float bestC = 1e9;
    for (int i = 0; i < 6; i++) {
      float c = 1200.0f * log2f(f / STRINGS[i].freq);
      if (fabsf(c) < fabsf(bestC)) { bestC = c; best = i; }
    }
    quietCount = 0;

    if (best == shownString) {
      dispCount = 0;
      smoothCents += SMOOTH * (bestC - smoothCents);
      smoothF     += SMOOTH * (f - smoothF);
    } else {
      if (best == dispCand) dispCount++;
      else { dispCand = best; dispCount = 1; }
      if (dispCount >= DISPLAY_CONFIRM) {
        shownString = best;
        smoothCents = bestC;
        smoothF = f;
        dispCount = 0;
      }
    }
    if (shownString >= 0 && millis() - lastDraw >= DISPLAY_INTERVAL_MS) {
      showReading(shownString, smoothF, smoothCents);
      lastDraw = millis();
    }

    if (state == ARMED) {
      if (best == candString) candCount++;
      else { candString = best; candCount = 1; }

      if (candCount >= CONFIRM_FRAMES) {
        state = PLAYING;
        Serial.printf("String %d (%s): %.1f Hz, %+.0f cents\n",
                      STRINGS[best].num, STRINGS[best].name, f, bestC);
        if (STRINGS[best].num % 2 == 0) {
          motorBusy = true;
          xTaskNotifyGive(motorTaskHandle);
          showStatus("MOTOR: 360", ST77XX_CYAN);
          Serial.println("  -> even string, motor 360");
        }
      }
    }
  } else if (dbfs < TH_OFF && !motorBusy) {
    if (++quietCount == QUIET_FRAMES) {
      state = ARMED;
      candString = -1; candCount = 0;
      shownString = -1; dispCand = -1; dispCount = 0;
      showIdle();
    }
  }
}
