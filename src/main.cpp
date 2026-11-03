#include <Arduino.h>
#include <LittleFS.h>

#include "model/Config.h"
#include "services/AudioOut.h"
#include "services/Decoder.h"
#include "services/Network.h"
#include "services/RingBuffer.h"
#include "services/SignalMeter.h"
#include "services/StreamClient.h"
#include "Log.h"

namespace {

constexpr uint32_t kInitialSampleRate = 44100;
constexpr size_t kMaxBytesPerMs = 40;  // 320 kbps
constexpr uint32_t kSignalReportMs = 1000;

Config cfg;
Network net;
AudioOut audio;
Decoder decoder;
SignalMeter meter;  // csak a dekóder task használja (onPcm és reportSignal)
RingBuffer* ring = nullptr;
StreamClient* client = nullptr;
bool clientStarted = false;
uint32_t beginFailures = 0;
uint32_t nextBeginAtMs = 0;
bool linkWasUp = true;

void loadConfig() {
  cfg = defaultConfig();
  if (!LittleFS.begin(false)) {
    LOGLN("[cfg] LittleFS mount failed, using defaults");
    return;
  }
  File f = LittleFS.open("/config.json", "r");
  if (!f) {
    LOGLN("[cfg] /config.json not found, using defaults");
    return;
  }
  String body = f.readString();
  f.close();
  for (const auto& w : applyJson(cfg, body.c_str())) LOGF("[cfg] %s\n", w.c_str());
  LOGLN("[cfg] loaded");
}

void onPcm(const int16_t* pcm, size_t samples, uint8_t channels, uint32_t rate, void*) {
  // A STREAM_INFO csak bejelentés; ez a ténylegesen dekódolt MP3 csatornaszáma.
  static uint8_t lastChannels = 0;
  if (channels != lastChannels) {
    LOGF("[dec] decoded MP3: %u ch, %u Hz\n", (unsigned)channels, (unsigned)rate);
    lastChannels = channels;
  }
  if (rate != audio.sampleRate()) audio.setSampleRate(rate);
  if (HANGSZORO_SERIAL_LOG) meter.add(pcm, samples / channels, channels);
  audio.write(pcm, samples / channels, channels);
}

// Másodpercenként kiírja, hogy megy-e hang az I2S-re, csatornánkénti csúcsszinttel.
void reportSignal() {
  if (!HANGSZORO_SERIAL_LOG) return;
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (now - lastMs < kSignalReportMs) return;
  lastMs = now;
  SignalMeter::Report r = meter.take();
  if (r.state == SignalMeter::State::NoData) {
    LOGF("[sig] NO DATA  (nothing decoded, buffered %u B)\n", (unsigned)ring->available());
    return;
  }
  LOGF("[sig] %-7s L=%6.1f dBFS  R=%6.1f dBFS\n",
       r.state == SignalMeter::State::Signal ? "SIGNAL" : "SILENCE", SignalMeter::toDbfs(r.peakL),
       SignalMeter::toDbfs(r.peakR));
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
    reportSignal();
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
  LOG_BEGIN(115200);
  delay(300);
  LOGLN("[boot] hangszoro");

  loadConfig();
  LOGF("[boot] device name: %s, hostname: %s\n", cfg.deviceName.c_str(),
                cfg.hostname.c_str());

  if (!audio.begin(cfg.bck, cfg.lrck, cfg.din, kInitialSampleRate)) {
    LOGLN("[boot] audio init failed");
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
    linkWasUp = true;
    if (!clientStarted && (int32_t)(millis() - nextBeginAtMs) >= 0) {
      clientStarted = client->begin();
      if (clientStarted) {
        beginFailures = 0;
      } else {
        beginFailures++;
        nextBeginAtMs = millis() + StreamClient::backoffMs(beginFailures);
        if (beginFailures == 5)
          LOGLN("[ws] repeated start failures: check host, DNS and firewall settings");
      }
    }
    if (clientStarted) client->loop();
  } else if (linkWasUp) {
    linkWasUp = false;  // kimaradási él: egyszer kérünk ürítést
    if (clientStarted) client->onLinkDown();
  }
  delay(1);
}
