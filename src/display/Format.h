#pragma once
// A debug kijelző szövegei. Hardverfüggetlen, natívan tesztelt.
// Egy sor legfeljebb kCols karakter (6 px széles betűk a 128 px-es kijelzőn).

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <string>

#include "../services/SignalMeter.h"

namespace disp {

constexpr size_t kCols = 21;

// Egy kirajzolás bemenete, a kijelző task állítja össze.
struct Snapshot {
  std::string deviceName;  // UTF-8
  bool wifiUp = false;
  int rssi = 0;
  uint32_t ip = 0;  // host byte order
  bool wsConnected = false;
  uint32_t wsFailures = 0;  // StreamClient + begin() hibák összege
  uint32_t bitrateKbps = 0;
  uint32_t sampleRate = 0;
  uint8_t channels = 0;
  size_t bufAvailable = 0;
  size_t bufCapacity = 0;
  SignalMeter::State state = SignalMeter::State::NoData;
  float dbL = -99;
  float dbR = -99;
  uint32_t dropped = 0;
  uint32_t malformed = 0;
  uint32_t lost = 0;
  uint32_t lastRxAgeMs = 0;
  uint32_t freeHeap = 0;
  uint64_t uptimeMs = 0;
  std::string buildDate;
};

// Bal szöveg + jobbra igazított jobb szöveg, pontosan kCols szélességben (szóközzel kitöltve).
// Ha nem fér ki, és keepRight igaz, a bal oldal rövidül (kCols - 1 - right.size() karakterre).
// Ha nem fér ki, és keepRight hamis, a jobb oldal elmarad: a bal oldal kitöltés nélkül, kCols-ra vágva.
// Üres jobb oldalnál a bal oldal kitöltés nélkül, kCols-ra vágva.
std::string fitLine(const std::string& left, const std::string& right, bool keepRight);

// á→a é→e í→i ó/ö/ő→o ú/ü/ű→u (nagybetűvel is); más nem ASCII kódpont helyén '?'.
std::string asciiFold(const std::string& utf8);

// 44100→"44.1", 48000→"48", 22050→"22.1", 0→"--".
std::string khzText(uint32_t hz);

// 999→"999", 1000→"1k", 12345→"12k", 2500000→"2M", 4e9→"4G" (lefelé kerekítve).
std::string compactCount(uint32_t n);

// [-60, 0] dBFS lineárisan 0..maxPx-re, kerekítve, levágva.
int dbToPixels(float dbfs, int maxPx);

// Kerekített egész [-60, 0]-ra vágva: "-12", "-60", "0".
std::string levelText(float dbfs);

// 1. oldal 1., 2., 3. és 6. sora (a 4-5. a szintcsík).
std::array<std::string, 4> overviewText(const Snapshot& s);

// 2. oldal fejléce.
std::string soundHeader(const Snapshot& s);

// 3. oldal.
std::array<std::string, 7> detailsText(const Snapshot& s);

// A 2. oldal frekvenciatengelye, egy felirat-karakter egy spektrumsáv alatt.
extern const char* const kAxisLabels;

}  // namespace disp
