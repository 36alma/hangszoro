#include <Arduino.h>
#include <LittleFS.h>

#include "model/Config.h"
#include "services/AudioOut.h"
#include "services/Decoder.h"
#include "services/Network.h"
#include "services/RingBuffer.h"
#include "services/StreamClient.h"

namespace {

constexpr uint32_t kInitialSampleRate = 44100;
constexpr size_t kMaxBytesPerMs = 40;  // 320 kbps

Config cfg;
Network net;
AudioOut audio;
Decoder decoder;
RingBuffer* ring = nullptr;
StreamClient* client = nullptr;
bool clientStarted = false;

void loadConfig() {
  cfg = defaultConfig();
  if (!LittleFS.begin(false)) {
    Serial.println("[cfg] LittleFS mount failed, using defaults");
    return;
  }
  File f = LittleFS.open("/config.json", "r");
  if (!f) {
    Serial.println("[cfg] /config.json not found, using defaults");
    return;
  }
  String body = f.readString();
  f.close();
  for (const auto& w : applyJson(cfg, body.c_str())) Serial.printf("[cfg] %s\n", w.c_str());
  Serial.println("[cfg] loaded");
}

void onPcm(const int16_t* pcm, size_t samples, uint8_t channels, uint32_t rate, void*) {
  if (rate != audio.sampleRate()) audio.setSampleRate(rate);
  audio.write(pcm, samples / channels, channels);
}

void drain(uint8_t* scratch, size_t scratchSize) {
  while (ring->read(scratch, scratchSize) > 0) {
  }
}

void decoderTask(void*) {
  uint8_t chunk[512];
  bool primed = false;
  bool muted = false;
  for (;;) {
    // Szétkapcsolás, új kapcsolat vagy túlcsordulás: lejátszás le, puffer és dekóder törlése.
    if (client->takeFlushRequest()) {
      drain(chunk, sizeof(chunk));
      decoder.reset();
      audio.mute();
      muted = true;
      primed = false;
    }
    if (!primed) {
      size_t prebuffer = (size_t)cfg.prebufferMs * client->bitrateKbps() / 8;
      if (ring->available() < prebuffer) {
        if (!muted) {
          audio.mute();
          muted = true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      primed = true;
      muted = false;
    }
    size_t n = ring->read(chunk, sizeof(chunk));
    if (n == 0) {  // kiürült: újra előpufferelünk, hogy ne ropogjon
      primed = false;
      continue;
    }
    decoder.feed(chunk, n);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("[boot] hangszoro");

  loadConfig();

  if (!audio.begin(cfg.bck, cfg.lrck, cfg.din, kInitialSampleRate)) {
    Serial.println("[boot] audio init failed");
  }
  ring = new RingBuffer((size_t)cfg.bufferMs * kMaxBytesPerMs);
  decoder.begin(onPcm, nullptr);
  client = new StreamClient(cfg, *ring);

  net.connect(cfg);

  xTaskCreatePinnedToCore(decoderTask, "decoder", 16384, nullptr, 2, nullptr, 1);
}

void loop() {
  net.maintain(cfg);
  if (net.connected()) {
    if (!clientStarted) clientStarted = client->begin();
    if (clientStarted) client->loop();
  }
  delay(1);
}
