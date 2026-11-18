#pragma once

// McLeod Pitch Method (MPM) peak picking. Pure C++, no Arduino code,
// so it can be tested on a PC.
//
//   x       - signal window (mean already removed), W samples
//   r       - autocorrelation of x: r[tau] = sum x[i]*x[i+tau], tau = 0..W/2+1
//   nsdf    - scratch buffer, at least W/2+2 floats
//   clarity - out: how "clean" the pitch is, 0..1 (1 = perfect tone)
//
// Returns the frequency in Hz, or 0 if no clear pitch between minHz and maxHz.
float mpmPitch(const float* x, const float* r, float* nsdf, int W,
               float fs, float minHz, float maxHz, float* clarity);
