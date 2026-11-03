#include "DebugDisplay.h"

#if HANGSZORO_OLED

#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_timer.h>

#include "../Log.h"
#include "../services/RingBuffer.h"
#include "../services/SignalMeter.h"
#include "../services/StreamClient.h"
#include "AudioTap.h"
#include "Format.h"
#include "PageSelector.h"
#include "Spectrum.h"

namespace debugdisplay {
namespace {

constexpr uint8_t kAddress = 0x3C;
constexpr uint32_t kI2cHz = 400000;
constexpr int kBootPin = 0;            // BOOT gomb, aktív alacsony
constexpr size_t kTapFrames = 8192;    // 32 KB: 48 kHz-en is bőven több 100 ms-nál
constexpr size_t kPullFrames = 512;
constexpr uint32_t kTickMs = 10;       // gomb lekérdezése
constexpr uint32_t kAudioMs = 100;     // a tap ürítése és a szintmérés ablaka
constexpr uint32_t kFastMs = 100;      // 1. és 2. oldal frissítése
constexpr uint32_t kSlowMs = 500;      // 3. oldal frissítése
constexpr uint8_t kDetailsPage = 2;

// Sorok alapvonala: y = 8 + 9 * sor (u8g2_font_6x10_tf, 6 px széles betűk).
constexpr int kLineY0 = 8;
constexpr int kLinePitch = 9;
constexpr int kCharW = 6;
constexpr int kWidth = 128;
// 1. oldal szintcsíkjai.
constexpr int kLevelBarX = 12;
constexpr int kLevelBarW = 84;
constexpr int kLevelBarH = 6;
// 2. oldal spektruma.
constexpr int kSpecBottom = 53;
constexpr int kSpecMaxH = 44;
constexpr int kSpecBarW = 5;

struct Context {
  const Config* cfg = nullptr;
  StreamClient* client = nullptr;
  const RingBuffer* ring = nullptr;
  const std::atomic<uint32_t>* beginFailures = nullptr;
  U8G2* u8g2 = nullptr;
  AudioTap* tap = nullptr;
  Spectrum* spectrum = nullptr;
};

// A begin() tölti ki, mielőtt az `active` igazra vált; utána csak olvasott.
Context ctx;
std::atomic<bool> active{false};

// Csak a kijelző task használja. A nagy pufferek statikusak, nem a task vermén vannak.
PageSelector pages;
SignalMeter meter;
SignalMeter::Report lastReport;
int16_t pullBuf[kPullFrames * 2];
int16_t monoWindow[Spectrum::kSize];  // körkörös, a legutóbbi kSize mono minta
size_t monoPos = 0;
int16_t monoOrdered[Spectrum::kSize];
float bandsDb[Spectrum::kBands];

int lineY(int row) { return kLineY0 + kLinePitch * row; }

void text(int row, const std::string& s) { ctx.u8g2->drawStr(0, lineY(row), s.c_str()); }

void drainAudio() {
  size_t n;
  while ((n = ctx.tap->pull(pullBuf, kPullFrames)) > 0) {
    meter.add(pullBuf, n, 2);
    for (size_t i = 0; i < n; i++) {
      monoWindow[monoPos] = (int16_t)(((int32_t)pullBuf[2 * i] + pullBuf[2 * i + 1]) / 2);
      monoPos = (monoPos + 1) % Spectrum::kSize;
    }
  }
  lastReport = meter.take();
}

disp::Snapshot snapshot() {
  disp::Snapshot s;
  StreamStats st = ctx.client->stats();
  s.deviceName = ctx.cfg->deviceName;
  s.wifiUp = WiFi.status() == WL_CONNECTED;
  s.rssi = s.wifiUp ? WiFi.RSSI() : 0;
  s.ip = ctx.cfg->ip;
  s.wsConnected = st.connected;
  s.wsFailures = st.failures + ctx.beginFailures->load();
  s.bitrateKbps = ctx.client->bitrateKbps();
  s.sampleRate = ctx.tap->sampleRate();
  s.channels = ctx.tap->channels();
  s.bufAvailable = ctx.ring->available();
  s.bufCapacity = ctx.ring->capacity();
  s.state = lastReport.state;
  s.dbL = SignalMeter::toDbfs(lastReport.peakL);
  s.dbR = SignalMeter::toDbfs(lastReport.peakR);
  s.dropped = st.dropped;
  s.malformed = st.malformed;
  s.lost = st.lost;
  s.lastRxAgeMs = millis() - st.lastRxMs;
  s.freeHeap = ESP.getFreeHeap();
  s.uptimeMs = (uint64_t)esp_timer_get_time() / 1000;
  s.buildDate = __DATE__;
  return s;
}

void levelRow(int row, const char* label, float db) {
  int y = lineY(row);
  ctx.u8g2->drawStr(0, y, label);
  ctx.u8g2->drawFrame(kLevelBarX, y - kLevelBarH, kLevelBarW, kLevelBarH);
  int w = disp::dbToPixels(db, kLevelBarW);
  if (w > 0) ctx.u8g2->drawBox(kLevelBarX, y - kLevelBarH, w, kLevelBarH);
  std::string value = disp::levelText(db);
  ctx.u8g2->drawStr(kWidth - (int)value.size() * kCharW, y, value.c_str());
}

void drawOverview(const disp::Snapshot& s) {
  auto t = disp::overviewText(s);
  text(0, t[0]);
  text(1, t[1]);
  text(2, t[2]);
  levelRow(3, "L", s.dbL);
  levelRow(4, "R", s.dbR);
  text(5, t[3]);
}

void drawSound(const disp::Snapshot& s) {
  text(0, disp::soundHeader(s));
  if (s.state != SignalMeter::State::NoData && s.sampleRate > 0) {
    // Időrendbe tett másolat: a legrégebbi minta elöl.
    for (size_t i = 0; i < Spectrum::kSize; i++)
      monoOrdered[i] = monoWindow[(monoPos + i) % Spectrum::kSize];
    ctx.spectrum->compute(monoOrdered, s.sampleRate, bandsDb);
    for (size_t i = 0; i < Spectrum::kBands; i++) {
      int h = disp::dbToPixels(bandsDb[i], kSpecMaxH);
      if (h > 0) ctx.u8g2->drawBox((int)i * kCharW, kSpecBottom + 1 - h, kSpecBarW, h);
    }
  }
  text(6, disp::kAxisLabels);
}

void drawDetails(const disp::Snapshot& s) {
  auto t = disp::detailsText(s);
  for (int row = 0; row < (int)t.size(); row++) text(row, t[row]);
}

void draw(uint8_t page) {
  disp::Snapshot s = snapshot();
  ctx.u8g2->clearBuffer();
  if (page == 0)
    drawOverview(s);
  else if (page == 1)
    drawSound(s);
  else
    drawDetails(s);
  ctx.u8g2->sendBuffer();  // kb. 25 ms I2C 400 kHz-en, ezért fut külön taskban a 0-s magon
}

void taskMain(void*) {
  uint32_t lastAudioMs = millis();
  uint32_t lastDrawMs = 0;
  bool dirty = true;  // induláskor azonnal rajzol
  for (;;) {
    uint32_t now = millis();
    if (pages.update(digitalRead(kBootPin) == LOW, now)) dirty = true;
    if (now - lastAudioMs >= kAudioMs) {
      lastAudioMs = now;
      drainAudio();
    }
    uint32_t period = pages.page() == kDetailsPage ? kSlowMs : kFastMs;
    if (dirty || now - lastDrawMs >= period) {
      dirty = false;
      lastDrawMs = now;
      draw(pages.page());
    }
    vTaskDelay(pdMS_TO_TICKS(kTickMs));
  }
}

}  // namespace

bool begin(const Config& cfg, StreamClient& client, const RingBuffer& ring,
           const std::atomic<uint32_t>& beginFailures) {
  if (!oledPinsValid(cfg)) {
    LOGLN("[oled] pins overlap i2s or each other, display disabled");
    return false;
  }
  Wire.begin(cfg.oledSda, cfg.oledScl, kI2cHz);
  Wire.beginTransmission(kAddress);
  if (Wire.endTransmission() != 0) {
    LOGF("[oled] no display at 0x3C (SDA %d, SCL %d), display disabled\n", cfg.oledSda, cfg.oledScl);
    Wire.end();
    return false;
  }

  // A Wire már a mi tűinken fut; az U8g2 saját Wire.begin()-je ezt meghagyja.
  U8G2* u8g2 = new U8G2_SH1106_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
  u8g2->setBusClock(kI2cHz);
  u8g2->begin();
  u8g2->setFont(u8g2_font_6x10_tf);
  pinMode(kBootPin, INPUT_PULLUP);

  ctx.cfg = &cfg;
  ctx.client = &client;
  ctx.ring = &ring;
  ctx.beginFailures = &beginFailures;
  ctx.u8g2 = u8g2;
  ctx.tap = new AudioTap(kTapFrames);
  ctx.spectrum = new Spectrum();
  active.store(true, std::memory_order_release);

  if (xTaskCreatePinnedToCore(taskMain, "display", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
    active.store(false, std::memory_order_release);
    LOGLN("[oled] display task start failed");
    return false;
  }
  LOGF("[oled] SH1106 display running (SDA %d, SCL %d)\n", cfg.oledSda, cfg.oledScl);
  return true;
}

void onPcm(const int16_t* pcm, size_t frames, uint8_t channels, uint32_t sampleRate) {
  if (!active.load(std::memory_order_acquire)) return;
  ctx.tap->push(pcm, frames, channels, sampleRate);
}

}  // namespace debugdisplay

#endif  // HANGSZORO_OLED
