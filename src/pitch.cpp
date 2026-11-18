#include "pitch.h"

static const float CLARITY_MIN = 0.6f;  // reject noise / chords below this
static const float PEAK_K      = 0.9f;  // first peak within 90% of the best wins

float mpmPitch(const float* x, const float* r, float* nsdf, int W,
               float fs, float minHz, float maxHz, float* clarity) {
  *clarity = 0;
  int minLag = (int)(fs / maxHz);
  int maxLag = (int)(fs / minHz) + 1;
  if (minLag < 2) minLag = 2;
  if (maxLag > W / 2) maxLag = W / 2;
  if (minLag >= maxLag) return 0;

  // NSDF(tau) = 2 r(tau) / m(tau), where m(tau) is the energy of both
  // overlapping parts. 1 = the signal repeats perfectly after tau samples.
  float m = 0;
  for (int i = 0; i < W; i++) m += x[i] * x[i];
  m *= 2;
  if (m <= 0) return 0;
  nsdf[0] = 1;
  for (int tau = 1; tau <= maxLag + 1; tau++) {
    m -= x[tau - 1] * x[tau - 1] + x[W - tau] * x[W - tau];
    nsdf[tau] = (m > 0) ? 2 * r[tau] / m : 0;
  }

  // Skip the peak around lag 0, then find the highest point of each
  // positive region ("key maxima").
  int tau = 1;
  while (tau <= maxLag && nsdf[tau] > 0) tau++;

  const int MAXK = 64;
  int   keyLag[MAXK];
  float keyVal[MAXK];
  int   nk = 0;
  int   curLag = -1;
  float curVal = 0;
  for (; tau <= maxLag; tau++) {
    float v = nsdf[tau];
    if (v > 0) {
      if (v > curVal && v >= nsdf[tau - 1] && v >= nsdf[tau + 1]) { curVal = v; curLag = tau; }
    } else if (curLag >= 0) {
      if (nk < MAXK) { keyLag[nk] = curLag; keyVal[nk] = curVal; nk++; }
      curLag = -1; curVal = 0;
    }
  }
  if (curLag >= 0 && nk < MAXK) { keyLag[nk] = curLag; keyVal[nk] = curVal; nk++; }

  float best = 0;
  for (int i = 0; i < nk; i++)
    if (keyLag[i] >= minLag && keyVal[i] > best) best = keyVal[i];
  if (best < CLARITY_MIN) return 0;

  // The first (shortest) period that is nearly as good as the best one.
  // Taking the best one directly would often give 2x or 3x the period.
  for (int i = 0; i < nk; i++) {
    if (keyLag[i] < minLag || keyVal[i] < PEAK_K * best) continue;
    int t = keyLag[i];
    float a = nsdf[t - 1], b = nsdf[t], c = nsdf[t + 1];
    float d = a - 2 * b + c;
    float shift = (d != 0) ? 0.5f * (a - c) / d : 0;
    *clarity = keyVal[i];
    return fs / (t + shift);
  }
  return 0;
}
