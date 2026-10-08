#pragma once
#include <Arduino.h>

#include "MP3DecoderHelix.h"

class Decoder {
 public:
  using PcmSink = void (*)(const int16_t* pcm, size_t samples, uint8_t channels, uint32_t sampleRate,
                           void* ctx);

  void begin(PcmSink sink, void* ctx);
  // MP3 bájtok betáplálása; a kész PCM a sink-be kerül.
  void feed(const uint8_t* data, size_t len);
  // Dekóder állapot törlése (pl. újracsatlakozás után).
  void reset();

 private:
  // A libhelix az adat-callbacknek a protected p_caller_data-t adja át (nem a setReference
  // értékét, ami csak az info-callbacknek megy), és ezt semmilyen publikus setter nem állítja.
  class Mp3 : public libhelix::MP3DecoderHelix {
   public:
    void setDataReference(void* ref) { p_caller_data = ref; }
  };

  static void onPcm(MP3FrameInfo& info, short* pcm, size_t len, void* ref);
  Mp3 mp3_;
  PcmSink sink_ = nullptr;
  void* ctx_ = nullptr;
};
