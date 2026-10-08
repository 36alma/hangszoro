#include "RingBuffer.h"

#include <string.h>

#include <algorithm>

RingBuffer::RingBuffer(size_t capacity) : buf_(capacity == 0 ? 1 : capacity) {}

size_t RingBuffer::write(const uint8_t* data, size_t len) {
  if (!data || len == 0) return 0;
  size_t n = std::min(len, freeSpace());
  size_t first = std::min(n, buf_.size() - head_);
  memcpy(&buf_[head_], data, first);
  if (n > first) memcpy(&buf_[0], data + first, n - first);
  head_ = (head_ + n) % buf_.size();
  used_.fetch_add(n, std::memory_order_release);
  return n;
}

size_t RingBuffer::read(uint8_t* out, size_t len) {
  if (!out || len == 0) return 0;
  size_t n = std::min(len, available());
  size_t first = std::min(n, buf_.size() - tail_);
  memcpy(out, &buf_[tail_], first);
  if (n > first) memcpy(out + first, &buf_[0], n - first);
  tail_ = (tail_ + n) % buf_.size();
  used_.fetch_sub(n, std::memory_order_release);
  return n;
}
