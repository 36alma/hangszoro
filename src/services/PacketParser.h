#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

// Csomag: 15 bájtos big-endian fejléc + MP3 payload.
//   0  4  magic "CSGP"
//   4  4  seq (u32)
//   8  4  timestamp_ms (u32)
//  12  1  status (bitmező)
//  13  2  length (u16): a payload hossza
//  15  .  payload
constexpr size_t kPacketHeaderBytes = 15;

enum PacketStatus : uint8_t { kStatusMusic = 1, kStatusMic = 2, kStatusBell = 4 };

struct PacketView {
  bool ok = false;
  uint32_t seq = 0;
  uint32_t timestampMs = 0;
  uint8_t status = 0;
  const uint8_t* payload = nullptr;
  size_t len = 0;
};

// ok=false, ha a csomag a fejlécnél rövidebb, a magic nem "CSGP", vagy a
// payload tényleges hossza nem egyezik a `length` mezővel.
PacketView parsePacket(const uint8_t* data, size_t len);

// Kihagyott csomagok számlálója. A 32 bites szekvenciaszám körbefordulhat.
class SeqTracker {
 public:
  // Az előre kihagyott csomagok számát adja. Az első csomag, a duplikátum és a
  // visszalépés 0-t ad (utóbbi kettő a lateCount()-ot növeli).
  uint32_t onPacket(uint32_t seq);
  void reset();
  uint32_t lateCount() const { return late_; }

 private:
  uint32_t last_ = 0;
  bool hasLast_ = false;
  uint32_t late_ = 0;
};

struct StreamInfo {
  std::string codec;
  uint32_t sampleRate = 0;
  uint8_t channels = 0;
  uint32_t bitrateKbps = 128;
  uint32_t frameSamples = 1152;
};

// A STREAM_INFO JSON szöveges üzenet. Csak "mp3" codec és ismert mintavétel fogadható el.
bool parseStreamInfo(const char* json, StreamInfo& out);
