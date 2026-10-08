#pragma once
#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <vector>

// Egy író és egy olvasó szál (SPSC) között biztonságos, zár nélküli bájt ring buffer.
// Az író csak write()-ot, az olvasó csak read()-et hívhat.
class RingBuffer {
 public:
  explicit RingBuffer(size_t capacity);

  size_t write(const uint8_t* data, size_t len);
  size_t read(uint8_t* out, size_t len);

  size_t available() const { return used_.load(std::memory_order_acquire); }
  size_t freeSpace() const { return buf_.size() - available(); }
  size_t capacity() const { return buf_.size(); }

 private:
  std::vector<uint8_t> buf_;
  size_t head_ = 0;  // csak az író módosítja
  size_t tail_ = 0;  // csak az olvasó módosítja
  std::atomic<size_t> used_{0};
};
