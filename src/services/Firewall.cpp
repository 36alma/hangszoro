#include "Firewall.h"

bool Firewall::allowConnect(uint32_t ip, uint16_t port) const {
  return ip_ != 0 && ip == ip_ && port == port_;
}

bool Firewall::verifyRemote(uint32_t remoteIp) const {
  return ip_ != 0 && remoteIp == ip_;
}
