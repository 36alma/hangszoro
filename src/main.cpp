#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

#include <atomic>

#include "AudioGeneratorMP3.h"
#include "Display.h"
#include "Log.h"
#include "Stats.h"
#include "StreamSource.h"
#include "config_defaults.h"

// Lánc: WebSocket (ws://<szerver>:8766/api/mixer/stream, Bearer token)
//       -> "CSGP" csomagok -> puffer -> MP3 dekóder -> PCM -> I2S -> PCM5102
//
// Belépés: a szerver a tokent ÉS a forrás IP-t ellenőrzi (az Eszközök fülön felvett IP).
// Interneten át (NAT mögül) a szerver a router PUBLIKUS IP-jét látja, azt kell felvenni.

struct Config {
  String wifiSsid = CFG_WIFI_SSID;
  String wifiPassword = CFG_WIFI_PASSWORD;
  String deviceName = CFG_DEVICE_NAME;
  String hostname = CFG_DEVICE_HOSTNAME;
  String ip = CFG_NET_IP;  // üres => DHCP
  String mask = CFG_NET_MASK;
  String gateway = CFG_NET_GATEWAY;
  String dns = CFG_NET_DNS;
  String host = CFG_ENDPOINT_HOST;  // IP vagy hosztnév (internetes cím is lehet)
  uint16_t port = CFG_ENDPOINT_PORT;
  String path = CFG_ENDPOINT_PATH;
  String token = CFG_ENDPOINT_TOKEN;
  int bck = CFG_I2S_BCK, lrck = CFG_I2S_LRCK, din = CFG_I2S_DIN;
  int oledSda = CFG_OLED_SDA, oledScl = CFG_OLED_SCL;
};

constexpr size_t kRingBytes = 32 * 1024;
constexpr size_t kPrebufferBytes = 6 * 1024;  // ennyi adat kell a (újra)induláshoz
constexpr size_t kHeaderBytes = 15;           // "CSGP"(4) seq(4) ts(4) status(1) len(2)
constexpr uint32_t kRxTimeoutMs = 5000;       // a mixer csend alatt is küld; hallgatás = hiba
constexpr uint32_t kIdleDrainMs = 1500;       // ennyi adatmentes idő után a prebuffer alatti maradékot ürítjük
constexpr uint8_t kStatusIdle = 0;            // MixerStatus.IDLE a csomagfejlécben
constexpr float kGain = 0.5f;

Config cfg;
WebSocketsClient ws;
StreamSource* ring = nullptr;
AudioGeneratorMP3* mp3 = nullptr;
TapOutput* out = nullptr;
String authHeader;
// WebSocket task (0-s mag) tulajdona:
uint32_t lastRxMs = 0;
uint32_t wsFailures = 0;
uint32_t lastSeq = 0;
bool haveSeq = false;
// Dekóder task (1-es mag) tulajdona:
bool streaming = false;  // a dekóder fut
// A WS task kéri, a dekóder task végrehajtja (dekóder leállítás + puffer ürítés).
std::atomic<bool> flushReq{false};
std::atomic<uint32_t> lastDataMs{0};  // az utolsó pufferbe írt MP3 adat ideje (ws task írja)

void requestFlush() { flushReq = true; }
void wsTask(void*);
void decoderTask(void*);

void loadConfig() {
  if (!LittleFS.begin(false)) {
    LOGLN("[fs] LittleFS mount failed, defaults in use");
    return;
  }
  File f = LittleFS.open("/config.json", "r");
  if (!f) {
    LOGLN("[cfg] /config.json not found, defaults in use");
    return;
  }
  JsonDocument d;
  DeserializationError err = deserializeJson(d, f);
  f.close();
  if (err) {
    LOGF("[cfg] config.json: %s\n", err.c_str());
    return;
  }
  auto str = [&](JsonVariantConst v, String& dst) {
    if (v.is<const char*>()) dst = v.as<const char*>();
  };
  str(d["device"]["name"], cfg.deviceName);
  str(d["device"]["hostname"], cfg.hostname);
  str(d["wifi"]["ssid"], cfg.wifiSsid);
  str(d["wifi"]["password"], cfg.wifiPassword);
  str(d["net"]["ip"], cfg.ip);
  str(d["net"]["mask"], cfg.mask);
  str(d["net"]["gateway"], cfg.gateway);
  str(d["net"]["dns"], cfg.dns);
  str(d["endpoint"]["host"], cfg.host);
  str(d["endpoint"]["path"], cfg.path);
  str(d["endpoint"]["token"], cfg.token);
  if (d["endpoint"]["port"].is<int>()) cfg.port = d["endpoint"]["port"].as<int>();
  if (d["i2s"]["bck"].is<int>()) cfg.bck = d["i2s"]["bck"].as<int>();
  if (d["i2s"]["lrck"].is<int>()) cfg.lrck = d["i2s"]["lrck"].as<int>();
  if (d["i2s"]["din"].is<int>()) cfg.din = d["i2s"]["din"].as<int>();
  if (d["oled"]["sda"].is<int>()) cfg.oledSda = d["oled"]["sda"].as<int>();
  if (d["oled"]["scl"].is<int>()) cfg.oledScl = d["oled"]["scl"].as<int>();
  LOGLN("[cfg] loaded");
}

