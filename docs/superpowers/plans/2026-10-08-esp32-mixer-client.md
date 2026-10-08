# ESP32 mixer-kliens – megvalósítási terv

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Az ESP32-S3 a mixer `WS /api/mixer/stream` végpontjáról MP3-at fogad, dekódolja, és I2S-en PCM5102A DAC-ra játssza; a hálózat és az endpoint config fájlból állítható, és csak az endpointtal kommunikál.

**Architecture:** A tiszta logika (config, IPv4, firewall, csomag-kicsomagolás, ring buffer) hardverfüggetlen C++, natív unit tesztekkel. Az eszközfüggő réteg (WiFi, I2S, MP3 dekóder, WebSocket) ezekre épül. Adatfolyam: WS csomag → ring buffer → dekóder task → I2S.

**Tech Stack:** PlatformIO, Arduino-ESP32, ArduinoJson 7, `links2004/WebSockets`, `pschatzmann/arduino-libhelix`, legacy `driver/i2s.h`, Unity (natív tesztek).

**Spec:** `docs/superpowers/specs/2026-10-08-esp32-mixer-client-design.md`

> **Git:** a projekt jelenleg nem git repo. A "Commit" lépések `git init` után érvényesek; ha a felhasználó nem akar repót, ezeket ki kell hagyni.

## Global Constraints

- Hang kimenet: PCM5102A DAC, I2S.
- Config: `include/` alatti alapértékek + `data/config.json` (LittleFS) felülírja azokat.
- Tűzfal: csak kliens; kimenő allowlist az endpoint IP/port párra; a kapcsolat után remote IP ellenőrzés; nincs szerverport.
- Hitelesítés: `Authorization: Bearer <token>` header a WebSocket handshake-ben.
- Végpont: `ws://<szerver-ip>:8766/api/mixer/stream`, sima WebSocket (nincs TLS); az ESP32 csak hallgat, semmit nem küld; az első üzenet `STREAM_INFO` JSON (`codec`, `sample_rate`, `channels`, `bitrate_kbps`, `frame_samples`), utána bináris csomagok.
- Csomag (big-endian): 15 bájtos fejléc = `magic` "CSGP" (4) + `seq` u32 (4) + `timestamp_ms` u32 (4) + `status` u8 (1) + `length` u16 (2), utána `length` bájt MP3. Más magic vagy a payload tényleges hosszától eltérő `length` esetén a csomag eldobandó. `status` bitek: 1 zene, 2 mikrofon, 4 csengő.
- A szerver 403-mal utasít el hibás tokent, ismeretlen IP-t, letiltott vagy lejárt eszközt; 1008 kóddal zárhat. A token sosem kerül naplóba.
- Struktúra: `src/model/Config`, `src/services/{Network,Firewall,StreamClient,Decoder,AudioOut}`, `src/main.cpp` csak indít.
- Config mezők: `wifi`, `net` (`ip`, `mask`, `gateway`, `dns`), `endpoint` (`host`, `port`, `path`, `token`), `i2s` (`bck`, `lrck`, `din`), `buffer` (`ms`, `prebuffer_ms`).
- Natív tesztek: `pio test -e native`.

### Eltérések a specifikációtól (a felhasználó jóváhagyása kell)

1. **`include/config_defaults.h`** a spec `include/config.h` helyett: Windows alatt a `config.h` és a `src/model/Config.h` fájlnév ütközik (kis/nagybetűre nem érzékeny), az include saját magát találná meg.
2. **Legacy `driver/i2s.h`** az új `i2s_std` helyett: a stock `platform = espressif32` Arduino 2.x / IDF 4.4 magot használ, ahol `i2s_std` nincs. A legacy driver 2.x és 3.x alatt is működik.
3. **Elutasított kézfogás (403):** a `links2004/WebSockets` nem adja ki a handshake HTTP státuszát, ezért a 403 nem különböztethető meg a hálózati hibától. Mindkettőre visszalépés van (1, 2, 5, 10, majd 30 s), és ismételt hibánál egyértelmű naplóüzenet. A szerver leírása szerinti állapotjelző LED nincs benne (YAGNI); a napló jelzi az állapotot.
4. **A token a `config.json`-ban** (LittleFS) van, nem NVS-ben, mert te a config fájlos megoldást kérted. A szerver leírása NVS-t javasol; ha később fontos, külön feladatként átvihető. A `data/config.json` nem kerülhet verziókezelésbe.

## Review Focus

1. Hibás, üres vagy `nullptr` config JSON: alapértékekkel indul, figyelmeztet (Task 2).
2. Érvénytelen IP (`256.1.1.1`, `1.2.3`, rossz típus): a mező elutasítva, az alapérték marad (Task 1, 2).
3. Token `\r\n`-nel (header injection): elutasítva (Task 2).
4. Hibás csomag: rossz magic, a fejlécnél rövidebb, a `length` nem egyezik a tényleges hosszal, üres payload: eldobja, nem omlik össze, nem ír a pufferbe (Task 4).
5. Szekvenciaszám körbefordulás, duplikátum, visszalépés: nem számít kihagyásnak (Task 4); ring buffer túlcsordulás részleges írással (Task 5), amit a StreamClient puffer-ürítéssel kezel (Task 8 kézi lista); feloldatlan (0) IP-nél a tűzfal mindent tilt (Task 3).

## Fájlok

| Fájl | Felelősség |
|---|---|
| `platformio.ini` | eszköz és natív környezet, függőségek |
| `include/config_defaults.h` | alapértékek makrókként |
| `data/config.json` | felülíró config (LittleFS) |
| `src/model/Ipv4.h` | IPv4 parszolás, maszk és alhálózat ellenőrzés |
| `src/model/Config.h/.cpp` | `Config` struktúra, `defaultConfig()`, `applyJson()` |
| `src/services/Firewall.h/.cpp` | allowlist szabályok |
| `src/services/PacketParser.h/.cpp` | csomag kicsomagolás, `SeqTracker`, `STREAM_INFO` parszolás |
| `src/services/RingBuffer.h/.cpp` | SPSC bájt ring buffer |
| `src/services/Network.h/.cpp` | WiFi statikus IP-vel |
| `src/services/AudioOut.h/.cpp` | I2S kimenet |
| `src/services/Decoder.h/.cpp` | MP3 → PCM (libhelix) |
| `src/services/StreamClient.h/.cpp` | WebSocket kliens |
| `src/main.cpp` | indítás, dekóder task |
| `test/test_*/test_main.cpp` | natív tesztek |

---

### Task 1: Projektváz és IPv4 segédek

**Files:**
- Modify: `platformio.ini`
- Create: `include/config_defaults.h`
- Create: `src/model/Ipv4.h`
- Test: `test/test_ipv4/test_main.cpp`

**Interfaces:**
- Produces:
  - `bool parseIPv4(const char* s, uint32_t& out)` (host byte order, `a.b.c.d` → `a<<24|b<<16|c<<8|d`)
  - `bool isValidMask(uint32_t mask)`
  - `bool sameSubnet(uint32_t a, uint32_t b, uint32_t mask)`
  - a `CFG_*` makrók a `config_defaults.h`-ban

- [ ] **Step 1: `platformio.ini` cseréje**

```ini
[env:rymcu-esp32-s3-devkitc-1]
platform = espressif32
board = rymcu-esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
board_build.filesystem = littlefs
lib_deps =
    bblanchon/ArduinoJson@^7.0.0
    links2004/WebSockets@^2.4.0
    pschatzmann/arduino-libhelix

[env:native]
platform = native
test_build_src = yes
build_src_filter =
    -<*>
    +<model/*.cpp>
    +<services/Firewall.cpp>
    +<services/PacketParser.cpp>
    +<services/RingBuffer.cpp>
lib_deps =
    bblanchon/ArduinoJson@^7.0.0
build_flags = -std=gnu++17
```

A natív környezethez Windows alatt gcc kell a PATH-on (pl. MinGW-w64 vagy MSYS2). Ellenőrzés: `gcc --version`.

- [ ] **Step 2: `include/config_defaults.h` létrehozása**

```cpp
#pragma once
// Alapértékek. A data/config.json felülírja őket.

#define CFG_WIFI_SSID "changeme"
#define CFG_WIFI_PASSWORD "changeme"

#define CFG_NET_IP "192.168.1.50"
#define CFG_NET_MASK "255.255.255.0"
#define CFG_NET_GATEWAY "192.168.1.1"
#define CFG_NET_DNS "192.168.1.1"

#define CFG_ENDPOINT_HOST "192.168.1.10"
#define CFG_ENDPOINT_PORT 8766
#define CFG_ENDPOINT_PATH "/api/mixer/stream"
#define CFG_ENDPOINT_TOKEN ""

#define CFG_I2S_BCK 15
#define CFG_I2S_LRCK 16
#define CFG_I2S_DIN 17

#define CFG_BUFFER_MS 500
#define CFG_PREBUFFER_MS 150
```

- [ ] **Step 3: Write the failing test** – `test/test_ipv4/test_main.cpp`

