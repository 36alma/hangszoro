#include "Display.h"

#if HANGSZORO_OLED

#include <U8g2lib.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_timer.h>
#include <math.h>

#include "Log.h"
#include "Stats.h"

namespace {

constexpr uint8_t kAddress = 0x3C;
constexpr uint32_t kI2cHz = 800000;  // az SH1106 elbírja; ha szemetel a kép, vedd vissza 400000-re
constexpr int kBootPin = 0;  // BOOT gomb, aktív alacsony
constexpr int kPages = 4;
constexpr int kHist = 72;  // 72 x 0,5 s = 36 s
constexpr int kLineY0 = 8, kLinePitch = 9, kCharW = 6, kWidth = 128;
constexpr int kBarX = 12, kBarW = 84, kBarH = 6;
constexpr int kBands = 21, kSpecBottom = 53, kSpecMaxH = 44, kSpecBarW = 5;
constexpr float kFloorDb = -60.0f;

U8G2* u8g2 = nullptr;
String devName, devIp;
int page = 0;
uint8_t hist[kHist] = {0};  // puffer telítettsége %-ban, körkörös
uint32_t histPos = 0;

int fillPct() {
  uint32_t cap = gStats.ringCap;
  return cap ? (int)(gStats.ringUsed.load() * 100ull / cap) : 0;
}

int lineY(int row) { return kLineY0 + kLinePitch * row; }
void text(int row, const String& s) { u8g2->drawStr(0, lineY(row), s.c_str()); }

// Magyar ékezetes betűk ékezet nélkül, a többi nem ASCII karakter '?'.
String asciiFold(const String& in) {
  String out;
  for (size_t i = 0; i < in.length();) {
    uint8_t c = in[i];
    if (c < 0x80) { out += (char)c; i++; continue; }
    if (c == 0xC3 && i + 1 < in.length()) {
      uint8_t d = in[i + 1];
      const char* m = nullptr;
      switch (d) {
        case 0xA1: m = "a"; break; case 0x81: m = "A"; break;
        case 0xA9: m = "e"; break; case 0x89: m = "E"; break;
        case 0xAD: m = "i"; break; case 0x8D: m = "I"; break;
        case 0xB3: case 0xB6: m = "o"; break; case 0x93: case 0x96: m = "O"; break;
        case 0xBA: case 0xBC: m = "u"; break; case 0x9A: case 0x9C: m = "U"; break;
      }
      if (m) { out += m; i += 2; continue; }
    }
    if (c == 0xC5 && i + 1 < in.length()) {  // ő Ő ű Ű
      uint8_t d = in[i + 1];
      const char* m = d == 0x91 ? "o" : d == 0x90 ? "O" : d == 0xB1 ? "u" : d == 0xB0 ? "U" : nullptr;
      if (m) { out += m; i += 2; continue; }
    }
    out += '?';
    i++;
    while (i < in.length() && ((uint8_t)in[i] & 0xC0) == 0x80) i++;  // UTF-8 folytatóbájtok
  }
  return out;
}

String padBetween(const String& left, const String& right) {  // 21 karakteres sor
  String l = left;
  int room = 21 - (int)right.length() - 1;
  if ((int)l.length() > room) l = l.substring(0, room);
  String s = l;
  while ((int)(s.length() + right.length()) < 21) s += ' ';
  return s + right;
}

float toDb(int32_t peak) { return peak <= 0 ? -99.0f : 20.0f * log10f(peak / 32768.0f); }

int dbToPx(float db, int maxPx) {
  if (db <= kFloorDb) return 0;
  if (db >= 0) return maxPx;
  return (int)((db - kFloorDb) / -kFloorDb * maxPx);
}

String uptimeShort(uint32_t sec) {
  char b[12];
  snprintf(b, sizeof b, "%u:%02uh", (unsigned)(sec / 3600), (unsigned)(sec / 60 % 60));
  return b;
}

String uptimeLong(uint32_t sec) {
  char b[16];
  if (sec >= 100 * 3600u) snprintf(b, sizeof b, "%ud%uh", (unsigned)(sec / 86400), (unsigned)(sec / 3600 % 24));
  else snprintf(b, sizeof b, "%u:%02u:%02u", (unsigned)(sec / 3600), (unsigned)(sec / 60 % 60), (unsigned)(sec % 60));
  return b;
}

String rateText(uint32_t hz) {
  if (hz == 0) return "-- kHz";
  char b[12];
  snprintf(b, sizeof b, "%.1fkHz", hz / 1000.0f);
  return b;
}

struct Snap {
  bool wifi;
  int rssi;
  float dbL, dbR;
  bool noData, silence;
  uint32_t sr, ch;
};

Snap take() {
  Snap s;
  s.wifi = WiFi.status() == WL_CONNECTED;
  s.rssi = s.wifi ? WiFi.RSSI() : 0;
  s.dbL = toDb(gStats.peakL.exchange(0));
  s.dbR = toDb(gStats.peakR.exchange(0));
  s.sr = gStats.sampleRate;
  s.ch = gStats.channels;
  s.noData = (millis() - gStats.lastPcmMs.load()) > 1000 || gStats.lastPcmMs.load() == 0;
  s.silence = !s.noData && s.dbL < kFloorDb && s.dbR < kFloorDb;
  return s;
}

void levelRow(int row, const char* label, float db) {
  int y = lineY(row);
  u8g2->drawStr(0, y, label);
  u8g2->drawFrame(kBarX, y - kBarH, kBarW, kBarH);
  int w = dbToPx(db, kBarW);
  if (w > 0) u8g2->drawBox(kBarX, y - kBarH, w, kBarH);
  char b[8];
  if (db <= -99) snprintf(b, sizeof b, "--"); else snprintf(b, sizeof b, "%d", (int)roundf(db));
  u8g2->drawStr(kWidth - (int)strlen(b) * kCharW, y, b);
}

String wsText() {
  if (WiFi.status() != WL_CONNECTED) return "WS:--";
  if (gStats.wsConnected) return "WS:OK";
  uint32_t f = gStats.wsFailures;
  return f ? "WS:E" + String(f) : "WS:..";
}

void pageOverview(const Snap& s) {
  text(0, padBetween(asciiFold(devName), s.wifi ? String(s.rssi) + "dBm" : String("NO WIFI")));
  text(1, padBetween(s.wifi ? WiFi.localIP().toString() : devIp, wsText()));
  uint32_t cap = gStats.ringCap, used = gStats.ringUsed;
  char b[24];
  snprintf(b, sizeof b, "%uk %.1fk %s buf %u%%", (unsigned)gStats.bitrateKbps.load(), s.sr / 1000.0f,
           s.ch == 1 ? "MO" : "ST", cap ? (unsigned)(used * 100 / cap) : 0u);
  text(2, b);
  levelRow(3, "L", s.dbL);
  levelRow(4, "R", s.dbR);
  uint32_t up = (uint64_t)esp_timer_get_time() / 1000000;
  static const char* const kMix[] = {"IDLE", "MUSIC", "MIC", "MUSIC+MIC", "BELL"};
  uint32_t ms = gStats.mixStatus;
  const char* st = s.noData ? "NO DATA" : s.silence ? "SILENCE" : ms < 5 ? kMix[ms] : "?";
  snprintf(b, sizeof b, "%s lost:%u", st, (unsigned)gStats.lost.load());
  text(5, padBetween(b, uptimeShort(up)));
}

// Goertzel: egy frekvencia teljesítménye az ablakból.
float bandDb(const int16_t* x, float freq, float fs) {
  float w = 2.0f * (float)M_PI * freq / fs, c = 2.0f * cosf(w);
  float s1 = 0, s2 = 0;
  for (size_t i = 0; i < kWinSize; i++) {
    float hann = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (kWinSize - 1));
    float s = x[i] / 32768.0f * hann + c * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  float p = s1 * s1 + s2 * s2 - c * s1 * s2;
  float mag = sqrtf(p > 0 ? p : 0) * 4.0f / kWinSize;  // Hann: 2x, a félspektrum: 2x
  return mag < 1e-6f ? -99.0f : 20.0f * log10f(mag);
}

float bandFreq(int i) { return 100.0f * powf(160.0f, i / (float)(kBands - 1)); }  // 100 Hz .. 16 kHz

void pageSound(const Snap& s) {
  text(0, "HANG    " + rateText(s.noData ? 0 : s.sr) + (s.noData ? "" : (s.ch == 1 ? " MO" : " ST")));
  if (!s.noData && s.sr > 0) {
    int16_t w[kWinSize];
    uint32_t pos = gStats.winPos.load();
    for (size_t i = 0; i < kWinSize; i++) w[i] = gStats.win[(pos + i) % kWinSize];
    float nyq = s.sr * 0.5f;
    for (int i = 0; i < kBands; i++) {
      float f = bandFreq(i);
      if (f >= nyq * 0.95f) break;
      int h = dbToPx(bandDb(w, f, (float)s.sr), kSpecMaxH);
      if (h > 0) u8g2->drawBox(i * kCharW, kSpecBottom + 1 - h, kSpecBarW, h);
    }
  }
  // Tengelyfeliratok: a jelölő a saját frekvenciáját tartalmazó sáv alatt áll.
  const struct { float f; const char* t; } ax[] = {{100, ".1"}, {300, ".3"}, {1000, "1"}, {3000, "3"}, {8000, "8"}};
  for (auto& a : ax) {
    int idx = (int)roundf(logf(a.f / 100.0f) / logf(160.0f) * (kBands - 1));
    u8g2->drawStr(idx * kCharW, lineY(6), a.t);
  }
  u8g2->drawStr(kWidth - kCharW, lineY(6), "k");
}

void pageDetails(const Snap& s) {
  uint32_t sec = (uint64_t)esp_timer_get_time() / 1000000;
  char b[28];
  text(0, "RESZLETEK");
  snprintf(b, sizeof b, "WiFi %s", s.wifi ? (String(s.rssi) + "dBm").c_str() : "--");
  text(1, b);
  snprintf(b, sizeof b, "WS hibak: %u", (unsigned)gStats.wsFailures.load());
  text(2, b);
  snprintf(b, sizeof b, "drop %u bad %u lost %u", (unsigned)gStats.dropped.load(),
           (unsigned)gStats.malformed.load(), (unsigned)gStats.lost.load());
  text(3, b);
  uint32_t last = gStats.lastRxMs.load();
  if (last == 0) text(4, "utolso csomag: -");
  else { snprintf(b, sizeof b, "utolso csomag: %.1fs", (millis() - last) / 1000.0f); text(4, b); }
  snprintf(b, sizeof b, "heap %uk  up %s", (unsigned)(ESP.getFreeHeap() / 1024), uptimeLong(sec).c_str());
  text(5, b);
  snprintf(b, sizeof b, "build %s", __DATE__);
  text(6, b);
}

// Vödör: a töltöttséggel arányosan telik; alatta a puffer története.
void pageBuffer() {
  int pct = fillPct();
  const int top = 10, bot = 60, xl0 = 4, xr0 = 44, xl1 = 10, xr1 = 38;  // trapéz: felül széles
  auto left = [&](int y) { return xl0 + (xl1 - xl0) * (y - top) / (bot - top); };
  auto right = [&](int y) { return xr0 + (xr1 - xr0) * (y - top) / (bot - top); };
  int fillRows = (bot - top) * pct / 100;
  for (int y = bot - fillRows; y <= bot; y++) u8g2->drawHLine(left(y), y, right(y) - left(y) + 1);
  u8g2->drawLine(xl0, top, xl1, bot);
  u8g2->drawLine(xr0, top, xr1, bot);
  u8g2->drawHLine(xl1, bot, xr1 - xl1 + 1);
  u8g2->drawHLine(xl0 - 1, top, xr0 - xl0 + 3);  // perem

  text(0, "BUFFER");
  char b[24];
  u8g2->setFont(u8g2_font_logisoso16_tn);
  snprintf(b, sizeof b, "%d", pct);
  u8g2->drawStr(56, 30, b);
  int w = u8g2->getStrWidth(b);
  u8g2->setFont(u8g2_font_6x10_tf);
  u8g2->drawStr(56 + w + 2, 30, "%");

  snprintf(b, sizeof b, "%.1f/%.0fk", gStats.ringUsed.load() / 1024.0f, gStats.ringCap.load() / 1024.0f);
  u8g2->drawStr(56, 40, b);
  uint32_t kbps = gStats.bitrateKbps;
  if (kbps) snprintf(b, sizeof b, "%u ms hang", (unsigned)(gStats.ringUsed.load() * 8ull / kbps));
  else snprintf(b, sizeof b, "-- ms hang");
  u8g2->drawStr(56, 49, b);

  // Történet: 36 s, 0..100 %.
  const int gx = 56, gy = 52, gw = kHist, gh = 12;
  u8g2->drawFrame(gx - 1, gy - 1, gw + 2, gh + 2);
  for (int i = 0; i < kHist; i++) {
    int h = hist[(histPos + i) % kHist] * gh / 100;
    if (h > 0) u8g2->drawVLine(gx + i, gy + gh - h, h);
  }
}

void draw() {
  Snap s = take();
  u8g2->clearBuffer();
  if (page == 0) pageOverview(s);
  else if (page == 1) pageSound(s);
  else if (page == 3) pageBuffer();
  else pageDetails(s);
  uint32_t t0 = millis();
  u8g2->sendBuffer();  // 800 kHz-en kb. 13 ms; külön taskban fut
  static bool logged = false;
  if (!logged) { logged = true; LOGF("[oled] frame send took %u ms\n", (unsigned)(millis() - t0)); }
}

void taskMain(void*) {
  bool lastDown = false;
  uint32_t lastEdge = 0, lastDraw = 0, lastHist = 0;
  bool dirty = true;
  for (;;) {
    uint32_t now = millis();
    bool down = digitalRead(kBootPin) == LOW;
    if (down != lastDown && now - lastEdge > 40) {  // pergésmentesítés
      lastEdge = now;
      lastDown = down;
      if (down) { page = (page + 1) % kPages; dirty = true; }
    }
    if (now - lastHist >= 500) {
      lastHist = now;
      hist[histPos % kHist] = (uint8_t)fillPct();
      histPos++;
    }
    uint32_t period = page == 2 ? 500 : 100;
    if (dirty || now - lastDraw >= period) {
      dirty = false;
      lastDraw = now;
      draw();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

bool displayBegin(int sda, int scl, const String& deviceName, const String& ip) {
  Wire.begin(sda, scl, kI2cHz);
  uint8_t addr = 0;
  for (uint8_t a : {kAddress, (uint8_t)0x3D}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { addr = a; break; }
  }
  if (!addr) {
    LOGF("[oled] no display at 0x3C/0x3D (SDA %d, SCL %d); I2C scan:", sda, scl);
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) { LOGF(" 0x%02X", a); found++; }
    }
    LOGF("%s\n", found ? "" : " nothing found (check wiring, 3V3, SDA/SCL swap)");
    Wire.end();
    return false;
  }
  devName = deviceName;
  devIp = ip;
  // Hardveres I2C (gyors). Az U8g2 begin()-je a Wire-t az alap tűkre állíthatja, ezért utána
  // újra a megadott SDA/SCL tűkre kényszerítjük.
  u8g2 = new U8G2_SH1106_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
  u8g2->setI2CAddress(addr << 1);
  u8g2->begin();
  Wire.end();
  Wire.begin(sda, scl, kI2cHz);
  u8g2->setBusClock(kI2cHz);
  u8g2->setPowerSave(0);
  u8g2->setContrast(255);
  u8g2->setFont(u8g2_font_6x10_tf);
  pinMode(kBootPin, INPUT_PULLUP);
  if (xTaskCreatePinnedToCore(taskMain, "display", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
    LOGLN("[oled] display task start failed");
    return false;
  }
  LOGF("[oled] SH1106 display running (SDA %d, SCL %d)\n", sda, scl);
  return true;
}

#endif  // HANGSZORO_OLED
