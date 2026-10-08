# ESP32 mixer-kliens – terv

Dátum: 2026-10-08

## Cél

Az ESP32-S3 (rymcu-esp32-s3-devkitc-1, Arduino/PlatformIO) a mixer szerver `WS /api/mixer/stream` végpontjára csatlakozik, fogadja a hangot, MP3-ból PCM-be dekódolja, és I2S-en kiadja egy PCM5102A DAC-ra. Minden hálózati és végpont-beállítás konfigurálható, és az eszköz csak a megadott endpointtal kommunikál.

## Hatókör

- Benne van: config betöltés, statikus IP-s WiFi, allowlist-alapú "tűzfal", WebSocket-kliens bearer hitelesítéssel, csomag-kicsomagolás, MP3 dekódolás, I2S kimenet, újracsatlakozás.
- Nincs benne: a szerver oldali kód (nincs ebben a repóban), a `GET /api/mixer/stream.mp3` endpoint, OTA, webes konfigurációs felület, bármilyen bejövő szolgáltatás.

## Döntések

| Téma | Döntés |
|---|---|
| Hang kimenet | PCM5102A DAC, I2S |
| Config tárolás | `include/config.h` alapértékek + `data/config.json` (LittleFS) felülírja |
| Tűzfal | csak kliens; kimenő allowlist az endpoint IP/port párra; a kapcsolat után remote IP ellenőrzés; nincs szerverport |
| Hitelesítés | `Authorization: Bearer <token>` header a WebSocket handshake-ben |
| Megközelítés | saját csővezeték: WS → ring buffer → `libhelix-mp3` → `i2s_std` |

## Struktúra

- `include/config.h`: alapértékek.
- `data/config.json`: felülírja az alapértékeket.
- `src/model/Config`: struktúra, JSON betöltés, validálás (IP, maszk, gateway formátum, tartományok).
- `src/services/Network`: WiFi statikus IP-vel, újracsatlakozással.
- `src/services/Firewall`: allowlist szabályok; csak az endpointra enged csatlakozni; kapcsolat után a remote IP-t ellenőrzi, eltérés esetén megszakít.
- `src/services/StreamClient`: WebSocket kliens; `STREAM_INFO` JSON fogadása, bináris csomagok kicsomagolása, kihagyott csomagok figyelése.
- `src/services/Decoder`: MP3 → PCM (`libhelix-mp3`); ha a `STREAM_INFO` mintavétele eltér a konfigurálttól, újrainicializálja az I2S-t.
- `src/services/AudioOut`: I2S kimenet a PCM5102A-ra.
- `src/main.cpp`: csak a szolgáltatások sorrendben való indítása.

## Config mezők (`config.json`)

- `wifi`: `ssid`, `password`
- `net`: `ip`, `mask`, `gateway`, `dns`
- `endpoint`: `host`, `port`, `path`, `token`
- `i2s`: `bck`, `lrck`, `din` tűk
- `buffer`: `ms` (puffer hossza milliszekundumban), `prebuffer_ms` (ennyi hang gyűlik össze lejátszás előtt, alap 150, legfeljebb `ms`)

A token a `config.json`-ban van (LittleFS), nem a forráskódban, és soha nem kerül naplóba. A `data/config.json` nem kerülhet verziókezelésbe.

Feltöltés: `pio run -t uploadfs`. Hiányzó vagy hibás mezőnél az érték a `config.h`-ból jön, és az eszköz figyelmeztetést logol.

## Hardver (PCM5102A)

- BCK, LRCK, DIN a configban megadott tűkre.
- SCK a GND-re (a modul a belső PLL-t használja).
- FMT a GND-re (I2S formátum), XSMT a 3,3 V-ra (nincs némítás), a modul egyéb lábai a kártya jelölései szerint.

## Adatfolyam

WS csomag → ring buffer (PSRAM, ha van) → dekóder task → I2S DMA. A hálózati és a dekóder task külön FreeRTOS task, így rövid hálózati kiesés a puffer tartásáig nem akasztja meg a lejátszást.

## Hibakezelés

- Kapcsolatvesztés: exponenciális újrapróbálkozás.
- Elutasított kézfogás (HTTP 403): a kliens-könyvtár nem adja ki a státuszt, ezért ez nem különböztethető meg a hálózati hibától. Mindkettőre visszalépés (plafon 30 s) és egyértelmű naplóüzenet ismételt hibánál.
- Puffer kiürülése: a kimenet némítása, hogy ne ropogjon.
- Hibás config: alapértékekkel indul, figyelmeztet.
- Remote IP eltérés az allowlisttől: a kapcsolat megszakad.

## Tesztelés

- Natív unit tesztek (`pio test -e native`): config-parszer és validálás, csomag-kicsomagoló, firewall szabályok.
- Eszközön: WiFi, WebSocket és I2S manuális próbája.

## Protokoll (a szerver leírása szerint)

- Cím: `ws://<szerver-ip>:8766/api/mixer/stream`, sima WebSocket (nincs TLS), fejléc: `Authorization: Bearer <token>`. Más útvonal nincs, az alapértelmezett port 8766.
- Az ESP32 csak hallgat, semmit nem küld a szervernek.
- A szerver csak a regisztrált IP-ről és érvényes tokennel enged be. Elutasításkor a kézfogás HTTP 403-mal bukik (az ok nem látszik a kliensnek). Ezért az ESP32 statikus IP-jének egyeznie kell az admin felületen felvett IP-vel.
- A szerver 1008 kóddal zárhatja a kapcsolatot (letiltás, törlés, token rotáció, lejárat).
- Első üzenet: `STREAM_INFO` JSON (`codec`, `sample_rate`, `channels`, `bitrate_kbps`, `frame_samples`); jelenleg mono, 44,1 kHz, 128 kbit/s MP3.
- Utána bináris csomagok, big-endian, 15 bájtos fejléccel + MP3 payload:

| Offset | Méret | Mező |
|---|---|---|
| 0 | 4 | `magic` = ASCII `CSGP` (más magic: eldobandó) |
| 4 | 4 | `seq` (u32, körbefordul) |
| 8 | 4 | `timestamp_ms` (u32) |
| 12 | 1 | `status` bitmező: 1 zene, 2 mikrofon, 4 csengő, 0 csend |
| 13 | 2 | `length` (u16), a payload hossza |
| 15 | `length` | MP3-adat; a hossznak pontosan egyeznie kell, különben eldobandó |

- A szerver "rádióként" működik: csatlakozáskor az aktuális hangot küldi, a múltat nem.
- Szétkapcsoláskor és puffer-túlcsordulásnál: a lejátszás leáll, a puffer és a dekóder állapota törlődik, és új előpufferelés indul.
- Újracsatlakozás visszalépéssel: 1, 2, 5, 10, majd 30 s.
