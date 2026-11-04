#pragma once
#include <cstddef>
#include <cstdint>

#ifdef NATIVE_TEST
#include <vector>
#include <algorithm>

// Mock AudioStream a teszt build-hez (FreeRTOS nélkül).
class AudioStream {
 public:
  explicit AudioStream(size_t capacityBytes)
    : buf_(capacityBytes), capacity_(capacityBytes), head_(0), tail_(0) {}
  ~AudioStream() = default;

  size_t write(const uint8_t* data, size_t len);
  size_t read(uint8_t* out, size_t len);
  size_t available() const;
  size_t capacity() const { return capacity_; }
  void reset();

 private:
  std::vector<uint8_t> buf_;
  size_t capacity_;
  size_t head_;
  size_t tail_;
};

#else
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

// FreeRTOS StreamBuffer alapú szálbiztos audio stream.
// Egy írót (dekóder task) és egy olvasót (kijelző) támogat.
class AudioStream {
 public:
  explicit AudioStream(size_t capacityBytes);
  ~AudioStream();

  size_t write(const uint8_t* data, size_t len);
  size_t read(uint8_t* out, size_t len);
  size_t available() const;
  size_t capacity() const { return capacity_; }
  void reset();

 private:
  StreamBufferHandle_t stream_{nullptr};
  size_t capacity_;
};
#endif
