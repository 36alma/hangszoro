#include "Format.h"

#include <math.h>
#include <stdio.h>

namespace disp {

// A jelölők oszlopa a frekvenciájukat tartalmazó sáv (Spectrum::bandEdgeHz): 100 Hz, 300 Hz, 1, 3, 8 kHz.
const char* const kAxisLabels = ".1  .3   1    3   8 k";

namespace {

constexpr float kMinDb = -60.0f;

std::string printf1(const char* format, unsigned a) {
  char buf[24];
  snprintf(buf, sizeof(buf), format, a);
  return buf;
}

std::string printf2(const char* format, unsigned a, unsigned b) {
  char buf[24];
  snprintf(buf, sizeof(buf), format, a, b);
  return buf;
}

// Magyar ékezetes betű → ékezet nélküli; 0, ha nem az.
char foldCodepoint(uint32_t cp) {
  switch (cp) {
    case 0xE1: return 'a';
    case 0xC1: return 'A';
    case 0xE9: return 'e';
    case 0xC9: return 'E';
    case 0xED: return 'i';
    case 0xCD: return 'I';
    case 0xF3: case 0xF6: case 0x151: return 'o';
    case 0xD3: case 0xD6: case 0x150: return 'O';
    case 0xFA: case 0xFC: case 0x171: return 'u';
    case 0xDA: case 0xDC: case 0x170: return 'U';
    default: return 0;
  }
}

float clampDb(float dbfs) {
  if (dbfs < kMinDb) return kMinDb;
  if (dbfs > 0.0f) return 0.0f;
  return dbfs;
}

std::string ipText(uint32_t ip) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (unsigned)(ip >> 24) & 0xFF, (unsigned)(ip >> 16) & 0xFF,
           (unsigned)(ip >> 8) & 0xFF, (unsigned)ip & 0xFF);
  return buf;
}

std::string rssiText(int dbm) { return std::to_string(dbm) + "dBm"; }

// A stream állapotkódja: "--" nincs WiFi, "OK", ".." kapcsolódik, "E<n>" n hiba után.
std::string wsCode(const Snapshot& s) {
  if (!s.wifiUp) return "--";
  if (s.wsConnected) return "OK";
  if (s.wsFailures == 0) return "..";
  return "E" + compactCount(s.wsFailures);
}

std::string channelsText(uint8_t ch) {
  if (ch == 2) return "ST";
  if (ch == 1) return "MO";
  return "--";
}

unsigned bufferPercent(size_t available, size_t capacity) {
  if (capacity == 0) return 0;
  uint64_t pct = (uint64_t)available * 100 / capacity;
  return pct > 99 ? 99 : (unsigned)pct;
}

const char* stateText(SignalMeter::State st) {
  switch (st) {
    case SignalMeter::State::Signal: return "SIGNAL";
    case SignalMeter::State::Silence: return "SILENCE";
    default: return "NO DATA";
  }
}

std::string uptimeShort(uint64_t ms) {
  uint64_t minutes = ms / 60000;
  uint64_t hours = minutes / 60;
  if (hours >= 100) return std::to_string(hours) + "h";
  return printf2("%u:%02uh", (unsigned)hours, (unsigned)(minutes % 60));
}

// 100 óra alatt ó:pp:mm, felette <nap>d<óra>h, hogy a heap mellett is kiférjen.
std::string uptimeLong(uint64_t ms) {
  uint64_t secs = ms / 1000;
  uint64_t hours = secs / 3600;
  char buf[24];
  if (hours >= 100)
    snprintf(buf, sizeof(buf), "%llud%02uh", (unsigned long long)(hours / 24), (unsigned)(hours % 24));
  else
    snprintf(buf, sizeof(buf), "%u:%02u:%02u", (unsigned)hours, (unsigned)(secs / 60 % 60),
             (unsigned)(secs % 60));
  return buf;
}

std::string ageText(const Snapshot& s) {
  if (!s.wsConnected) return "-";
  if (s.lastRxAgeMs < 10000) return printf2("%u.%us", s.lastRxAgeMs / 1000, s.lastRxAgeMs / 100 % 10);
  if (s.lastRxAgeMs < 1000000) return printf1("%us", s.lastRxAgeMs / 1000);
  return printf1("%um", s.lastRxAgeMs / 60000);
}

