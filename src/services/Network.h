#pragma once
#include <Arduino.h>
#include <IPAddress.h>

#include "../model/Config.h"

inline IPAddress toIPAddress(uint32_t v) {
  return IPAddress((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

class Network {
 public:
  // Statikus IP-vel csatlakozik a WiFi-hez. Időtúllépéskor false.
  bool connect(const Config& cfg, uint32_t timeoutMs = 20000);
  bool connected() const;
  // A loop()-ból hívandó: kiesés után 5 másodpercenként újracsatlakozik.
  void maintain(const Config& cfg);

 private:
  uint32_t lastAttemptMs_ = 0;
};
