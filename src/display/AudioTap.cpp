#include "AudioTap.h"

#include <algorithm>

// C++14: a std::min referenciát kér, ezért a static constexpr tagoknak definíció is kell.
constexpr size_t AudioTap::kFrameBytes;
constexpr size_t AudioTap::kChunkFrames;

AudioTap::AudioTap(size_t capacityFrames) : stream_(capacityFrames * kFrameBytes) {}

void AudioTap::push(const int16_t* pcm, size_t frames, uint8_t channels, uint32_t sampleRate) {
  if (!pcm || frames == 0 || channels < 1 || channels > 2) return;
  sampleRate_.store(sampleRate, std::memory_order_release);
  channels_.store(channels, std::memory_order_release);

  int16_t stereo[kChunkFrames * 2];
  size_t done = 0;
  while (done < frames) {
    size_t room = (stream_.capacity() - stream_.available()) / kFrameBytes;
    size_t n = std::min(std::min(frames - done, kChunkFrames), room);
    if (n == 0) return;
    const int16_t* src = pcm + done * channels;
    if (channels == 2) {
      std::copy(src, src + n * 2, stereo);
    } else {
      for (size_t i = 0; i < n; i++) stereo[2 * i] = stereo[2 * i + 1] = src[i];
    }
    stream_.write(reinterpret_cast<const uint8_t*>(stereo), n * kFrameBytes);
    done += n;
  }
}

size_t AudioTap::pull(int16_t* outStereo, size_t maxFrames) {
  if (!outStereo || maxFrames == 0) return 0;
  size_t n = std::min(maxFrames, stream_.available() / kFrameBytes);
  return stream_.read(reinterpret_cast<uint8_t*>(outStereo), n * kFrameBytes) / kFrameBytes;
}
