#include "Decoder.h"

using namespace libhelix;

void Decoder::begin(PcmSink sink, void* ctx) {
  sink_ = sink;
  ctx_ = ctx;
  mp3_.setDataCallback(onPcm);
  mp3_.setReference(this);
  mp3_.setDataReference(this);
  mp3_.begin();
}

void Decoder::onPcm(MP3FrameInfo& info, short* pcm, size_t len, void* ref) {
  Decoder* self = static_cast<Decoder*>(ref);
  if (!self || self->discard_ || !self->sink_ || info.nChans < 1 || info.nChans > 2) return;
  self->sink_(pcm, len, (uint8_t)info.nChans, (uint32_t)info.samprate, self->ctx_);
}

void Decoder::feed(const uint8_t* data, size_t len) {
  if (!data) return;
  size_t done = 0;
  while (done < len) {
    size_t n = mp3_.write(data + done, len - done);
    if (n == 0) {  // nem haladt: ne pörögjön végtelenül
      vTaskDelay(pdMS_TO_TICKS(1));
      break;
    }
    done += n;
  }
}

void Decoder::reset() {
  discard_ = true;  // end() flush()-ol, ne jusson régi PCM a sinkbe
  mp3_.end();
  discard_ = false;
  mp3_.begin();
}
