#include "AudioStream.h"

#ifdef NATIVE_TEST

size_t AudioStream::write(const uint8_t* data, size_t len) {
  if (!data || len == 0) return 0;
  size_t available = buf_.size() - ((head_ - tail_ + buf_.size()) % buf_.size());
  size_t n = std::min(len, available);
  for (size_t i = 0; i < n; ++i) {
    buf_[head_] = data[i];
    head_ = (head_ + 1) % buf_.size();
  }
  return n;
}

size_t AudioStream::read(uint8_t* out, size_t len) {
  if (!out || len == 0) return 0;
  size_t used = (head_ - tail_ + buf_.size()) % buf_.size();
  size_t n = std::min(len, used);
  for (size_t i = 0; i < n; ++i) {
    out[i] = buf_[tail_];
    tail_ = (tail_ + 1) % buf_.size();
  }
  return n;
}

size_t AudioStream::available() const {
  return (head_ - tail_ + buf_.size()) % buf_.size();
}

void AudioStream::reset() {
  head_ = 0;
  tail_ = 0;
}

#else

AudioStream::AudioStream(size_t capacityBytes) : capacity_(capacityBytes) {
  stream_ = xStreamBufferCreate(capacityBytes, 1);
}

AudioStream::~AudioStream() {
  if (stream_) {
    vStreamBufferDelete(stream_);
  }
}

size_t AudioStream::write(const uint8_t* data, size_t len) {
  if (!data || len == 0 || !stream_) return 0;
  return xStreamBufferSend(stream_, data, len, 0);
}

size_t AudioStream::read(uint8_t* out, size_t len) {
  if (!out || len == 0 || !stream_) return 0;
  return xStreamBufferReceive(stream_, out, len, 0);
}

size_t AudioStream::available() const {
  if (!stream_) return 0;
  return xStreamBufferBytesAvailable(stream_);
}

void AudioStream::reset() {
  if (stream_) {
    xStreamBufferReset(stream_);
  }
}

#endif
