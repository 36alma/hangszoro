#include "Network.h"

#include <WiFi.h>

#include "../Log.h"

bool Network::connect(const Config& cfg, uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg.hostname.c_str());  // a WiFi.config()/begin() előtt kell
  WiFi.setAutoReconnect(true);
  if (!WiFi.config(toIPAddress(cfg.ip), toIPAddress(cfg.gateway), toIPAddress(cfg.mask),
                   toIPAddress(cfg.dns))) {
    LOGLN("[net] WiFi.config failed");
    return false;
  }
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
  LOGF("[net] connecting to '%s'...\n", cfg.wifiSsid.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeoutMs) {
      LOGLN("[net] connect timeout");
      return false;
    }
    delay(200);
  }
  LOGF("[net] connected, hostname=%s ip=%s gw=%s\n", WiFi.getHostname(),
                WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());
  return true;
}

bool Network::connected() const { return WiFi.status() == WL_CONNECTED; }

void Network::maintain(const Config& cfg) {
  if (connected()) return;
  uint32_t now = millis();
  if (now - lastAttemptMs_ < 5000) return;
  lastAttemptMs_ = now;
  LOGLN("[net] link down, reconnecting");
  WiFi.reconnect();
}
