#include "Config.h"

#include <ArduinoJson.h>

#include "Ipv4.h"
#include "config_defaults.h"

namespace {

using Warnings = std::vector<std::string>;

void warn(Warnings& w, const std::string& msg) { w.push_back("config.json: " + msg); }

bool hasControl(const std::string& s) {
  for (unsigned char c : s)
    if (c < 0x20 || c == 0x7F) return true;
  return false;
}

bool hasControlOrSpace(const std::string& s) {
  return hasControl(s) || s.find(' ') != std::string::npos;
}

bool readString(JsonVariantConst v, const char* name, std::string& out, Warnings& w) {
  if (v.isNull()) return false;
  if (!v.is<const char*>()) {
    warn(w, std::string(name) + " must be a string");
    return false;
  }
  out = v.as<const char*>();
  return true;
}

bool readIp(JsonVariantConst v, const char* name, uint32_t& out, Warnings& w) {
  std::string s;
  if (!readString(v, name, s, w)) return false;
  uint32_t ip = 0;
  if (!parseIPv4(s.c_str(), ip)) {
    warn(w, std::string(name) + " is not a valid IPv4 address: " + s);
    return false;
  }
  out = ip;
  return true;
}

bool readInt(JsonVariantConst v, const char* name, long lo, long hi, long& out, Warnings& w) {
  if (v.isNull()) return false;
  if (!v.is<long>()) {
    warn(w, std::string(name) + " must be an integer");
    return false;
  }
  long n = v.as<long>();
  if (n < lo || n > hi) {
    warn(w, std::string(name) + " is out of range");
    return false;
  }
  out = n;
  return true;
}

}  // namespace

Config defaultConfig() {
  Config c;
  c.wifiSsid = CFG_WIFI_SSID;
  c.wifiPassword = CFG_WIFI_PASSWORD;
  parseIPv4(CFG_NET_IP, c.ip);
  parseIPv4(CFG_NET_MASK, c.mask);
  parseIPv4(CFG_NET_GATEWAY, c.gateway);
  parseIPv4(CFG_NET_DNS, c.dns);
  c.host = CFG_ENDPOINT_HOST;
  c.port = CFG_ENDPOINT_PORT;
  c.path = CFG_ENDPOINT_PATH;
  c.token = CFG_ENDPOINT_TOKEN;
  c.bck = CFG_I2S_BCK;
  c.lrck = CFG_I2S_LRCK;
  c.din = CFG_I2S_DIN;
  c.bufferMs = CFG_BUFFER_MS;
  c.prebufferMs = CFG_PREBUFFER_MS;
  return c;
}

Warnings applyJson(Config& cfg, const char* json) {
  Warnings w;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json ? json : "");
  if (err) {
    warn(w, std::string("invalid JSON: ") + err.c_str());
    return w;
  }
  if (!doc.is<JsonObjectConst>()) {
    warn(w, "top level must be an object");
    return w;
  }
  const JsonDocument& root = doc;

  // wifi
  {
    JsonVariantConst wifi = root["wifi"];
    std::string s;
    if (readString(wifi["ssid"], "wifi.ssid", s, w)) {
      if (s.empty() || s.size() > 32) warn(w, "wifi.ssid must be 1..32 bytes");
      else cfg.wifiSsid = s;
    }
    if (readString(wifi["password"], "wifi.password", s, w)) {
      if (s.size() > 63) warn(w, "wifi.password must be at most 63 bytes");
      else cfg.wifiPassword = s;
    }
  }

  // net: egy csoport
  {
    JsonVariantConst net = root["net"];
    uint32_t ip = cfg.ip, mask = cfg.mask, gw = cfg.gateway, dns = cfg.dns;
    bool any = false;
    any |= readIp(net["ip"], "net.ip", ip, w);
    any |= readIp(net["mask"], "net.mask", mask, w);
    any |= readIp(net["gateway"], "net.gateway", gw, w);
    any |= readIp(net["dns"], "net.dns", dns, w);
    if (any) {
      if (!isValidMask(mask)) {
        warn(w, "net.mask is not a valid subnet mask; net group ignored");
      } else if (!sameSubnet(ip, gw, mask)) {
        warn(w, "net.gateway must be in the same subnet as net.ip; net group ignored");
      } else {
        cfg.ip = ip;
        cfg.mask = mask;
        cfg.gateway = gw;
        cfg.dns = dns;
      }
    }
  }

  // endpoint
  {
    JsonVariantConst ep = root["endpoint"];
    std::string s;
    long n = 0;
    if (readString(ep["host"], "endpoint.host", s, w)) {
      if (s.empty() || s.size() > 253 || hasControlOrSpace(s)) warn(w, "endpoint.host is invalid");
      else cfg.host = s;
    }
    if (readInt(ep["port"], "endpoint.port", 1, 65535, n, w)) cfg.port = (uint16_t)n;
    if (readString(ep["path"], "endpoint.path", s, w)) {
      if (s.empty() || s[0] != '/' || s.size() > 256 || hasControlOrSpace(s))
        warn(w, "endpoint.path must start with '/' and contain no spaces or control characters");
      else cfg.path = s;
    }
    if (readString(ep["token"], "endpoint.token", s, w)) {
      if (s.size() > 512 || hasControlOrSpace(s))
        warn(w, "endpoint.token must not contain spaces or control characters");
      else cfg.token = s;
    }
  }

  // i2s: egy csoport, a tűk különbözők
  {
    JsonVariantConst i2s = root["i2s"];
    long bck = cfg.bck, lrck = cfg.lrck, din = cfg.din;
    bool any = false;
    any |= readInt(i2s["bck"], "i2s.bck", 0, 48, bck, w);
    any |= readInt(i2s["lrck"], "i2s.lrck", 0, 48, lrck, w);
    any |= readInt(i2s["din"], "i2s.din", 0, 48, din, w);
    if (any) {
      if (bck == lrck || bck == din || lrck == din) {
        warn(w, "i2s pins must be distinct; i2s group ignored");
      } else {
        cfg.bck = (int)bck;
        cfg.lrck = (int)lrck;
        cfg.din = (int)din;
      }
    }
  }

  // buffer: egy csoport, az előpuffer nem lehet nagyobb a pufferenél
  {
    JsonVariantConst buf = root["buffer"];
    long ms = cfg.bufferMs, pre = cfg.prebufferMs;
    bool any = false;
    any |= readInt(buf["ms"], "buffer.ms", 50, 5000, ms, w);
    any |= readInt(buf["prebuffer_ms"], "buffer.prebuffer_ms", 20, 5000, pre, w);
    if (any) {
      if (pre > ms) {
        warn(w, "buffer.prebuffer_ms must not exceed buffer.ms; buffer group ignored");
      } else {
        cfg.bufferMs = (uint32_t)ms;
        cfg.prebufferMs = (uint32_t)pre;
      }
    }
  }

  return w;
}
