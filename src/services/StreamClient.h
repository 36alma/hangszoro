#pragma once
#include <Arduino.h>
#include <WebSocketsClient.h>

#include <atomic>

#include "../model/Config.h"
#include "Firewall.h"
#include "PacketParser.h"
#include "RingBuffer.h"

// A WebSocketsClient alosztálya, hogy a kapcsolat távoli IP-je lekérdezhető legyen.
class RemoteAwareWs : public WebSocketsClient {
 public:
  IPAddress remoteIp() { return _client.tcp ? _client.tcp->remoteIP() : IPAddress(); }
};

class StreamClient {
 public:
  StreamClient(const Config& cfg, RingBuffer& ring);

  // Feloldja a hostot, felépíti a tűzfalat, elindítja a kapcsolatot. False, ha nem sikerült.
  bool begin();
  // A loop()-ból hívandó.
  void loop();
  bool connected() { return ws_.isConnected(); }

  // A dekóder task hívja: igaz, ha ürítenie kell a puffert és a dekódert.
  bool takeFlushRequest() { return flush_.exchange(false); }
  uint32_t bitrateKbps() const { return bitrateKbps_.load(); }

 private:
  void onEvent(WStype_t type, uint8_t* payload, size_t length);
  void onBinary(const uint8_t* data, size_t length);
  uint32_t nextBackoffMs() const;

  const Config& cfg_;
  RingBuffer& ring_;
  RemoteAwareWs ws_;
  Firewall firewall_;
  SeqTracker seq_;
  String authHeader_;
  std::atomic<bool> flush_{false};
  std::atomic<uint32_t> bitrateKbps_{128};
  uint32_t failures_ = 0;
  uint32_t dropped_ = 0;
  uint32_t malformed_ = 0;
  uint32_t lost_ = 0;
  uint8_t lastStatus_ = 0xFF;
};
