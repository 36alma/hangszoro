#include "PacketParser.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstring>

namespace {
uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
}  // namespace

PacketView parsePacket(const uint8_t* data, size_t len) {
  PacketView v;
  if (!data || len < kPacketHeaderBytes) return v;
  if (memcmp(data, "CSGP", 4) != 0) return v;

  size_t declared = ((size_t)data[13] << 8) | data[14];
  if (len - kPacketHeaderBytes != declared) return v;

  v.ok = true;
  v.seq = be32(data + 4);
  v.timestampMs = be32(data + 8);
  v.status = data[12];
  v.payload = data + kPacketHeaderBytes;
  v.len = declared;
  return v;
}

void SeqTracker::reset() {
  hasLast_ = false;
  last_ = 0;
  late_ = 0;
}

uint32_t SeqTracker::onPacket(uint32_t seq) {
  if (!hasLast_) {
    hasLast_ = true;
    last_ = seq;
    return 0;
  }
  uint32_t expected = last_ + 1u;  // u32: magától körbefordul
  uint32_t delta = seq - expected;
  if (delta < 0x80000000u) {  // előre ugrás (0 = sorrendben)
    last_ = seq;
    return delta;
  }
  late_++;  // duplikátum vagy visszalépés
  return 0;
}

bool parseStreamInfo(const char* json, StreamInfo& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json ? json : "")) return false;
  const JsonDocument& d = doc;

  if (!d["codec"].is<const char*>()) return false;
  std::string codec = d["codec"].as<const char*>();
  for (auto& c : codec) c = (char)tolower((unsigned char)c);
  if (codec != "mp3") return false;

  if (!d["sample_rate"].is<uint32_t>() || !d["channels"].is<int>()) return false;
  uint32_t rate = d["sample_rate"].as<uint32_t>();
  static const uint32_t kRates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
  bool known = false;
  for (uint32_t r : kRates) known = known || (r == rate);
  if (!known) return false;

  int channels = d["channels"].as<int>();
  if (channels != 1 && channels != 2) return false;

  StreamInfo si;
  si.codec = codec;
  si.sampleRate = rate;
  si.channels = (uint8_t)channels;
  if (d["bitrate_kbps"].is<uint32_t>()) {
    uint32_t b = d["bitrate_kbps"].as<uint32_t>();
    if (b >= 8 && b <= 320) si.bitrateKbps = b;
  }
  if (d["frame_samples"].is<uint32_t>()) {
    uint32_t fs = d["frame_samples"].as<uint32_t>();
    if (fs > 0 && fs <= 4608) si.frameSamples = fs;
  }
  out = si;
  return true;
}
