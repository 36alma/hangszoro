#pragma once
// Közös számlálók a kijelzőnek (a kijelző task a 0-s magon fut, a loop() az 1-esen).
// A PCM-et lehallgató kimenet itt számolja a szintet és a spektrumhoz az ablakot.

#include <Arduino.h>

#include <atomic>

#include "AudioOutputI2S.h"

constexpr size_t kWinSize = 512;  // spektrum ablak (mono minták)

struct Stats {
  std::atomic<bool> wsConnected{false};
  std::atomic<uint32_t> wsFailures{0};
  std::atomic<uint32_t> bitrateKbps{0};
  std::atomic<uint32_t> lost{0};
  std::atomic<uint32_t> malformed{0};
  std::atomic<uint32_t> dropped{0};
  std::atomic<uint32_t> lastRxMs{0};
  std::atomic<uint32_t> mixStatus{0};  // MixerStatus a csomagfejlécből: 0 IDLE,1 zene,2 mic,3 mix,4 csengő
  std::atomic<uint32_t> ringUsed{0};
  std::atomic<uint32_t> ringCap{0};
  std::atomic<uint32_t> sampleRate{0};
  std::atomic<uint32_t> channels{0};
  std::atomic<int32_t> peakL{0};  // 0..32768, a kijelző olvasáskor nullázza
  std::atomic<int32_t> peakR{0};
  std::atomic<uint32_t> lastPcmMs{0};
  int16_t win[kWinSize] = {0};  // körkörös; vizuális célra a ritka versenyhelyzet elfogadható
  std::atomic<uint32_t> winPos{0};
};

inline Stats gStats;

// I2S kimenet, ami közben szintet és spektrumadatot gyűjt.
class TapOutput : public AudioOutputI2S {
 public:
  bool SetRate(int hz) override {
    gStats.sampleRate = hz;
    return AudioOutputI2S::SetRate(hz);
  }
  bool SetChannels(int ch) override {
    gStats.channels = ch;
    return AudioOutputI2S::SetChannels(ch);
  }
  bool ConsumeSample(int16_t s[2]) override {
    int32_t l = abs((int32_t)s[0]), r = abs((int32_t)s[1]);
    if (l > gStats.peakL.load(std::memory_order_relaxed)) gStats.peakL.store(l, std::memory_order_relaxed);
    if (r > gStats.peakR.load(std::memory_order_relaxed)) gStats.peakR.store(r, std::memory_order_relaxed);
    uint32_t p = gStats.winPos.load(std::memory_order_relaxed);
    gStats.win[p % kWinSize] = (int16_t)(((int32_t)s[0] + s[1]) / 2);
    gStats.winPos.store(p + 1, std::memory_order_relaxed);
    gStats.lastPcmMs.store(millis(), std::memory_order_relaxed);
    return AudioOutputI2S::ConsumeSample(s);
  }
};