```cpp
#include <unity.h>
#include "model/Ipv4.h"

void setUp() {}
void tearDown() {}

void test_parse_valid() {
  uint32_t ip = 0;
  TEST_ASSERT_TRUE(parseIPv4("192.168.1.50", ip));
  TEST_ASSERT_EQUAL_UINT32(0xC0A80132u, ip);
  TEST_ASSERT_TRUE(parseIPv4("0.0.0.0", ip));
  TEST_ASSERT_EQUAL_UINT32(0u, ip);
  TEST_ASSERT_TRUE(parseIPv4("255.255.255.255", ip));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, ip);
}

void test_parse_rejects_invalid() {
  uint32_t ip = 123;
  TEST_ASSERT_FALSE(parseIPv4(nullptr, ip));
  TEST_ASSERT_FALSE(parseIPv4("", ip));
  TEST_ASSERT_FALSE(parseIPv4("256.1.1.1", ip));
  TEST_ASSERT_FALSE(parseIPv4("1.2.3", ip));
  TEST_ASSERT_FALSE(parseIPv4("1.2.3.4.5", ip));
  TEST_ASSERT_FALSE(parseIPv4("a.b.c.d", ip));
  TEST_ASSERT_FALSE(parseIPv4("1.2.3.4 ", ip));
  TEST_ASSERT_FALSE(parseIPv4("1..2.3", ip));
  TEST_ASSERT_FALSE(parseIPv4("0001.1.1.1", ip));
  TEST_ASSERT_EQUAL_UINT32(123u, ip);  // hibánál nem módosul
}

void test_mask() {
  TEST_ASSERT_TRUE(isValidMask(0xFFFFFF00u));
  TEST_ASSERT_TRUE(isValidMask(0xFFFFFFFFu));
  TEST_ASSERT_TRUE(isValidMask(0x80000000u));
  TEST_ASSERT_FALSE(isValidMask(0u));
  TEST_ASSERT_FALSE(isValidMask(0xFF00FF00u));
  TEST_ASSERT_FALSE(isValidMask(0x00FFFFFFu));
}

void test_same_subnet() {
  TEST_ASSERT_TRUE(sameSubnet(0xC0A80132u, 0xC0A80101u, 0xFFFFFF00u));
  TEST_ASSERT_FALSE(sameSubnet(0x0A000014u, 0xC0A80101u, 0xFFFFFF00u));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_valid);
  RUN_TEST(test_parse_rejects_invalid);
  RUN_TEST(test_mask);
  RUN_TEST(test_same_subnet);
  return UNITY_END();
}
```

- [ ] **Step 4: Run test to verify it fails**

Run: `pio test -e native -f test_ipv4`
Expected: fordítási hiba, `model/Ipv4.h: No such file or directory`.

- [ ] **Step 5: Write minimal implementation** – `src/model/Ipv4.h`

```cpp
#pragma once
#include <stdint.h>

// "a.b.c.d" -> host byte order szám. Hibánál false, és `out` nem változik.
inline bool parseIPv4(const char* s, uint32_t& out) {
  if (!s) return false;
  uint32_t result = 0;
  int octets = 0;
  const char* p = s;
  while (true) {
    if (*p < '0' || *p > '9') return false;
    uint32_t v = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
      v = v * 10 + (uint32_t)(*p - '0');
      if (++digits > 3 || v > 255) return false;
      p++;
    }
    result = (result << 8) | v;
    if (++octets == 4) break;
    if (*p != '.') return false;
    p++;
  }
  if (*p != '\0') return false;
  out = result;
  return true;
}

// Összefüggő 1-esek balról, legalább egy.
inline bool isValidMask(uint32_t mask) {
  if (mask == 0) return false;
  uint32_t inv = ~mask;
  return (inv & (inv + 1)) == 0;
}

inline bool sameSubnet(uint32_t a, uint32_t b, uint32_t mask) {
  return (a & mask) == (b & mask);
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `pio test -e native -f test_ipv4`
Expected: 4 Tests 0 Failures, PASSED.

- [ ] **Step 7: Commit**

```bash
git add platformio.ini include/config_defaults.h src/model/Ipv4.h test/test_ipv4/test_main.cpp
git commit -m "feat: project setup and IPv4 helpers"
```

---

### Task 2: Config modell és JSON betöltés

**Files:**
- Create: `src/model/Config.h`
- Create: `src/model/Config.cpp`
- Create: `data/config.json`
- Test: `test/test_config/test_main.cpp`

**Interfaces:**
- Consumes: `parseIPv4`, `isValidMask`, `sameSubnet` (Task 1), `CFG_*` makrók
- Produces:
  - `struct Config` (mezők lent)
  - `Config defaultConfig()`
  - `std::vector<std::string> applyJson(Config& cfg, const char* json)`, ami figyelmeztetéseket ad vissza, és csak az érvényes mezőket írja át

- [ ] **Step 1: Write the failing test** – `test/test_config/test_main.cpp`

```cpp
#include <unity.h>
#include "model/Config.h"

void setUp() {}
void tearDown() {}

void test_defaults() {
  Config c = defaultConfig();
  TEST_ASSERT_EQUAL_UINT32(0xC0A80132u, c.ip);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFF00u, c.mask);
  TEST_ASSERT_EQUAL_UINT32(0xC0A80101u, c.gateway);
  TEST_ASSERT_EQUAL_UINT16(8766, c.port);
  TEST_ASSERT_EQUAL_STRING("/api/mixer/stream", c.path.c_str());
  TEST_ASSERT_EQUAL_UINT32(500, c.bufferMs);
  TEST_ASSERT_EQUAL_UINT32(150, c.prebufferMs);
  TEST_ASSERT_EQUAL_INT(15, c.bck);
}

void test_json_overrides_all() {
  Config c = defaultConfig();
  auto w = applyJson(c, R"({
    "wifi":{"ssid":"Home","password":"secret123"},
    "net":{"ip":"10.0.0.20","mask":"255.255.255.0","gateway":"10.0.0.1","dns":"8.8.8.8"},
    "endpoint":{"host":"10.0.0.5","port":9000,"path":"/ws","token":"abc"},
    "i2s":{"bck":1,"lrck":2,"din":3},
    "buffer":{"ms":800,"prebuffer_ms":200}
  })");
  TEST_ASSERT_TRUE(w.empty());
  TEST_ASSERT_EQUAL_STRING("Home", c.wifiSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("secret123", c.wifiPassword.c_str());
  TEST_ASSERT_EQUAL_UINT32(0x0A000014u, c.ip);
  TEST_ASSERT_EQUAL_UINT32(0x0A000001u, c.gateway);
  TEST_ASSERT_EQUAL_UINT32(0x08080808u, c.dns);
  TEST_ASSERT_EQUAL_STRING("10.0.0.5", c.host.c_str());
  TEST_ASSERT_EQUAL_UINT16(9000, c.port);
  TEST_ASSERT_EQUAL_STRING("/ws", c.path.c_str());
  TEST_ASSERT_EQUAL_STRING("abc", c.token.c_str());
  TEST_ASSERT_EQUAL_INT(1, c.bck);
  TEST_ASSERT_EQUAL_INT(3, c.din);
  TEST_ASSERT_EQUAL_UINT32(800, c.bufferMs);
  TEST_ASSERT_EQUAL_UINT32(200, c.prebufferMs);
}

void test_partial_json_keeps_other_defaults() {
  Config c = defaultConfig();
  auto w = applyJson(c, R"({"buffer":{"ms":300},"unknown":{"x":1}})");
  TEST_ASSERT_TRUE(w.empty());
  TEST_ASSERT_EQUAL_UINT32(300, c.bufferMs);
  TEST_ASSERT_EQUAL_UINT16(8766, c.port);
}

void test_malformed_json_keeps_defaults() {
  Config c = defaultConfig();
  auto w = applyJson(c, "{not json");
  TEST_ASSERT_EQUAL_size_t(1, w.size());
  TEST_ASSERT_EQUAL_UINT16(8766, c.port);
}

void test_empty_and_null_json() {
  Config c = defaultConfig();
  TEST_ASSERT_FALSE(applyJson(c, "").empty());
  TEST_ASSERT_FALSE(applyJson(c, nullptr).empty());
  TEST_ASSERT_FALSE(applyJson(c, "[1,2]").empty());
  TEST_ASSERT_EQUAL_UINT16(8766, c.port);
}

void test_bad_ip_octet_rejected() {
  Config c = defaultConfig();
  auto w = applyJson(c, R"({"net":{"ip":"256.1.1.1"}})");
  TEST_ASSERT_FALSE(w.empty());
  TEST_ASSERT_EQUAL_UINT32(0xC0A80132u, c.ip);
}

void test_wrong_type_rejected() {
  Config c = defaultConfig();
  auto w = applyJson(c, R"({"net":{"ip":5},"endpoint":{"port":"80"}})");
  TEST_ASSERT_EQUAL_size_t(2, w.size());
  TEST_ASSERT_EQUAL_UINT32(0xC0A80132u, c.ip);
  TEST_ASSERT_EQUAL_UINT16(8766, c.port);
}

