#include "Spectrum.h"

#include <math.h>

#include <utility>

constexpr size_t Spectrum::kSize;
constexpr size_t Spectrum::kBands;

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kFloorDb = -99.0f;
constexpr float kMinHz = 100.0f;
constexpr float kMaxHz = 16000.0f;

// Helyben számoló iteratív radix-2 FFT (Cooley-Tukey), n 2 hatványa.
void fft(float* re, float* im, size_t n) {
  for (size_t i = 1, j = 0; i < n; i++) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    float ang = -2.0f * kPi / (float)len;
    float wr = cosf(ang), wi = sinf(ang);
    size_t half = len / 2;
    for (size_t i = 0; i < n; i += len) {
      float cr = 1.0f, ci = 0.0f;
      for (size_t k = 0; k < half; k++) {
        size_t a = i + k, b = a + half;
        float tr = re[b] * cr - im[b] * ci;
        float ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
        float ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = ncr;
      }
    }
  }
}

}  // namespace

Spectrum::Spectrum() {
  for (size_t i = 0; i < kSize; i++) {
    window_[i] = 0.5f - 0.5f * cosf(2.0f * kPi * (float)i / (float)kSize);
    windowSum_ += window_[i];
  }
}

float Spectrum::bandEdgeHz(size_t i) {
  return kMinHz * powf(kMaxHz / kMinHz, (float)i / (float)kBands);
}

float Spectrum::binDb(size_t k) const {
  float mag = sqrtf(re_[k] * re_[k] + im_[k] * im_[k]);
  if (mag <= 0.0f) return kFloorDb;
  float db = 20.0f * log10f(2.0f * mag / windowSum_);
  return db < kFloorDb ? kFloorDb : db;
}

float Spectrum::bandDb(size_t band, uint32_t sampleRate) const {
  float nyquist = (float)sampleRate / 2.0f;
  float lo = bandEdgeHz(band), hi = bandEdgeHz(band + 1);
  if (sampleRate == 0 || lo >= nyquist) return kFloorDb;
  float binHz = (float)sampleRate / (float)kSize;
  float best = kFloorDb;
  bool any = false;
  for (size_t k = (size_t)ceilf(lo / binHz); k <= kSize / 2 && (float)k * binHz < hi; k++) {
    float db = binDb(k);
    if (!any || db > best) best = db;
    any = true;
  }
  if (any) return best;
  // Keskeny sáv bin-közép nélkül: a mértani középhez legközelebbi bin.
  size_t k = (size_t)lroundf(sqrtf(lo * hi) / binHz);
  return binDb(k > kSize / 2 ? kSize / 2 : k);
}

void Spectrum::compute(const int16_t* mono, uint32_t sampleRate, float* bandsDb) {
  for (size_t i = 0; i < kSize; i++) {
    re_[i] = (float)mono[i] / 32768.0f * window_[i];
    im_[i] = 0.0f;
  }
  fft(re_, im_, kSize);
  for (size_t b = 0; b < kBands; b++) bandsDb[b] = bandDb(b, sampleRate);
}
