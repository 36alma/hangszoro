#pragma once
#include <stdint.h>

// Kimenő allowlist egyetlen endpointra. A 0 IP "nincs feloldva", azt mindig tiltja.
// Az eszköz nem nyit szerverportot, ezért bejövő kapcsolatot nem fogad.
class Firewall {
 public:
  Firewall() = default;
  Firewall(uint32_t allowedIp, uint16_t allowedPort) : ip_(allowedIp), port_(allowedPort) {}

  bool allowConnect(uint32_t ip, uint16_t port) const;
  bool verifyRemote(uint32_t remoteIp) const;
  bool listenAllowed() const { return false; }

 private:
  uint32_t ip_ = 0;
  uint16_t port_ = 0;
};