bool connectWifi(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg.hostname.c_str());
  WiFi.setAutoReconnect(true);
  if (cfg.ip.length() > 0) {  // fix IP; üresen DHCP
    IPAddress ip, mask, gw, dns;
    if (ip.fromString(cfg.ip) && mask.fromString(cfg.mask) && gw.fromString(cfg.gateway) &&
        dns.fromString(cfg.dns)) {
      WiFi.config(ip, gw, mask, dns);
    } else {
      LOGLN("[net] invalid static IP settings, using DHCP");
    }
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
  LOGF("[net] connected, ip=%s gw=%s\n", WiFi.localIP().toString().c_str(),
       WiFi.gatewayIP().toString().c_str());
  return true;
}

void stopDecoder() {
  if (mp3 && mp3->isRunning()) mp3->stop();
  streaming = false;
}

// Csomag: 15 bájtos big-endian fejléc + MP3 payload. Hibás csomagot eldobunk.
void onBinary(const uint8_t* d, size_t len) {
  size_t declared = len >= kHeaderBytes ? (((size_t)d[13] << 8) | d[14]) : 0;
  if (len < kHeaderBytes || memcmp(d, "CSGP", 4) != 0 || len - kHeaderBytes != declared) {
    gStats.malformed++;
    return;
  }
  uint32_t seq = ((uint32_t)d[4] << 24) | ((uint32_t)d[5] << 16) | ((uint32_t)d[6] << 8) | d[7];
  if (haveSeq) {
    uint32_t gap = seq - (lastSeq + 1u);
    if (gap < 0x80000000u) { gStats.lost += gap; lastSeq = seq; }
  } else {
    haveSeq = true;
    lastSeq = seq;
  }
  gStats.mixStatus = d[12];
  static uint8_t lastStatus = 0xFF;
  if (d[12] != lastStatus) {
    static const char* const kNames[] = {"IDLE", "MUSIC", "MIC", "MUSIC+MIC", "BELL"};
    LOGF("[mix] status: %s -> %s\n", lastStatus < 5 ? kNames[lastStatus] : "-",
         d[12] < 5 ? kNames[d[12]] : "?");
    lastStatus = d[12];
  }
  // IDLE (semmi nem szól): a csend-csomagokat eldobjuk, és az IDLE-be váltáskor egyszer
  // leállítjuk a dekódert + ürítjük a puffert. Nem-IDLE csomagnál a prebuffer után újraindul.
  static bool wasIdle = false;
  bool idle = d[12] == kStatusIdle;
  if (idle) {
    if (!wasIdle) requestFlush();
    wasIdle = true;
    return;
  }
  wasIdle = false;
  if (declared == 0 || flushReq) return;  // ürítés közben nem írunk a pufferbe
  size_t w = ring->write(d + kHeaderBytes, declared);
  if (w > 0) lastDataMs = millis();
  if (w < declared) {  // túlcsordulás: szinkronvesztés, újrakezdjük
    gStats.dropped += declared - w;
    LOGLN("[ws] buffer overflow, flushing");
    requestFlush();
  }
}

void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      LOGLN("[ws] connected");
      wsFailures = 0;
      gStats.wsFailures = 0;
      gStats.wsConnected = true;
      haveSeq = false;
      lastRxMs = millis();
      requestFlush();
      break;
    case WStype_DISCONNECTED: {
      wsFailures++;
      gStats.wsFailures = wsFailures;
      gStats.wsConnected = false;
      requestFlush();
      // Kézfogás-hiba esetén a könyvtár az okot a payloadban adja (pl. "... HTTP 403").
      char reason[96] = "";
      if (payload && length > 0) {
        size_t n = length < sizeof(reason) - 1 ? length : sizeof(reason) - 1;
        memcpy(reason, payload, n);
        reason[n] = '\0';
      }
      LOGF("[ws] disconnected (attempt %u) %s\n", (unsigned)wsFailures, reason);
      if (strstr(reason, "403"))
        LOGLN("[ws] rejected: bad/expired token, or this device's public IP is not the registered one");
      break;
    }
    case WStype_TEXT: {  // STREAM_INFO: a bitrátát a kijelző mutatja
      lastRxMs = millis();
      gStats.lastRxMs = lastRxMs;
      JsonDocument si;
      if (!deserializeJson(si, (const char*)payload) && si["bitrate_kbps"].is<uint32_t>())
        gStats.bitrateKbps = si["bitrate_kbps"].as<uint32_t>();
      LOGF("[ws] %s\n", (const char*)payload);
      break;
    }
    case WStype_BIN:
      lastRxMs = millis();
      gStats.lastRxMs = lastRxMs;
      onBinary(payload, length);
      break;
    default:
      break;
  }
}

