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

// A kapcsolat állapota és számlálói, bármelyik taskból olvasható pillanatkép.
struct StreamStats {
  bool connected;      // a tűzfal-ellenőrzés után kapcsolódva
  uint32_t failures;   // egymás utáni szétkapcsolások (sikeres kapcsolatnál 0)
  uint32_t dropped;    // túlcsordulás miatt eldobott bájtok
  uint32_t malformed;  // hibás csomagok
  uint32_t lost;       // a szekvenciaszámokból hiányzó csomagok
  uint32_t lastRxMs;   // millis() az utolsó fogadott üzenetkor
};

class StreamClient {
 public:
  StreamClient(const Config& cfg, RingBuffer& ring);

  // Feloldja a hostot, felépíti a tűzfalat, elindítja a kapcsolatot. False, ha nem sikerült.
  bool begin();
  // A loop()-ból hívandó.
  void loop();
  bool connected() { return ws_.isConnected(); }
  // Bármelyik taskból hívható (pl. a debug kijelzőről).
  StreamStats stats() const;

  // A dekóder task hívja: igaz, ha ürítenie kell a puffert és a dekódert.
  bool takeFlushRequest() { return flush_.exchange(false); }
  uint32_t bitrateKbps() const { return bitrateKbps_.load(); }

  // A WiFi-kapcsolat elveszett: ürítést kér (a loop() nem hívja a ws-t, amíg nincs hálózat).
  void onLinkDown() { flush_ = true; }

  // Várakozás (ms) az attempt-edik egymás utáni hiba után: 1, 2, 5, 10, majd 30 s.
  static uint32_t backoffMs(uint32_t attempt);

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
  // A loop() taskja írja, a debug kijelző taskja olvassa.
  std::atomic<bool> connected_{false};
  std::atomic<uint32_t> failures_{0};
  std::atomic<uint32_t> dropped_{0};
  std::atomic<uint32_t> malformed_{0};
  std::atomic<uint32_t> lost_{0};
  uint8_t lastStatus_ = 0xFF;
  std::atomic<uint32_t> lastRxMs_{0};
  static constexpr uint32_t kRxTimeoutMs = 5000;
};
