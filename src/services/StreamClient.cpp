#include "StreamClient.h"

#include <string.h>
#include <WiFi.h>

#include "../model/Ipv4.h"
#include "Network.h"
#include "../Log.h"

namespace {
uint32_t toHostOrder(const IPAddress& ip) {
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
}
// Újracsatlakozási várakozás: 1, 2, 5, 10, majd 30 s.
constexpr uint32_t kBackoffMs[] = {1000, 2000, 5000, 10000, 30000};
}  // namespace

StreamClient::StreamClient(const Config& cfg, RingBuffer& ring) : cfg_(cfg), ring_(ring) {}

uint32_t StreamClient::backoffMs(uint32_t attempt) {
  size_t idx = attempt == 0 ? 0 : attempt - 1;
  size_t last = sizeof(kBackoffMs) / sizeof(kBackoffMs[0]) - 1;
  return kBackoffMs[idx < last ? idx : last];
}

uint32_t StreamClient::nextBackoffMs() const { return backoffMs(failures_); }

bool StreamClient::begin() {
  IPAddress ip;
  uint32_t literal = 0;
  if (parseIPv4(cfg_.host.c_str(), literal)) {
    ip = toIPAddress(literal);
  } else if (!WiFi.hostByName(cfg_.host.c_str(), ip)) {
    LOGF("[ws] cannot resolve host '%s'\n", cfg_.host.c_str());
    return false;
  }

  uint32_t ipv = toHostOrder(ip);
  firewall_ = Firewall(ipv, cfg_.port);
  if (!firewall_.allowConnect(ipv, cfg_.port)) {
    LOGLN("[fw] endpoint blocked by firewall");
    return false;
  }

  if (!cfg_.token.empty()) {
    authHeader_ = String("Authorization: Bearer ") + cfg_.token.c_str();
    ws_.setExtraHeaders(authHeader_.c_str());
  }
  ws_.onEvent([this](WStype_t t, uint8_t* p, size_t l) { onEvent(t, p, l); });
  ws_.setReconnectInterval(kBackoffMs[0]);
  // IP-literállal csatlakozunk, így a kapcsolat biztosan az allowlistelt címre megy.
  ws_.begin(ip.toString().c_str(), cfg_.port, cfg_.path.c_str());
  LOGF("[ws] connecting to %s:%u%s\n", ip.toString().c_str(), cfg_.port, cfg_.path.c_str());
  return true;
}

void StreamClient::loop() {
  ws_.loop();
  // Vételi figyelő: a szerver folyamatosan küld, így hallgatás = néma kapcsolatvesztés.
  if (ws_.isConnected() && (uint32_t)(millis() - lastRxMs_) > kRxTimeoutMs) {
    LOGLN("[ws] no data for 5 s, reconnecting");
    lastRxMs_ = millis();
    ws_.disconnect();  // a DISCONNECTED kezelő végzi a várakozást és az ürítést
  }
}

void StreamClient::onEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      uint32_t remote = toHostOrder(ws_.remoteIp());
      if (!firewall_.verifyRemote(remote)) {
        LOGLN("[fw] remote IP does not match the allowlist, disconnecting");
        ws_.disconnect();
        return;
      }
      LOGLN("[ws] connected");
      lastRxMs_ = millis();
      failures_ = 0;
      ws_.setReconnectInterval(kBackoffMs[0]);
      seq_.reset();
      lastStatus_ = 0xFF;
      flush_ = true;  // friss kezdés: nincs régi adat
      break;
    }
    case WStype_DISCONNECTED: {
      failures_++;
      uint32_t backoff = nextBackoffMs();
      ws_.setReconnectInterval(backoff);
      flush_ = true;  // leáll a lejátszás, törlődik a puffer
      // A könyvtár a kézfogás-hiba okát a payloadban adja (pl. "WebSocket handshake failed - HTTP 403").
      char reason[96] = "";
      if (payload != nullptr && length > 0) {
        size_t n = length < sizeof(reason) - 1 ? length : sizeof(reason) - 1;
        memcpy(reason, payload, n);
        reason[n] = '\0';
      }
      if (reason[0] != '\0') {
        LOGF("[ws] disconnected (attempt %u, reason: %s), retry in %u ms\n",
                      (unsigned)failures_, reason, (unsigned)backoff);
        if (strstr(reason, "HTTP 403") != nullptr)
          LOGLN(
              "[ws] server rejected the handshake (HTTP 403): the token or the registered IP of "
              "this device is invalid, expired, or the device is disabled");
      } else {
        LOGF("[ws] disconnected (attempt %u), retry in %u ms\n", (unsigned)failures_,
                      (unsigned)backoff);
      }
      if (failures_ >= 5)
        LOGLN("[ws] repeated failures: check endpoint, static IP, network and token");
      break;
    }
    case WStype_TEXT: {
      lastRxMs_ = millis();
      StreamInfo si;
      if (parseStreamInfo((const char*)payload, si)) {
        bitrateKbps_ = si.bitrateKbps;
        LOGF("[ws] STREAM_INFO %s %u Hz %uch %u kbps\n", si.codec.c_str(),
                      (unsigned)si.sampleRate, (unsigned)si.channels, (unsigned)si.bitrateKbps);
      } else {
        LOGLN("[ws] unsupported or invalid STREAM_INFO");
      }
      break;
    }
    case WStype_BIN:
      lastRxMs_ = millis();
      onBinary(payload, length);
      break;
    default:
      break;
  }
}

void StreamClient::onBinary(const uint8_t* data, size_t length) {
  PacketView v = parsePacket(data, length);
  if (!v.ok) {  // hibás magic vagy hossz: a csomagot eldobjuk
    if (++malformed_ % 100 == 1) LOGF("[ws] malformed packets: %u\n", (unsigned)malformed_);
    return;
  }
  if (v.status != lastStatus_) {
    lastStatus_ = v.status;
    LOGF("[ws] mixer status=0x%02X\n", v.status);
  }
  uint32_t gap = seq_.onPacket(v.seq);
  if (gap) {
    lost_ += gap;
    LOGF("[ws] seq gap %u, total %u\n", (unsigned)gap, (unsigned)lost_);
  }
  if (v.len == 0) return;
  size_t written = ring_.write(v.payload, v.len);
  if (written < v.len) {  // túlcsordulás: szinkronvesztés, a puffert eldobjuk
    dropped_ += (uint32_t)(v.len - written);
    flush_ = true;
    LOGF("[ws] buffer overflow, flushing (dropped %u bytes total)\n", (unsigned)dropped_);
  }
}
