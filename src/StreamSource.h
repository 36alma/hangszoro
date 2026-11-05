#pragma once
// A WebSocketről érkező MP3 bájtok puffere: FreeRTOS StreamBuffer.
// Egy író (WebSocket task) és egy olvasó (dekóder task) használhatja biztonságosan.
// Az írás és az olvasás sosem blokkol (timeout 0); a clear() csak az olvasó taskból hívható.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

#include "AudioFileSource.h"

class StreamSource : public AudioFileSource {
 public:
  explicit StreamSource(size_t capacity) : cap_(capacity) {
    sb_ = xStreamBufferCreate(cap_, 1);
  }
  ~StreamSource() override {
    if (sb_) vStreamBufferDelete(sb_);
  }

  bool ok() const { return sb_ != nullptr; }
  size_t available() const { return sb_ ? xStreamBufferBytesAvailable(sb_) : 0; }
  size_t capacity() const { return cap_; }
  void clear() {
    if (sb_) xStreamBufferReset(sb_);
  }

  // Visszaadja a ténylegesen beírt bájtok számát (kevesebb, ha megtelt).
  size_t write(const uint8_t* data, size_t len) {
    return sb_ ? xStreamBufferSend(sb_, data, len, 0) : 0;
  }

  uint32_t read(void* data, uint32_t len) override {
    return sb_ ? (uint32_t)xStreamBufferReceive(sb_, data, len, 0) : 0;
  }

  bool seek(int32_t, int) override { return false; }
  bool close() override { return true; }
  bool isOpen() override { return true; }
  uint32_t getSize() override { return 0; }
  uint32_t getPos() override { return 0; }

 private:
  StreamBufferHandle_t sb_ = nullptr;
  size_t cap_;
};
