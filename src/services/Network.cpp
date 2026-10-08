#include "Network.h"

#include <WiFi.h>

bool Network::connect(const Config& cfg, uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (!WiFi.config(toIPAddress(cfg.ip), toIPAddress(cfg.gateway), toIPAddress(cfg.mask),
                   toIPAddress(cfg.dns))) {
    Serial.println("[net] WiFi.config failed");
    return false;
  }
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
  Serial.printf("[net] connecting to '%s'...\n", cfg.wifiSsid.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeoutMs) {
      Serial.println("[net] connect timeout");
      return false;
    }
    delay(200);
  }
  Serial.printf("[net] connected, ip=%s gw=%s\n", WiFi.localIP().toString().c_str(),
                WiFi.gatewayIP().toString().c_str());
  return true;
}

bool Network::connected() const { return WiFi.status() == WL_CONNECTED; }

void Network::maintain(const Config& cfg) {
  if (connected()) return;
  uint32_t now = millis();
  if (now - lastAttemptMs_ < 5000) return;
  lastAttemptMs_ = now;
  Serial.println("[net] link down, reconnecting");
  WiFi.reconnect();
}