void test_gateway_outside_subnet_rejects_net_group() {
  Config c = defaultConfig();
  // az alapértelmezett gateway (192.168.1.1) nincs a 10.0.0.0/24-ben
  auto w = applyJson(c, R"({"net":{"ip":"10.0.0.20"}})");
  TEST_ASSERT_FALSE(w.empty());
  TEST_ASSERT_EQUAL_UINT32(0xC0A80132u, c.ip);
}

void test_invalid_mask_rejects_net_group() {
  Config c = defaultConfig();
  auto w = applyJson(c, R"({"net":{"mask":"255.0.255.0"}})");
  TEST_ASSERT_FALSE(w.empty());
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFF00u, c.mask);
}

void test_token_with_crlf_rejected() {
  Config c = defaultConfig();
  auto w = applyJson(c, "{\"endpoint\":{\"token\":\"abc\\r\\nX-Evil: 1\"}}");
  TEST_ASSERT_FALSE(w.empty());
  TEST_ASSERT_EQUAL_STRING("", c.token.c_str());
}

void test_token_with_space_rejected() {
  Config c = defaultConfig();
  TEST_ASSERT_FALSE(applyJson(c, R"({"endpoint":{"token":"a b"}})").empty());
  TEST_ASSERT_EQUAL_STRING("", c.token.c_str());
}

void test_port_range() {
  Config c = defaultConfig();
  TEST_ASSERT_FALSE(applyJson(c, R"({"endpoint":{"port":0}})").empty());
  TEST_ASSERT_FALSE(applyJson(c, R"({"endpoint":{"port":70000}})").empty());
  TEST_ASSERT_EQUAL_UINT16(8766, c.port);
}

void test_path_must_start_with_slash() {
  Config c = defaultConfig();
  TEST_ASSERT_FALSE(applyJson(c, R"({"endpoint":{"path":"api"}})").empty());
  TEST_ASSERT_EQUAL_STRING("/api/mixer/stream", c.path.c_str());
}

void test_host_with_whitespace_rejected() {
  Config c = defaultConfig();
  TEST_ASSERT_FALSE(applyJson(c, R"({"endpoint":{"host":"a b"}})").empty());
  TEST_ASSERT_FALSE(applyJson(c, R"({"endpoint":{"host":""}})").empty());
  TEST_ASSERT_EQUAL_STRING("192.168.1.10", c.host.c_str());
}

void test_ssid_length() {
  Config c = defaultConfig();
  TEST_ASSERT_FALSE(applyJson(c, R"({"wifi":{"ssid":""}})").empty());
  TEST_ASSERT_EQUAL_STRING("changeme", c.wifiSsid.c_str());
}

void test_i2s_pins_must_be_distinct_and_in_range() {
  Config c = defaultConfig();
  TEST_ASSERT_FALSE(applyJson(c, R"({"i2s":{"bck":4,"lrck":4,"din":5}})").empty());
  TEST_ASSERT_FALSE(applyJson(c, R"({"i2s":{"bck":99}})").empty());
  TEST_ASSERT_EQUAL_INT(15, c.bck);
  TEST_ASSERT_EQUAL_INT(16, c.lrck);
}

void test_buffer_range() {
  Config c = defaultConfig();
  TEST_ASSERT_FALSE(applyJson(c, R"({"buffer":{"ms":10}})").empty());
  TEST_ASSERT_FALSE(applyJson(c, R"({"buffer":{"ms":999999}})").empty());
  TEST_ASSERT_EQUAL_UINT32(500, c.bufferMs);
}

void test_prebuffer_must_not_exceed_buffer() {
  Config c = defaultConfig();
  // az előpuffer (alap 150) nagyobb lenne a pufferenél
  TEST_ASSERT_FALSE(applyJson(c, R"({"buffer":{"ms":100}})").empty());
  TEST_ASSERT_FALSE(applyJson(c, R"({"buffer":{"prebuffer_ms":900}})").empty());
  TEST_ASSERT_FALSE(applyJson(c, R"({"buffer":{"prebuffer_ms":5}})").empty());
  TEST_ASSERT_EQUAL_UINT32(500, c.bufferMs);
  TEST_ASSERT_EQUAL_UINT32(150, c.prebufferMs);
}

void test_token_value_never_in_warnings() {
  Config c = defaultConfig();
  auto w = applyJson(c, "{\"endpoint\":{\"token\":\"SECRET\\r\\nTOKEN\"}}");
  TEST_ASSERT_FALSE(w.empty());
  for (const auto& m : w) TEST_ASSERT_TRUE(m.find("SECRET") == std::string::npos);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_json_overrides_all);
  RUN_TEST(test_partial_json_keeps_other_defaults);
  RUN_TEST(test_malformed_json_keeps_defaults);
  RUN_TEST(test_empty_and_null_json);
  RUN_TEST(test_bad_ip_octet_rejected);
  RUN_TEST(test_wrong_type_rejected);
  RUN_TEST(test_gateway_outside_subnet_rejects_net_group);
  RUN_TEST(test_invalid_mask_rejects_net_group);
  RUN_TEST(test_token_with_crlf_rejected);
  RUN_TEST(test_token_with_space_rejected);
  RUN_TEST(test_port_range);
  RUN_TEST(test_path_must_start_with_slash);
  RUN_TEST(test_host_with_whitespace_rejected);
  RUN_TEST(test_ssid_length);
  RUN_TEST(test_i2s_pins_must_be_distinct_and_in_range);
  RUN_TEST(test_buffer_range);
  RUN_TEST(test_prebuffer_must_not_exceed_buffer);
  RUN_TEST(test_token_value_never_in_warnings);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_config`
Expected: fordítási hiba, `model/Config.h: No such file or directory`.

- [ ] **Step 3: `src/model/Config.h`**

```cpp
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

struct Config {
  std::string wifiSsid;
  std::string wifiPassword;

  uint32_t ip = 0;
  uint32_t mask = 0;
  uint32_t gateway = 0;
  uint32_t dns = 0;

  std::string host;
  uint16_t port = 0;
  std::string path;
  std::string token;

  int bck = 0;
  int lrck = 0;
  int din = 0;

  uint32_t bufferMs = 0;
  uint32_t prebufferMs = 0;
};

// A config_defaults.h alapértékeiből épített konfiguráció.
Config defaultConfig();

// A JSON érvényes mezőit ráírja `cfg`-re. Az érvénytelen mezőket kihagyja,
// és mindegyikről figyelmeztetést ad vissza. A `net`, az `i2s` és a `buffer`
// csoport egyben érvényes vagy egyben elutasított. A token értékét nem írja a figyelmeztetésekbe.
std::vector<std::string> applyJson(Config& cfg, const char* json);
```

- [ ] **Step 4: `src/model/Config.cpp`**

```cpp
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
```

- [ ] **Step 5: Run test to verify it passes**

Run: `pio test -e native -f test_config`
Expected: 19 Tests 0 Failures, PASSED.

- [ ] **Step 6: `data/config.json` példa létrehozása**

```json
{
  "wifi": { "ssid": "IDE_A_WIFI_NEVE", "password": "IDE_A_JELSZO" },
  "net": {
    "ip": "192.168.1.50",
    "mask": "255.255.255.0",
    "gateway": "192.168.1.1",
    "dns": "192.168.1.1"
  },
  "endpoint": {
    "host": "192.168.1.10",
    "port": 8766,
    "path": "/api/mixer/stream",
    "token": "IDE_A_TOKEN_AZ_ADMIN_FELULETROL"
  },
  "i2s": { "bck": 15, "lrck": 16, "din": 17 },
  "buffer": { "ms": 500, "prebuffer_ms": 150 }
}
```

A `.gitignore`-hoz érdemes hozzáadni a `data/config.json`-t, mert WiFi jelszó és token kerül bele.

- [ ] **Step 7: Commit**

```bash
git add src/model/Config.h src/model/Config.cpp test/test_config/test_main.cpp
git commit -m "feat: config model with JSON override and validation"
```

---

### Task 3: Firewall

**Files:**
- Create: `src/services/Firewall.h`
- Create: `src/services/Firewall.cpp`
- Test: `test/test_firewall/test_main.cpp`

**Interfaces:**
- Produces:
  - `class Firewall` ctor `Firewall()` (mindent tilt) és `Firewall(uint32_t allowedIp, uint16_t allowedPort)`
  - `bool allowConnect(uint32_t ip, uint16_t port) const`
  - `bool verifyRemote(uint32_t remoteIp) const`
  - `bool listenAllowed() const`, mindig `false`

- [ ] **Step 1: Write the failing test** – `test/test_firewall/test_main.cpp`

```cpp
#include <unity.h>
#include "services/Firewall.h"

void setUp() {}
void tearDown() {}

