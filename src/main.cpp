#include <Arduino.h>
#include <math.h>

const int      MIC_PIN          = 1;
const uint32_t SAMPLE_RATE      = 8000;
const uint32_t SAMPLE_PERIOD_US = 1000000UL / SAMPLE_RATE;
const int      N                = 2048;

const float MIN_FREQ      = 70.0f;
const float MAX_FREQ      = 400.0f;
const float YIN_THRESHOLD = 0.15f;
const float IN_TUNE_CENTS = 5.0f;

const int TAU_MIN = SAMPLE_RATE / MAX_FREQ;
const int TAU_MAX = SAMPLE_RATE / MIN_FREQ;

float samples[N];
float yin[TAU_MAX + 2];
float soundThreshold = 15.0f;

struct GuitarString {
  const char *name;
  int number;
  float freq;
};

const GuitarString strings[] = {
  {"E2", 6, 82.41f},
  {"A2", 5, 110.00f},
  {"D3", 4, 146.83f},
  {"G3", 3, 196.00f},
  {"B3", 2, 246.94f},
  {"E4", 1, 329.63f}
};
const int STRING_COUNT = sizeof(strings) / sizeof(strings[0]);

float captureBlock() {
  uint32_t t = micros();
  double sum = 0;

  for (int i = 0; i < N; i++) {
    while ((uint32_t)(micros() - t) < SAMPLE_PERIOD_US) { }
    t += SAMPLE_PERIOD_US;
    samples[i] = analogRead(MIC_PIN);
    sum += samples[i];
  }

  float mean = sum / N;
  double sumSq = 0;
  for (int i = 0; i < N; i++) {
    samples[i] -= mean;
    sumSq += samples[i] * samples[i];
  }
  return sqrt(sumSq / N);
}

float detectPitch() {
  const int W = N - TAU_MAX;

  for (int tau = 1; tau <= TAU_MAX; tau++) {
    float d = 0;
    for (int j = 0; j < W; j++) {
      float diff = samples[j] - samples[j + tau];
      d += diff * diff;
    }
    yin[tau] = d;
  }

  yin[0] = 1.0f;
  float running = 0;
  for (int tau = 1; tau <= TAU_MAX; tau++) {
    running += yin[tau];
    yin[tau] = (running > 0) ? yin[tau] * tau / running : 1.0f;
  }

  int tauEst = -1;
  for (int tau = TAU_MIN; tau <= TAU_MAX; tau++) {
    if (yin[tau] < YIN_THRESHOLD) {
      while (tau + 1 <= TAU_MAX && yin[tau + 1] < yin[tau]) tau++;
      tauEst = tau;
      break;
    }
  }
  if (tauEst < 0) return 0;

  float better = tauEst;
  if (tauEst > 1 && tauEst < TAU_MAX) {
    float s0 = yin[tauEst - 1], s1 = yin[tauEst], s2 = yin[tauEst + 1];
    float denom = s0 + s2 - 2.0f * s1;
    if (fabs(denom) > 1e-9f) better = tauEst + (s0 - s2) / (2.0f * denom);
  }
  return SAMPLE_RATE / better;
}

void printResult(float freq) {
  int best = 0;
  float cents = 1e9;
  for (int i = 0; i < STRING_COUNT; i++) {
    float c = 1200.0f * log2f(freq / strings[i].freq);
    if (fabs(c) < fabs(cents)) {
      cents = c;
      best = i;
    }
  }

  const GuitarString &s = strings[best];
  float diffHz = freq - s.freq;

  Serial.printf("String %d (%s) | %.2f Hz | target %.2f Hz | %+.1f cents | ",
                s.number, s.name, freq, s.freq, cents);

  if (fabs(cents) <= IN_TUNE_CENTS) {
    Serial.println("IN TUNE");
    return;
  }

  const char *direction = (cents < 0) ? "TIGHTEN (pitch up)" : "LOOSEN (pitch down)";
  const char *amount = (fabs(cents) > 50) ? "a lot" : (fabs(cents) > 15) ? "a little" : "slightly";
  Serial.printf("%s %s, %.2f Hz\n", direction, amount, fabs(diffHz));
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  analogReadResolution(12);
  analogSetPinAttenuation(MIC_PIN, ADC_11db);

  float noise = 0;
  for (int i = 0; i < 4; i++) noise += captureBlock();
  soundThreshold = max(15.0f, (noise / 4) * 3.0f);

  Serial.println("Guitar tuner ready. Play a string.");
}

void loop() {
  if (captureBlock() < soundThreshold) return;

  float freq = detectPitch();
  if (freq >= MIN_FREQ && freq <= MAX_FREQ) {
    printResult(freq);
  }
}
