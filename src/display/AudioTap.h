#pragma once
#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "../services/RingBuffer.h"

// PCM-másolat a dekóder task (író) és a kijelző task (olvasó) között.
// Mindig sztereó int16 kereteket tárol (4 bájt), zár nélkül (SPSC RingBuffer).
class AudioTap {
 public:
  explicit AudioTap(size_t capacityFrames);

  // Csak a dekóder task hívja. Interleaved PCM, `frames` minta csatornánként; mono → mindkét csatorna.
  // Ami nem fér el, elveszik; csak egész keretet ír.
  void push(const int16_t* pcm, size_t frames, uint8_t channels, uint32_t sampleRate);

  // Csak a kijelző task hívja. Legfeljebb maxFrames sztereó keret (L,R,L,R…); visszaadja a darabszámot.
  size_t pull(int16_t* outStereo, size_t maxFrames);

  uint32_t sampleRate() const { return sampleRate_.load(std::memory_order_relaxed); }  // előtte 0
  uint8_t channels() const { return channels_.load(std::memory_order_relaxed); }       // előtte 0

 private:
  static constexpr size_t kFrameBytes = 2 * sizeof(int16_t);
  static constexpr size_t kChunkFrames = 256;

  RingBuffer ring_;
  std::atomic<uint32_t> sampleRate_{0};
  std::atomic<uint8_t> channels_{0};
};