void test_allows_only_configured_endpoint() {
  Firewall fw(0xC0A8010Au, 8000);
  TEST_ASSERT_TRUE(fw.allowConnect(0xC0A8010Au, 8000));
  TEST_ASSERT_FALSE(fw.allowConnect(0xC0A8010Bu, 8000));
  TEST_ASSERT_FALSE(fw.allowConnect(0xC0A8010Au, 8001));
}

void test_verify_remote() {
  Firewall fw(0xC0A8010Au, 8000);
  TEST_ASSERT_TRUE(fw.verifyRemote(0xC0A8010Au));
  TEST_ASSERT_FALSE(fw.verifyRemote(0x08080808u));
}

void test_default_denies_everything() {
  Firewall fw;
  TEST_ASSERT_FALSE(fw.allowConnect(0, 0));
  TEST_ASSERT_FALSE(fw.allowConnect(0xC0A8010Au, 8000));
  TEST_ASSERT_FALSE(fw.verifyRemote(0));
}

void test_unresolved_ip_zero_denies_everything() {
  Firewall fw(0, 8000);  // pl. sikertelen DNS feloldás
  TEST_ASSERT_FALSE(fw.allowConnect(0, 8000));
  TEST_ASSERT_FALSE(fw.verifyRemote(0));
}

void test_never_listens() {
  Firewall fw(0xC0A8010Au, 8000);
  TEST_ASSERT_FALSE(fw.listenAllowed());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_allows_only_configured_endpoint);
  RUN_TEST(test_verify_remote);
  RUN_TEST(test_default_denies_everything);
  RUN_TEST(test_unresolved_ip_zero_denies_everything);
  RUN_TEST(test_never_listens);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_firewall`
Expected: fordítási hiba, `services/Firewall.h: No such file or directory`.

- [ ] **Step 3: `src/services/Firewall.h`**

```cpp
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
```

- [ ] **Step 4: `src/services/Firewall.cpp`**

```cpp
#include "Firewall.h"

bool Firewall::allowConnect(uint32_t ip, uint16_t port) const {
  return ip_ != 0 && ip == ip_ && port == port_;
}

