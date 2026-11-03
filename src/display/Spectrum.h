#pragma once
#include <stddef.h>
#include <stdint.h>

// Spektrum a kijelző 2. oldalához: Hann-ablakos, hordozható radix-2 FFT, logaritmikus sávok.
// Hardverfüggetlen, natívan tesztelt. Kb. 12 KB, ezért statikus vagy heapen lévő példány kell.
class Spectrum {
 public:
  static constexpr size_t kSize = 1024;  // FFT hossz (2 hatványa)
  static constexpr size_t kBands = 21;   // = disp::kCols, egy sáv egy karakteroszlop (6 px)

  Spectrum();

  // mono: kSize minta, a legrégebbi elöl. bandsDb: kBands érték dBFS-ben
  // (teljes kivezérlésű szinusz ≈ 0 dB), alsó határ -99. A Nyquist feletti sávok -99.
  void compute(const int16_t* mono, uint32_t sampleRate, float* bandsDb);

  // i = 0..kBands: 100 * 160^(i / kBands), azaz 100 Hz .. 16 kHz.
  static float bandEdgeHz(size_t i);

 private:
  float bandDb(size_t band, uint32_t sampleRate) const;
  float binDb(size_t k) const;

  float re_[kSize];
  float im_[kSize];
  float window_[kSize];
  float windowSum_ = 0;
};