// "drop 0 bad 0 lost 3"; ha nem fér ki, rövid alak ("D:4G B:99k L:123k"), ami mindig kifér.
std::string countersText(const Snapshot& s) {
  std::string full = "drop " + compactCount(s.dropped) + " bad " + compactCount(s.malformed) + " lost " +
                     compactCount(s.lost);
  if (full.size() <= kCols) return full;
  return "D:" + compactCount(s.dropped) + " B:" + compactCount(s.malformed) + " L:" + compactCount(s.lost);
}

}  // namespace

std::string fitLine(const std::string& left, const std::string& right, bool keepRight) {
  if (right.empty()) return left.substr(0, kCols);
  if (left.size() + 1 + right.size() <= kCols)
    return left + std::string(kCols - left.size() - right.size(), ' ') + right;
  if (!keepRight) return left.substr(0, kCols);
  if (right.size() + 1 >= kCols) return right.substr(0, kCols);
  return left.substr(0, kCols - 1 - right.size()) + " " + right;
}

std::string asciiFold(const std::string& utf8) {
  std::string out;
  size_t i = 0;
  while (i < utf8.size()) {
    unsigned char c = (unsigned char)utf8[i];
    if (c < 0x80) {
      out += (char)c;
      i++;
      continue;
    }
    size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    uint32_t cp = len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
    bool ok = len > 1 && i + len <= utf8.size();
    for (size_t k = 1; ok && k < len; k++) {
      unsigned char cc = (unsigned char)utf8[i + k];
      if ((cc & 0xC0) != 0x80) ok = false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    char folded = ok ? foldCodepoint(cp) : 0;
    out += folded ? folded : '?';
    i += ok ? len : 1;
  }
  return out;
}

std::string khzText(uint32_t hz) {
  if (hz == 0) return "--";
  uint32_t tenths = (hz + 50) / 100;
  if (tenths % 10 == 0) return printf1("%u", tenths / 10);
  return printf2("%u.%u", tenths / 10, tenths % 10);
}

std::string compactCount(uint32_t n) {
  if (n < 1000) return std::to_string(n);
  if (n < 1000000) return std::to_string(n / 1000) + "k";
  if (n < 1000000000) return std::to_string(n / 1000000) + "M";
  return std::to_string(n / 1000000000) + "G";
}

int dbToPixels(float dbfs, int maxPx) {
  return (int)lroundf((clampDb(dbfs) - kMinDb) / -kMinDb * (float)maxPx);
}

std::string levelText(float dbfs) { return std::to_string(lroundf(clampDb(dbfs))); }

std::array<std::string, 4> overviewText(const Snapshot& s) {
  std::string format = std::to_string(s.bitrateKbps) + "k " + khzText(s.sampleRate) + "k " +
                       channelsText(s.channels);
  std::string buf = "buf " + std::to_string(bufferPercent(s.bufAvailable, s.bufCapacity)) + "%";
  std::string state = std::string(stateText(s.state)) + "  lost:" + compactCount(s.lost);
  std::string ip = ipText(s.ip);
  std::string code = wsCode(s);
  return {
      fitLine(asciiFold(s.deviceName), s.wifiUp ? rssiText(s.rssi) : "NO WIFI", true),
      // A leghosszabb IP (15) mellé a "WS:" előtag nem fér ki; ilyenkor csak a kód áll ott, a szám sosem vágódik.
      fitLine(ip, ip.size() + 1 + 3 + code.size() <= kCols ? "WS:" + code : code, true),
      fitLine(format, buf, true),
      fitLine(state, uptimeShort(s.uptimeMs), false),
  };
}

std::string soundHeader(const Snapshot& s) {
  bool hasData = s.state != SignalMeter::State::NoData && s.sampleRate > 0;
  std::string right = hasData ? khzText(s.sampleRate) + "kHz " + channelsText(s.channels) : "-- kHz";
  return fitLine("HANG", right, true);
}

std::array<std::string, 7> detailsText(const Snapshot& s) {
  return {
      fitLine("RESZLETEK", "", true),
      fitLine("WiFi " + (s.wifiUp ? rssiText(s.rssi) : std::string("--")), "", true),
      fitLine("WS hibak: " + std::to_string(s.wsFailures), "", true),
      fitLine(countersText(s), "", true),
      fitLine("utolso csomag: " + ageText(s), "", true),
      fitLine("heap " + std::to_string(s.freeHeap / 1024) + "k", "up " + uptimeLong(s.uptimeMs), true),
      fitLine("build " + s.buildDate, "", true),
  };
}

}  // namespace disp