bool Firewall::verifyRemote(uint32_t remoteIp) const {
  return ip_ != 0 && remoteIp == ip_;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `pio test -e native -f test_firewall`
Expected: 5 Tests 0 Failures, PASSED.

- [ ] **Step 6: Commit**

```bash
git add src/services/Firewall.h src/services/Firewall.cpp test/test_firewall/test_main.cpp
git commit -m "feat: endpoint allowlist firewall"
```

---

### Task 4: Csomag-kicsomagolás, szekvenciakövetés, STREAM_INFO

**Files:**
- Create: `src/services/PacketParser.h`
- Create: `src/services/PacketParser.cpp`
- Test: `test/test_packet/test_main.cpp`

**Interfaces:**
- Produces:
  - `constexpr size_t kPacketHeaderBytes = 15`
  - `struct PacketView { bool ok; uint32_t seq; uint32_t timestampMs; uint8_t status; const uint8_t* payload; size_t len; }`
  - `PacketView parsePacket(const uint8_t* data, size_t len)`: ellenőrzi a `CSGP` magicet és azt, hogy `len - 15 == length`
  - `class SeqTracker { SeqTracker(); uint32_t onPacket(uint32_t seq); void reset(); uint32_t lateCount() const; }`, ahol az `onPacket` a kihagyott csomagok számát adja
  - `enum PacketStatus : uint8_t { kStatusMusic = 1, kStatusMic = 2, kStatusBell = 4 }`
  - `struct StreamInfo { std::string codec; uint32_t sampleRate; uint8_t channels; uint32_t bitrateKbps; uint32_t frameSamples; }`
  - `bool parseStreamInfo(const char* json, StreamInfo& out)`

- [ ] **Step 1: Write the failing test** – `test/test_packet/test_main.cpp`

```cpp
#include <unity.h>
#include <string.h>
#include <vector>
#include "services/PacketParser.h"

void setUp() {}
void tearDown() {}

// Csomag összeállítása a szerver formátuma szerint (big-endian).
static std::vector<uint8_t> makePacket(uint32_t seq, uint32_t ts, uint8_t status,
                                       const std::vector<uint8_t>& payload,
                                       const char* magic = "CSGP") {
  std::vector<uint8_t> p;
  for (int i = 0; i < 4; i++) p.push_back((uint8_t)magic[i]);
  for (int s = 24; s >= 0; s -= 8) p.push_back((uint8_t)(seq >> s));
  for (int s = 24; s >= 0; s -= 8) p.push_back((uint8_t)(ts >> s));
  p.push_back(status);
  p.push_back((uint8_t)(payload.size() >> 8));
  p.push_back((uint8_t)payload.size());
  p.insert(p.end(), payload.begin(), payload.end());
  return p;
}

void test_parse_valid_packet() {
  auto p = makePacket(0x102, 0x1000, 3, {0xAA, 0xBB});
  PacketView v = parsePacket(p.data(), p.size());
  TEST_ASSERT_TRUE(v.ok);
  TEST_ASSERT_EQUAL_UINT32(0x102u, v.seq);
  TEST_ASSERT_EQUAL_UINT32(0x1000u, v.timestampMs);
  TEST_ASSERT_EQUAL_UINT8(3, v.status);
  TEST_ASSERT_EQUAL_size_t(2, v.len);
  TEST_ASSERT_EQUAL_UINT8(0xAA, v.payload[0]);
  TEST_ASSERT_EQUAL_UINT8(0xBB, v.payload[1]);
}

void test_parse_empty_payload() {
  auto p = makePacket(7, 0, 0, {});
  PacketView v = parsePacket(p.data(), p.size());
  TEST_ASSERT_TRUE(v.ok);
  TEST_ASSERT_EQUAL_size_t(0, v.len);
  TEST_ASSERT_EQUAL_size_t(kPacketHeaderBytes, p.size());
}

void test_wrong_magic_rejected() {
  auto p = makePacket(1, 0, 0, {1, 2}, "XSGP");
  TEST_ASSERT_FALSE(parsePacket(p.data(), p.size()).ok);
  auto q = makePacket(1, 0, 0, {1, 2}, "csgp");
  TEST_ASSERT_FALSE(parsePacket(q.data(), q.size()).ok);
}

void test_shorter_than_header_rejected() {
  auto p = makePacket(1, 0, 0, {});
  TEST_ASSERT_FALSE(parsePacket(p.data(), kPacketHeaderBytes - 1).ok);
  TEST_ASSERT_FALSE(parsePacket(p.data(), 0).ok);
  TEST_ASSERT_FALSE(parsePacket(nullptr, 20).ok);
}

void test_length_mismatch_rejected() {
  auto p = makePacket(1, 0, 0, {1, 2, 3});
  TEST_ASSERT_FALSE(parsePacket(p.data(), p.size() - 1).ok);  // kevesebb adat a jelzettnél
  p.push_back(9);
  TEST_ASSERT_FALSE(parsePacket(p.data(), p.size()).ok);  // több adat a jelzettnél
}

void test_status_bits() {
  TEST_ASSERT_EQUAL_UINT8(1, kStatusMusic);
  TEST_ASSERT_EQUAL_UINT8(2, kStatusMic);
  TEST_ASSERT_EQUAL_UINT8(4, kStatusBell);
}

void test_seq_in_order_and_first_packet() {
  SeqTracker t;
  TEST_ASSERT_EQUAL_UINT32(0, t.onPacket(100));  // első csomag sosem kihagyás
  TEST_ASSERT_EQUAL_UINT32(0, t.onPacket(101));
  TEST_ASSERT_EQUAL_UINT32(0, t.onPacket(102));
}

void test_seq_gap_counted() {
  SeqTracker t;
  t.onPacket(10);
  TEST_ASSERT_EQUAL_UINT32(3, t.onPacket(14));  // 11, 12, 13 hiányzik
  TEST_ASSERT_EQUAL_UINT32(0, t.onPacket(15));
}

void test_seq_wraparound_is_not_a_gap() {
  SeqTracker t;
  t.onPacket(0xFFFFFFFEu);
  TEST_ASSERT_EQUAL_UINT32(0, t.onPacket(0xFFFFFFFFu));
  TEST_ASSERT_EQUAL_UINT32(0, t.onPacket(0));
  TEST_ASSERT_EQUAL_UINT32(0, t.onPacket(1));
}

void test_seq_gap_across_wraparound() {
  SeqTracker t;
  t.onPacket(0xFFFFFFFEu);
  TEST_ASSERT_EQUAL_UINT32(2, t.onPacket(1));  // 0xFFFFFFFF és 0 hiányzik
}

void test_seq_duplicate_and_backwards_are_not_gaps() {
  SeqTracker t;
  t.onPacket(50);
  t.onPacket(51);
  TEST_ASSERT_EQUAL_UINT32(0, t.onPacket(51));  // duplikátum
  TEST_ASSERT_EQUAL_UINT32(0, t.onPacket(40));  // visszalépés
  TEST_ASSERT_EQUAL_UINT32(2, t.lateCount());
  TEST_ASSERT_EQUAL_UINT32(0, t.onPacket(52));  // a folyamat onnan megy tovább, ahol volt
}

void test_seq_reset() {
  SeqTracker t;
  t.onPacket(5);
  t.reset();
  TEST_ASSERT_EQUAL_UINT32(0, t.onPacket(900));
}

void test_stream_info_valid() {
  StreamInfo si;
  TEST_ASSERT_TRUE(parseStreamInfo(
      R"({"codec":"mp3","sample_rate":44100,"channels":1,"bitrate_kbps":128,"frame_samples":1152})", si));
  TEST_ASSERT_EQUAL_STRING("mp3", si.codec.c_str());
  TEST_ASSERT_EQUAL_UINT32(44100, si.sampleRate);
  TEST_ASSERT_EQUAL_UINT8(1, si.channels);
  TEST_ASSERT_EQUAL_UINT32(128, si.bitrateKbps);
  TEST_ASSERT_EQUAL_UINT32(1152, si.frameSamples);
}

void test_stream_info_optional_fields_default() {
  StreamInfo si;
  TEST_ASSERT_TRUE(parseStreamInfo(R"({"codec":"MP3","sample_rate":48000,"channels":2})", si));
  TEST_ASSERT_EQUAL_UINT32(128, si.bitrateKbps);
  TEST_ASSERT_EQUAL_UINT32(1152, si.frameSamples);
}

void test_stream_info_rejects_bad_input() {
  StreamInfo si;
  TEST_ASSERT_FALSE(parseStreamInfo(nullptr, si));
  TEST_ASSERT_FALSE(parseStreamInfo("", si));
  TEST_ASSERT_FALSE(parseStreamInfo("{bad", si));
  TEST_ASSERT_FALSE(parseStreamInfo(R"({"codec":"opus","sample_rate":48000,"channels":2})", si));
  TEST_ASSERT_FALSE(parseStreamInfo(R"({"codec":"mp3","sample_rate":12345,"channels":2})", si));
  TEST_ASSERT_FALSE(parseStreamInfo(R"({"codec":"mp3","sample_rate":44100,"channels":6})", si));
  TEST_ASSERT_FALSE(parseStreamInfo(R"({"codec":"mp3","channels":1})", si));
  TEST_ASSERT_FALSE(parseStreamInfo(R"({"codec":"mp3","sample_rate":"44100","channels":1})", si));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_valid_packet);
  RUN_TEST(test_parse_empty_payload);
  RUN_TEST(test_wrong_magic_rejected);
  RUN_TEST(test_shorter_than_header_rejected);
  RUN_TEST(test_length_mismatch_rejected);
  RUN_TEST(test_status_bits);
  RUN_TEST(test_seq_in_order_and_first_packet);
  RUN_TEST(test_seq_gap_counted);
  RUN_TEST(test_seq_wraparound_is_not_a_gap);
  RUN_TEST(test_seq_gap_across_wraparound);
  RUN_TEST(test_seq_duplicate_and_backwards_are_not_gaps);
  RUN_TEST(test_seq_reset);
  RUN_TEST(test_stream_info_valid);
  RUN_TEST(test_stream_info_optional_fields_default);
  RUN_TEST(test_stream_info_rejects_bad_input);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_packet`
Expected: fordítási hiba, `services/PacketParser.h: No such file or directory`.

- [ ] **Step 3: `src/services/PacketParser.h`**

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

// Csomag: 15 bájtos big-endian fejléc + MP3 payload.
//   0  4  magic "CSGP"
//   4  4  seq (u32)
//   8  4  timestamp_ms (u32)
//  12  1  status (bitmező)
//  13  2  length (u16): a payload hossza
//  15  .  payload
constexpr size_t kPacketHeaderBytes = 15;

enum PacketStatus : uint8_t { kStatusMusic = 1, kStatusMic = 2, kStatusBell = 4 };

struct PacketView {
  bool ok = false;
  uint32_t seq = 0;
  uint32_t timestampMs = 0;
  uint8_t status = 0;
  const uint8_t* payload = nullptr;
  size_t len = 0;
};

// ok=false, ha a csomag a fejlécnél rövidebb, a magic nem "CSGP", vagy a
// payload tényleges hossza nem egyezik a `length` mezővel.
PacketView parsePacket(const uint8_t* data, size_t len);

// Kihagyott csomagok számlálója. A 32 bites szekvenciaszám körbefordulhat.
class SeqTracker {
 public:
  // Az előre kihagyott csomagok számát adja. Az első csomag, a duplikátum és a
  // visszalépés 0-t ad (utóbbi kettő a lateCount()-ot növeli).
  uint32_t onPacket(uint32_t seq);
  void reset();
  uint32_t lateCount() const { return late_; }

 private:
  uint32_t last_ = 0;
  bool hasLast_ = false;
  uint32_t late_ = 0;
};

struct StreamInfo {
  std::string codec;
  uint32_t sampleRate = 0;
  uint8_t channels = 0;
  uint32_t bitrateKbps = 128;
  uint32_t frameSamples = 1152;
};

// A STREAM_INFO JSON szöveges üzenet. Csak "mp3" codec és ismert mintavétel fogadható el.
bool parseStreamInfo(const char* json, StreamInfo& out);
```

- [ ] **Step 4: `src/services/PacketParser.cpp`**

```cpp
#include "PacketParser.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstring>

namespace {
uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
}  // namespace

PacketView parsePacket(const uint8_t* data, size_t len) {
  PacketView v;
  if (!data || len < kPacketHeaderBytes) return v;
  if (memcmp(data, "CSGP", 4) != 0) return v;

  size_t declared = ((size_t)data[13] << 8) | data[14];
  if (len - kPacketHeaderBytes != declared) return v;

  v.ok = true;
  v.seq = be32(data + 4);
  v.timestampMs = be32(data + 8);
  v.status = data[12];
  v.payload = data + kPacketHeaderBytes;
  v.len = declared;
  return v;
}

void SeqTracker::reset() {
  hasLast_ = false;
  last_ = 0;
  late_ = 0;
}

uint32_t SeqTracker::onPacket(uint32_t seq) {
  if (!hasLast_) {
    hasLast_ = true;
    last_ = seq;
    return 0;
  }
  uint32_t expected = last_ + 1u;  // u32: magától körbefordul
  uint32_t delta = seq - expected;
  if (delta < 0x80000000u) {  // előre ugrás (0 = sorrendben)
    last_ = seq;
    return delta;
  }
  late_++;  // duplikátum vagy visszalépés
  return 0;
}

bool parseStreamInfo(const char* json, StreamInfo& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json ? json : "")) return false;
  const JsonDocument& d = doc;

  if (!d["codec"].is<const char*>()) return false;
  std::string codec = d["codec"].as<const char*>();
  for (auto& c : codec) c = (char)tolower((unsigned char)c);
  if (codec != "mp3") return false;

  if (!d["sample_rate"].is<uint32_t>() || !d["channels"].is<int>()) return false;
  uint32_t rate = d["sample_rate"].as<uint32_t>();
  static const uint32_t kRates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
  bool known = false;
  for (uint32_t r : kRates) known = known || (r == rate);
  if (!known) return false;

  int channels = d["channels"].as<int>();
  if (channels != 1 && channels != 2) return false;

  StreamInfo si;
  si.codec = codec;
  si.sampleRate = rate;
  si.channels = (uint8_t)channels;
  if (d["bitrate_kbps"].is<uint32_t>()) {
    uint32_t b = d["bitrate_kbps"].as<uint32_t>();
    if (b >= 8 && b <= 320) si.bitrateKbps = b;
  }
  if (d["frame_samples"].is<uint32_t>()) {
    uint32_t fs = d["frame_samples"].as<uint32_t>();
    if (fs > 0 && fs <= 4608) si.frameSamples = fs;
  }
  out = si;
  return true;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `pio test -e native -f test_packet`
Expected: 15 Tests 0 Failures, PASSED.

- [ ] **Step 6: Commit**

```bash
git add src/services/PacketParser.h src/services/PacketParser.cpp test/test_packet/test_main.cpp
git commit -m "feat: CSGP packet parsing, sequence tracking, STREAM_INFO parsing"
```

---

### Task 5: Ring buffer

**Files:**
- Create: `src/services/RingBuffer.h`
- Create: `src/services/RingBuffer.cpp`
- Test: `test/test_ringbuffer/test_main.cpp`

**Interfaces:**
- Produces: `class RingBuffer` (egy író és egy olvasó szál)
  - `explicit RingBuffer(size_t capacity)` (legalább 1)
  - `size_t write(const uint8_t* data, size_t len)`: a szabad helynél többet nem ír, a leírt bájtok számát adja
  - `size_t read(uint8_t* out, size_t len)`: a rendelkezésre állónál többet nem olvas
  - `size_t available() const`, `size_t freeSpace() const`, `size_t capacity() const`

- [ ] **Step 1: Write the failing test** – `test/test_ringbuffer/test_main.cpp`

