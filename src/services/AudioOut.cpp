#include "AudioOut.h"

#include <driver/i2s.h>

#include "../Log.h"

namespace {
constexpr i2s_port_t kPort = I2S_NUM_0;
}

bool AudioOut::begin(int bck, int lrck, int din, uint32_t sampleRate) {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = sampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;

  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) {
    LOGLN("[i2s] driver install failed");
    return false;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = bck;
  pins.ws_io_num = lrck;
  pins.data_out_num = din;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(kPort, &pins) != ESP_OK) {
    LOGLN("[i2s] set pin failed");
    i2s_driver_uninstall(kPort);
    return false;
  }
  sampleRate_ = sampleRate;
  started_ = true;
  LOGF("[i2s] started, %u Hz, bck=%d lrck=%d din=%d\n", (unsigned)sampleRate, bck, lrck, din);
  return true;
}

void AudioOut::setSampleRate(uint32_t sampleRate) {
  if (!started_ || sampleRate == sampleRate_) return;
  if (i2s_set_sample_rates(kPort, sampleRate) == ESP_OK) {
    LOGF("[i2s] sample rate %u -> %u Hz\n", (unsigned)sampleRate_, (unsigned)sampleRate);
    sampleRate_ = sampleRate;
  }
}

void AudioOut::write(const int16_t* pcm, size_t frames, uint8_t channels) {
  if (!started_ || !pcm || frames == 0) return;
  size_t written = 0;
  const TickType_t timeout = pdMS_TO_TICKS(1000);
  if (channels == 2) {
    i2s_write(kPort, pcm, frames * 4, &written, timeout);
    return;
  }
  while (frames > 0) {
    size_t n = frames < kChunkFrames ? frames : kChunkFrames;
    for (size_t i = 0; i < n; i++) stereo_[2 * i] = stereo_[2 * i + 1] = pcm[i];
    i2s_write(kPort, stereo_, n * 4, &written, timeout);
    pcm += n;
    frames -= n;
  }
}

void AudioOut::mute() {
  if (started_) i2s_zero_dma_buffer(kPort);
}