void startWs() {
  if (cfg.token.length() > 0) {
    authHeader = "Authorization: Bearer " + cfg.token;
    ws.setExtraHeaders(authHeader.c_str());
  }
  ws.onEvent(onWsEvent);
  ws.setReconnectInterval(2000);
  ws.begin(cfg.host.c_str(), cfg.port, cfg.path.c_str());
  LOGF("[ws] connecting to %s:%u%s\n", cfg.host.c_str(), cfg.port, cfg.path.c_str());
}

void setup() {
  LOG_BEGIN(115200);
  delay(300);
  LOGLN("[boot] hangszoro");

  loadConfig();

  ring = new StreamSource(kRingBytes);
  if (!ring->ok()) {
    LOGLN("[mem] ring buffer alloc failed");
    return;
  }
  out = new TapOutput();
  out->SetPinout(cfg.bck, cfg.lrck, cfg.din);
  out->SetGain(kGain);
  mp3 = new AudioGeneratorMP3();
  gStats.ringCap = kRingBytes;
  displayBegin(cfg.oledSda, cfg.oledScl, cfg.deviceName, cfg.ip);

  connectWifi(20000);  // ha elbukik, a wsTask várja meg az újracsatlakozást
  startWs();

  // Dekóder: 1-es mag, magasabb prioritás, hogy a hálózat ne akassza meg az I2S-t.
  // WebSocket + WiFi állapot: 0-s mag (a WiFi stack is ott fut).
  if (xTaskCreatePinnedToCore(decoderTask, "decoder", 10240, nullptr, 3, nullptr, 1) != pdPASS ||
      xTaskCreatePinnedToCore(wsTask, "ws", 8192, nullptr, 2, nullptr, 0) != pdPASS) {
    LOGLN("[boot] task start failed");
  }
}

// WebSocket task: hálózat, csomagok -> StreamBuffer.
void wsTask(void*) {
  for (;;) {
    // WiFi nélkül nincs értelme a WebSocketnek; az újracsatlakozást a setAutoReconnect végzi
    // (a kézi WiFi.reconnect() ütközik vele: "sta is connecting, return error").
    if (WiFi.status() != WL_CONNECTED) {
      static uint32_t lastLog = 0;
      if (millis() - lastLog > 5000) {
        lastLog = millis();
        LOGF("[net] no WiFi (status=%d), check ssid/password in config.json (2.4 GHz only)\n",
             (int)WiFi.status());
      }
      requestFlush();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    ws.loop();

    // Néma kapcsolat: újracsatlakozás.
    if (ws.isConnected() && millis() - lastRxMs > kRxTimeoutMs) {
      LOGLN("[ws] no data for 5 s, reconnecting");
      lastRxMs = millis();
      ws.disconnect();
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// Dekóder task: StreamBuffer -> MP3 -> PCM -> I2S. A dekóder és a puffer ürítése itt él.
void decoderTask(void*) {
  for (;;) {
    gStats.ringUsed = ring->available();

    if (flushReq) {
      stopDecoder();
      ring->clear();
      flushReq = false;
      continue;
    }

    if (streaming) {
      if (!mp3->isRunning() || !mp3->loop()) {  // elfogyott az adat: újra előpufferelünk
        LOGLN("[mp3] underrun, rebuffering");
        stopDecoder();
      }
    } else if (ring->available() >= kPrebufferBytes) {
      streaming = mp3->begin(ring, out);
      if (!streaming) LOGLN("[mp3] begin failed");
    } else {
      // A stream elhallgatott, de a puffer a prebuffer alatt ragadt (~18%): nem indul el,
      // nem is ürül magától. Elavult maradék, eldobjuk, hogy újracsatlakozáskor tiszta legyen.
      if (ring->available() > 0 && millis() - lastDataMs > kIdleDrainMs) {
        LOGLN("[buf] idle, draining leftover");
        ring->clear();
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }  // minden munka a task-okban fut