```cpp
#include <unity.h>
#include <string.h>
#include "services/RingBuffer.h"

void setUp() {}
void tearDown() {}

void test_roundtrip() {
  RingBuffer rb(8);
  const uint8_t in[] = {1, 2, 3, 4};
  uint8_t out[8] = {0};
  TEST_ASSERT_EQUAL_size_t(4, rb.write(in, 4));
  TEST_ASSERT_EQUAL_size_t(4, rb.available());
  TEST_ASSERT_EQUAL_size_t(4, rb.read(out, 8));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 4);
  TEST_ASSERT_EQUAL_size_t(0, rb.available());
}

void test_read_from_empty_returns_zero() {
  RingBuffer rb(4);
  uint8_t out[4];
  TEST_ASSERT_EQUAL_size_t(0, rb.read(out, 4));
}

void test_overflow_writes_only_free_space() {
  RingBuffer rb(4);
  const uint8_t in[] = {1, 2, 3, 4, 5, 6};
  TEST_ASSERT_EQUAL_size_t(4, rb.write(in, 6));
  TEST_ASSERT_EQUAL_size_t(0, rb.write(in, 1));
  TEST_ASSERT_EQUAL_size_t(0, rb.freeSpace());
  uint8_t out[4];
  TEST_ASSERT_EQUAL_size_t(4, rb.read(out, 4));
  const uint8_t expect[] = {1, 2, 3, 4};  // a régi adat marad, az új eldobódik
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 4);
}

void test_wraparound_preserves_order() {
  RingBuffer rb(5);
  const uint8_t a[] = {1, 2, 3, 4};
  uint8_t out[8];
  rb.write(a, 4);
  rb.read(out, 3);  // olvasó a 3. indexen
  const uint8_t b[] = {5, 6, 7, 8};
  TEST_ASSERT_EQUAL_size_t(4, rb.write(b, 4));  // az író körbefordul
  TEST_ASSERT_EQUAL_size_t(5, rb.read(out, 8));
  const uint8_t expect[] = {4, 5, 6, 7, 8};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 5);
}

void test_many_cycles_stay_consistent() {
  RingBuffer rb(7);
  uint8_t counter = 0, expected = 0;
  for (int i = 0; i < 1000; i++) {
    uint8_t chunk[5];
    for (int j = 0; j < 5; j++) chunk[j] = counter + j;
    size_t w = rb.write(chunk, 5);
    counter += (uint8_t)w;
    uint8_t out[4];
    size_t r = rb.read(out, 4);
    for (size_t j = 0; j < r; j++) TEST_ASSERT_EQUAL_UINT8(expected++, out[j]);
  }
}

void test_zero_capacity_is_clamped() {
  RingBuffer rb(0);
  TEST_ASSERT_EQUAL_size_t(1, rb.capacity());
}

void test_null_and_zero_length_are_safe() {
  RingBuffer rb(4);
  TEST_ASSERT_EQUAL_size_t(0, rb.write(nullptr, 3));
  TEST_ASSERT_EQUAL_size_t(0, rb.write((const uint8_t*)"a", 0));
  uint8_t out[1];
  TEST_ASSERT_EQUAL_size_t(0, rb.read(nullptr, 3));
  TEST_ASSERT_EQUAL_size_t(0, rb.read(out, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip);
  RUN_TEST(test_read_from_empty_returns_zero);
  RUN_TEST(test_overflow_writes_only_free_space);
  RUN_TEST(test_wraparound_preserves_order);
  RUN_TEST(test_many_cycles_stay_consistent);
  RUN_TEST(test_zero_capacity_is_clamped);
  RUN_TEST(test_null_and_zero_length_are_safe);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_ringbuffer`
Expected: fordítási hiba, `services/RingBuffer.h: No such file or directory`.

- [ ] **Step 3: `src/services/RingBuffer.h`**

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <vector>

// Egy író és egy olvasó szál (SPSC) között biztonságos, zár nélküli bájt ring buffer.
// Az író csak write()-ot, az olvasó csak read()-et hívhat.
class RingBuffer {
 public:
  explicit RingBuffer(size_t capacity);

  size_t write(const uint8_t* data, size_t len);
  size_t read(uint8_t* out, size_t len);

  size_t available() const { return used_.load(std::memory_order_acquire); }
  size_t freeSpace() const { return buf_.size() - available(); }
  size_t capacity() const { return buf_.size(); }

 private:
  std::vector<uint8_t> buf_;
  size_t head_ = 0;  // csak az író módosítja
  size_t tail_ = 0;  // csak az olvasó módosítja
  std::atomic<size_t> used_{0};
};
```

- [ ] **Step 4: `src/services/RingBuffer.cpp`**

```cpp
#include "RingBuffer.h"

#include <string.h>

#include <algorithm>

RingBuffer::RingBuffer(size_t capacity) : buf_(capacity == 0 ? 1 : capacity) {}

size_t RingBuffer::write(const uint8_t* data, size_t len) {
  if (!data || len == 0) return 0;
  size_t n = std::min(len, freeSpace());
  size_t first = std::min(n, buf_.size() - head_);
  memcpy(&buf_[head_], data, first);
  if (n > first) memcpy(&buf_[0], data + first, n - first);
  head_ = (head_ + n) % buf_.size();
  used_.fetch_add(n, std::memory_order_release);
  return n;
}

