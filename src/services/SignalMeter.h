#pragma once
#include <stddef.h>
#include <stdint.h>

// Jelszintmérő a soros naplóhoz: két take() között csatornánként gyűjti a csúcsértéket.
// Egyetlen szálból használandó (a dekóder task tölti és olvassa is).
class SignalMeter {
 public:
  enum class State { NoData, Silence, Signal };

  struct Report {
    size_t frames = 0;  // minta csatornánként az előző take() óta
    int32_t peakL = 0;  // 0..32768
    int32_t peakR = 0;
    State state = State::NoData;
  };

  // Ennél nem nagyobb csúcs csendnek számít (kb. -60 dBFS).
  static constexpr int32_t kSilencePeak = 33;

  // Interleaved PCM, `frames` minta csatornánként. A mono mindkét oldalra számít,
  // mert az AudioOut is mindkét csatornára kiírja.
  void add(const int16_t* pcm, size_t frames, uint8_t channels);
  // Visszaadja az eddig gyűjtött értékeket, és nulláz.
  Report take();

  // Csúcsérték dBFS-ben (32768 = 0 dBFS); 0 esetén -99.
  static float toDbfs(int32_t peak);

 private:
  size_t frames_ = 0;
  int32_t peakL_ = 0;
  int32_t peakR_ = 0;
};
