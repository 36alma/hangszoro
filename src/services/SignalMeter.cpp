#include "SignalMeter.h"

#include <math.h>

namespace {
inline int32_t absSample(int16_t s) { return s < 0 ? -(int32_t)s : (int32_t)s; }
}  // namespace

void SignalMeter::add(const int16_t* pcm, size_t frames, uint8_t channels) {
  if (!pcm || frames == 0 || channels < 1 || channels > 2) return;
  for (size_t i = 0; i < frames; i++) {
    int32_t l = absSample(pcm[i * channels]);
    int32_t r = channels == 2 ? absSample(pcm[i * 2 + 1]) : l;
    if (l > peakL_) peakL_ = l;
    if (r > peakR_) peakR_ = r;
  }
  frames_ += frames;
}

SignalMeter::Report SignalMeter::take() {
  Report r;
  r.frames = frames_;
  r.peakL = peakL_;
  r.peakR = peakR_;
  if (frames_ == 0)
    r.state = State::NoData;
  else if (peakL_ > kSilencePeak || peakR_ > kSilencePeak)
    r.state = State::Signal;
  else
    r.state = State::Silence;
  frames_ = 0;
  peakL_ = 0;
  peakR_ = 0;
  return r;
}

float SignalMeter::toDbfs(int32_t peak) {
  if (peak <= 0) return -99.0f;
  return 20.0f * log10f((float)peak / 32768.0f);
}