size_t RingBuffer::read(uint8_t* out, size_t len) {
  if (!out || len == 0) return 0;
  size_t n = std::min(len, available());
  size_t first = std::min(n, buf_.size() - tail_);
  memcpy(out, &buf_[tail_], first);
  if (n > first) memcpy(out + first, &buf_[0], n - first);
  tail_ = (tail_ + n) % buf_.size();
  used_.fetch_sub(n, std::memory_order_release);
  return n;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `pio test -e native -f test_ringbuffer`
Expected: 7 Tests 0 Failures, PASSED.

- [ ] **Step 6: Futtasd az összes natív tesztet**

Run: `pio test -e native`
Expected: minden suite PASSED.

- [ ] **Step 7: Commit**

```bash
git add src/services/RingBuffer.h src/services/RingBuffer.cpp test/test_ringbuffer/test_main.cpp
git commit -m "feat: SPSC ring buffer"
```

---

### Task 6: WiFi statikus IP-vel (eszközfüggő)

Eszközfüggő kód, unit teszt nincs. A kész állapotot fordítás és soros napló igazolja.

**Files:**
- Create: `src/services/Network.h`
- Create: `src/services/Network.cpp`

**Interfaces:**
- Consumes: `Config` (Task 2)
- Produces:
  - `class Network`: `bool connect(const Config& cfg, uint32_t timeoutMs = 20000)`, `bool connected() const`, `void maintain(const Config& cfg)`
  - szabad függvény: `IPAddress toIPAddress(uint32_t hostOrderIp)` a `Network.h`-ban (a StreamClient használja)

- [ ] **Step 1: `src/services/Network.h`**

```cpp
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
```

- [ ] **Step 2: `src/services/Network.cpp`**

```cpp
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
```

- [ ] **Step 3: Fordítás ellenőrzése**

Run: `pio run -e rymcu-esp32-s3-devkitc-1`
Expected: `SUCCESS`. (A `cfg` paraméter a `maintain`-ben egyelőre nem használt; ez csak figyelmeztetés.)

- [ ] **Step 4: Commit**

```bash
git add src/services/Network.h src/services/Network.cpp
git commit -m "feat: WiFi with static IP"
```

---

### Task 7: I2S kimenet és MP3 dekóder (eszközfüggő)

**Files:**
- Create: `src/services/AudioOut.h`
- Create: `src/services/AudioOut.cpp`
- Create: `src/services/Decoder.h`
- Create: `src/services/Decoder.cpp`

**Interfaces:**
- Produces:
  - `class AudioOut`: `bool begin(int bck, int lrck, int din, uint32_t sampleRate)`, `void setSampleRate(uint32_t)`, `void write(const int16_t* pcm, size_t frames, uint8_t channels)` (mono → sztereó duplikálás), `void mute()`, `uint32_t sampleRate() const`
  - `class Decoder`: `using PcmSink = void (*)(const int16_t* pcm, size_t samples, uint8_t channels, uint32_t sampleRate, void* ctx)`, `void begin(PcmSink, void* ctx)`, `void feed(const uint8_t* data, size_t len)`, `void reset()`. A `samples` a csatornák összesített mintaszáma.

- [ ] **Step 1: Ellenőrizd a libhelix API-t**

Az első `pio run` letölti a könyvtárat. Nyisd meg a `.pio/libdeps/rymcu-esp32-s3-devkitc-1/arduino-libhelix/src/MP3DecoderHelix.h` fájlt, és erősítsd meg: (a) a callback típusa `void (*)(MP3FrameInfo&, short*, size_t, void*)`, (b) léteznek `setDataCallback`, `setReference`, `begin`, `write`, `end`, (c) az `MP3FrameInfo` mezői `nChans` és `samprate`. Ha bármi eltér, a `Decoder.cpp`-t ehhez kell igazítani. A terv ezekre a nevekre épül.

- [ ] **Step 2: `src/services/AudioOut.h`**

```cpp
#pragma once
#include <Arduino.h>

// PCM5102A DAC I2S-en (legacy driver/i2s.h). A DAC SCK/FMT/XSMT lábai hardveresen kötöttek.
class AudioOut {
 public:
  bool begin(int bck, int lrck, int din, uint32_t sampleRate);
  void setSampleRate(uint32_t sampleRate);
  // `frames` db minta csatornánként; 1 csatornát sztereóra duplikál.
  void write(const int16_t* pcm, size_t frames, uint8_t channels);
  void mute();
  uint32_t sampleRate() const { return sampleRate_; }

 private:
  static constexpr size_t kChunkFrames = 576;
  uint32_t sampleRate_ = 0;
  bool started_ = false;
  int16_t stereo_[kChunkFrames * 2];
};
```

- [ ] **Step 3: `src/services/AudioOut.cpp`**

```cpp
#include "AudioOut.h"

#include <driver/i2s.h>

namespace {
constexpr i2s_port_t kPort = I2S_NUM_0;
}

bool AudioOut::begin(int bck, int lrck, int din, uint32_t sampleRate) {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = sampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;

  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("[i2s] driver install failed");
    return false;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = bck;
  pins.ws_io_num = lrck;
  pins.data_out_num = din;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(kPort, &pins) != ESP_OK) {
    Serial.println("[i2s] set pin failed");
    i2s_driver_uninstall(kPort);
    return false;
  }
  sampleRate_ = sampleRate;
  started_ = true;
  Serial.printf("[i2s] started, %u Hz, bck=%d lrck=%d din=%d\n", (unsigned)sampleRate, bck, lrck, din);
  return true;
}

void AudioOut::setSampleRate(uint32_t sampleRate) {
  if (!started_ || sampleRate == sampleRate_) return;
  if (i2s_set_sample_rates(kPort, sampleRate) == ESP_OK) {
    Serial.printf("[i2s] sample rate %u -> %u Hz\n", (unsigned)sampleRate_, (unsigned)sampleRate);
    sampleRate_ = sampleRate;
  }
}

void AudioOut::write(const int16_t* pcm, size_t frames, uint8_t channels) {
  if (!started_ || !pcm || frames == 0) return;
  size_t written = 0;
  const TickType_t timeout = pdMS_TO_TICKS(1000);
  if (channels == 2) {
    i2s_write(kPort, pcm, frames * 4, &written, timeout);
    return;
  }
  while (frames > 0) {
    size_t n = frames < kChunkFrames ? frames : kChunkFrames;
    for (size_t i = 0; i < n; i++) stereo_[2 * i] = stereo_[2 * i + 1] = pcm[i];
    i2s_write(kPort, stereo_, n * 4, &written, timeout);
    pcm += n;
    frames -= n;
  }
}

void AudioOut::mute() {
  if (started_) i2s_zero_dma_buffer(kPort);
}
```

- [ ] **Step 4: `src/services/Decoder.h`**

```cpp
#pragma once
#include <Arduino.h>

#include "MP3DecoderHelix.h"

class Decoder {
 public:
  using PcmSink = void (*)(const int16_t* pcm, size_t samples, uint8_t channels, uint32_t sampleRate,
                           void* ctx);

  void begin(PcmSink sink, void* ctx);
  // MP3 bájtok betáplálása; a kész PCM a sink-be kerül.
  void feed(const uint8_t* data, size_t len);
  // Dekóder állapot törlése (pl. újracsatlakozás után).
  void reset();

 private:
  static void onPcm(MP3FrameInfo& info, short* pcm, size_t len, void* ref);
  libhelix::MP3DecoderHelix mp3_;
  PcmSink sink_ = nullptr;
  void* ctx_ = nullptr;
};
```

- [ ] **Step 5: `src/services/Decoder.cpp`**

```cpp
#include "Decoder.h"

using namespace libhelix;

void Decoder::begin(PcmSink sink, void* ctx) {
  sink_ = sink;
  ctx_ = ctx;
  mp3_.setDataCallback(onPcm);
  mp3_.setReference(this);
  mp3_.begin();
}

void Decoder::onPcm(MP3FrameInfo& info, short* pcm, size_t len, void* ref) {
  Decoder* self = static_cast<Decoder*>(ref);
  if (!self || !self->sink_ || info.nChans < 1 || info.nChans > 2) return;
  self->sink_(pcm, len, (uint8_t)info.nChans, (uint32_t)info.samprate, self->ctx_);
}

void Decoder::feed(const uint8_t* data, size_t len) {
  if (!data) return;
  size_t done = 0;
  while (done < len) {
    size_t n = mp3_.write(data + done, len - done);
    if (n == 0) {  // nem haladt: ne pörögjön végtelenül
      vTaskDelay(pdMS_TO_TICKS(1));
      break;
    }
    done += n;
  }
}

void Decoder::reset() {
  mp3_.end();
  mp3_.begin();
}
```

- [ ] **Step 6: Fordítás ellenőrzése**

Run: `pio run -e rymcu-esp32-s3-devkitc-1`
Expected: `SUCCESS`. Ha a libhelix eltér a Step 1-ben leírtaktól, igazítsd a `Decoder.*`-ot, és futtasd újra.

- [ ] **Step 7: Commit**

```bash
git add src/services/AudioOut.h src/services/AudioOut.cpp src/services/Decoder.h src/services/Decoder.cpp
git commit -m "feat: I2S output and MP3 decoder"
```

---

### Task 8: StreamClient és főprogram (eszközfüggő)

**Files:**
- Create: `src/services/StreamClient.h`
- Create: `src/services/StreamClient.cpp`
- Modify: `src/main.cpp` (teljes csere)

**Interfaces:**
- Consumes: `Config`, `Firewall`, `parsePacket`/`SeqTracker`/`parseStreamInfo`, `RingBuffer`, `Network`/`toIPAddress`, `AudioOut`, `Decoder`
- Produces: `class StreamClient`:
  - `StreamClient(const Config&, RingBuffer&)`, `bool begin()`, `void loop()`, `bool connected()`
  - `bool takeFlushRequest()`: igaz, ha szétkapcsolás, új kapcsolat vagy puffer-túlcsordulás miatt a dekódernek ürítenie kell (egyszer ad igazat, majd törlődik)
  - `uint32_t bitrateKbps() const`: a `STREAM_INFO` bitrátája (alap 128)

- [ ] **Step 1: `src/services/StreamClient.h`**

```cpp
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
```

Ha a fordítás azt mondja, hogy a `_client` vagy a `tcp` nem elérhető (a könyvtár verziója eltér), töröld a `RemoteAwareWs`-t, használj sima `WebSocketsClient`-et, és hagyd ki a távoli IP ellenőrzést az `onEvent`-ben. A védelem ekkor azt a garanciát adja, hogy a kapcsolat IP-literálra épül, amit a `Firewall::allowConnect` engedett.

- [ ] **Step 2: `src/services/StreamClient.cpp`**

```cpp
#include "StreamClient.h"

#include <WiFi.h>

#include "../model/Ipv4.h"
#include "Network.h"

namespace {
uint32_t toHostOrder(const IPAddress& ip) {
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
}
// Újracsatlakozási várakozás: 1, 2, 5, 10, majd 30 s.
constexpr uint32_t kBackoffMs[] = {1000, 2000, 5000, 10000, 30000};
}  // namespace

StreamClient::StreamClient(const Config& cfg, RingBuffer& ring) : cfg_(cfg), ring_(ring) {}

uint32_t StreamClient::nextBackoffMs() const {
  size_t idx = failures_ == 0 ? 0 : failures_ - 1;
  size_t last = sizeof(kBackoffMs) / sizeof(kBackoffMs[0]) - 1;
  return kBackoffMs[idx < last ? idx : last];
}

bool StreamClient::begin() {
  IPAddress ip;
  uint32_t literal = 0;
  if (parseIPv4(cfg_.host.c_str(), literal)) {
    ip = toIPAddress(literal);
  } else if (!WiFi.hostByName(cfg_.host.c_str(), ip)) {
    Serial.printf("[ws] cannot resolve host '%s'\n", cfg_.host.c_str());
    return false;
  }

  uint32_t ipv = toHostOrder(ip);
  firewall_ = Firewall(ipv, cfg_.port);
  if (!firewall_.allowConnect(ipv, cfg_.port)) {
    Serial.println("[fw] endpoint blocked by firewall");
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
  Serial.printf("[ws] connecting to %s:%u%s\n", ip.toString().c_str(), cfg_.port, cfg_.path.c_str());
  return true;
}

void StreamClient::loop() { ws_.loop(); }

void StreamClient::onEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      uint32_t remote = toHostOrder(ws_.remoteIp());
      if (!firewall_.verifyRemote(remote)) {
        Serial.println("[fw] remote IP does not match the allowlist, disconnecting");
        ws_.disconnect();
        return;
      }
      Serial.println("[ws] connected");
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
      Serial.printf("[ws] disconnected (attempt %u), retry in %u ms\n", (unsigned)failures_,
                    (unsigned)backoff);
      if (failures_ >= 5)
        Serial.println("[ws] repeated failures: check endpoint, static IP, network and token");
      break;
    }
    case WStype_TEXT: {
      StreamInfo si;
      if (parseStreamInfo((const char*)payload, si)) {
        bitrateKbps_ = si.bitrateKbps;
        Serial.printf("[ws] STREAM_INFO %s %u Hz %uch %u kbps\n", si.codec.c_str(),
                      (unsigned)si.sampleRate, (unsigned)si.channels, (unsigned)si.bitrateKbps);
      } else {
        Serial.println("[ws] unsupported or invalid STREAM_INFO");
      }
      break;
    }
    case WStype_BIN:
      onBinary(payload, length);
      break;
    default:
      break;
  }
}

void StreamClient::onBinary(const uint8_t* data, size_t length) {
  PacketView v = parsePacket(data, length);
  if (!v.ok) {  // hibás magic vagy hossz: a csomagot eldobjuk
    if (++malformed_ % 100 == 1) Serial.printf("[ws] malformed packets: %u\n", (unsigned)malformed_);
    return;
  }
  if (v.status != lastStatus_) {
    lastStatus_ = v.status;
    Serial.printf("[ws] mixer status=0x%02X\n", v.status);
  }
  uint32_t gap = seq_.onPacket(v.seq);
  if (gap) {
    lost_ += gap;
    Serial.printf("[ws] seq gap %u, total %u\n", (unsigned)gap, (unsigned)lost_);
  }
  if (v.len == 0) return;
  size_t written = ring_.write(v.payload, v.len);
  if (written < v.len) {  // túlcsordulás: szinkronvesztés, a puffert eldobjuk
    dropped_ += (uint32_t)(v.len - written);
    flush_ = true;
    Serial.printf("[ws] buffer overflow, flushing (dropped %u bytes total)\n", (unsigned)dropped_);
  }
}
```

- [ ] **Step 3: `src/main.cpp` teljes cseréje**

```cpp
#include <Arduino.h>
#include <LittleFS.h>

#include "model/Config.h"
#include "services/AudioOut.h"
#include "services/Decoder.h"
#include "services/Network.h"
#include "services/RingBuffer.h"
#include "services/StreamClient.h"

namespace {

constexpr uint32_t kInitialSampleRate = 44100;
constexpr size_t kMaxBytesPerMs = 40;  // 320 kbps

Config cfg;
Network net;
AudioOut audio;
Decoder decoder;
RingBuffer* ring = nullptr;
StreamClient* client = nullptr;
bool clientStarted = false;

void loadConfig() {
  cfg = defaultConfig();
  if (!LittleFS.begin(false)) {
    Serial.println("[cfg] LittleFS mount failed, using defaults");
    return;
  }
  File f = LittleFS.open("/config.json", "r");
  if (!f) {
    Serial.println("[cfg] /config.json not found, using defaults");
    return;
  }
  String body = f.readString();
  f.close();
  for (const auto& w : applyJson(cfg, body.c_str())) Serial.printf("[cfg] %s\n", w.c_str());
  Serial.println("[cfg] loaded");
}

void onPcm(const int16_t* pcm, size_t samples, uint8_t channels, uint32_t rate, void*) {
  if (rate != audio.sampleRate()) audio.setSampleRate(rate);
  audio.write(pcm, samples / channels, channels);
}

void drain(uint8_t* scratch, size_t scratchSize) {
  while (ring->read(scratch, scratchSize) > 0) {
  }
}

void decoderTask(void*) {
  uint8_t chunk[512];
  bool primed = false;
  bool muted = false;
  for (;;) {
    // Szétkapcsolás, új kapcsolat vagy túlcsordulás: lejátszás le, puffer és dekóder törlése.
    if (client->takeFlushRequest()) {
      drain(chunk, sizeof(chunk));
      decoder.reset();
      audio.mute();
      muted = true;
      primed = false;
    }
    if (!primed) {
      size_t prebuffer = (size_t)cfg.prebufferMs * client->bitrateKbps() / 8;
      if (ring->available() < prebuffer) {
        if (!muted) {
          audio.mute();
          muted = true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      primed = true;
      muted = false;
    }
    size_t n = ring->read(chunk, sizeof(chunk));
    if (n == 0) {  // kiürült: újra előpufferelünk, hogy ne ropogjon
      primed = false;
      continue;
    }
    decoder.feed(chunk, n);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("[boot] hangszoro");

  loadConfig();

  if (!audio.begin(cfg.bck, cfg.lrck, cfg.din, kInitialSampleRate)) {
    Serial.println("[boot] audio init failed");
  }
  ring = new RingBuffer((size_t)cfg.bufferMs * kMaxBytesPerMs);
  decoder.begin(onPcm, nullptr);
  client = new StreamClient(cfg, *ring);

  net.connect(cfg);

  xTaskCreatePinnedToCore(decoderTask, "decoder", 16384, nullptr, 2, nullptr, 1);
}

void loop() {
  net.maintain(cfg);
  if (net.connected()) {
    if (!clientStarted) clientStarted = client->begin();
    if (clientStarted) client->loop();
  }
  delay(1);
}
```

A token sehol nem kerül kiírásra: a naplóüzenetek csak a hostot, a portot és az útvonalat tartalmazzák.

- [ ] **Step 4: Fordítás ellenőrzése**

Run: `pio run -e rymcu-esp32-s3-devkitc-1`
Expected: `SUCCESS`. Ha a `RemoteAwareWs` nem fordul, alkalmazd a Step 1-ben leírt visszalépést.

- [ ] **Step 5: Config és firmware feltöltése, soros napló**

Szerkeszd a `data/config.json`-t: a saját WiFi, a szerver IP-je, a port (8766) és az admin felületről kimásolt token. Az `net.ip` egyezzen az admin felületen az Eszközök fülön felvett IP-vel, különben a szerver 403-mal elutasít.

Run: `pio run -t uploadfs` és `pio run -t upload`, utána `pio device monitor`
Expected a naplóban sorrendben: `[cfg] loaded`, `[i2s] started, 44100 Hz ...`, `[net] connected, ip=<a configban megadott IP>`, `[ws] connecting to <szerver>:8766/api/mixer/stream`, `[ws] connected`, `[ws] STREAM_INFO mp3 44100 Hz 1ch 128 kbps`, `[ws] mixer status=0x..`.

- [ ] **Step 6: Kézi ellenőrző lista (eszközön, mixerrel)**

- [ ] Szól a hang a DAC kimenetén, ropogás nélkül; a bemondás, a zene és a csengő is.
- [ ] A kapcsolat megszakításakor (admin felületen letiltás, vagy a szerver leállítása) `[ws] disconnected` jelenik meg, a hang elnémul, és a puffer kiürül. Visszaengedés után a stream magától folytatódik, régi hang nélkül.
- [ ] Hibás tokennel a napló ismétlődő `disconnected` sorokat mutat 1, 2, 5, 10, majd 30 s várakozással, 5 hiba után a "check endpoint, static IP, network and token" üzenettel; a szerver naplójában `denied reason=bad_token` látszik.
- [ ] Rossz statikus IP-vel a szerver `denied reason=ip_mismatch` naplót ír.
- [ ] A soros naplóban a token sehol nem szerepel.
- [ ] Hibás `config.json`-nal (pl. egy kitörölt vessző) az eszköz alapértékekkel indul, és `[cfg] config.json: invalid JSON` jelenik meg.

- [ ] **Step 7: Commit**

```bash
git add src/services/StreamClient.h src/services/StreamClient.cpp src/main.cpp data/config.json
git commit -m "feat: websocket stream client and main wiring"
```

---

## Önellenőrzés (a terv szerzőjétől)

- **Spec lefedettség:** config (Task 1–2, 8), statikus IP (6), firewall (3, 8), bearer token (8), `STREAM_INFO` és csomagok (4, 8), ring buffer (5), MP3 → I2S (7), újracsatlakozás (6, 8), natív tesztek (1–5). Az eltéréseket a Global Constraints szakasz sorolja fel.
- **Típuskövetkezetesség:** `parsePacket(data, len)` és `SeqTracker()` a Task 4-ben és a `StreamClient`-ben azonos (nincs `PacketFormat`); `Config::prebufferMs` a Task 2-ben definiált, a `main.cpp` használja; `toIPAddress` a Task 6-ban él és a Task 8 használja.
- **Protokoll:** a szerver leírásával egyezik a cím, a port (8766), a Bearer fejléc, a 15 bájtos big-endian fejléc, a magic és a hosszellenőrzés, az előpuffer, a puffer-ürítés szétkapcsoláskor és túlcsordulásnál, valamint az 1, 2, 5, 10, 30 s visszalépés.
- **Nem tesztelhető natívan:** `Network`, `AudioOut`, `Decoder`, `StreamClient`, `main`. Ezeket a fordítás és a Task 8 kézi lista fedi.
