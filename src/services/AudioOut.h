#pragma once
#include <Arduino.h>

// PCM5102A DAC I2S-en (legacy driver/i2s.h). A DAC SCK/FMT/XSMT lábai hardveresen kötöttek.
class AudioOut {
 public:
  bool begin(int bck, int lrck, int din, uint32_t sampleRate);
  void setSampleRate(uint32_t sampleRate);
  // `frames` db minta csatornánként; 1 csatornát sztereóra duplikál.
  void write(const int16_t* pcm, size_t frames, uint8_t channels);
  // Egyszeri törlés: kinullázza a DMA-ban még lejátszásra váró mintákat.
  // Nem tartós némítás, a következő write() hangja rendesen kimegy.
  void clearOutput();
  uint32_t sampleRate() const { return sampleRate_; }

 private:
  static constexpr size_t kChunkFrames = 576;
  uint32_t sampleRate_ = 0;
  bool started_ = false;
  int16_t stereo_[kChunkFrames * 2];
};
