# Hangszóró (ESP32-S3 mixer kliens)

## 1. Mit csinál

Az ESP32-S3 csatlakozik a mixer WebSocket streamjéhez (`ws://<szerver>:8766/api/mixer/stream`), az érkező MP3 hangot dekódolja, és I2S-en keresztül egy PCM5102A DAC-ra játssza. Az eszköz **csak hallgat**: a szervernek semmit nem küld.

Állapot: a firmware lefordul és az egységtesztek lefutnak, de **eszközön még nincs ellenőrizve**.

## 2. Hardver és bekötés

PCM5102A modul bekötése (az alapértelmezett GPIO-k a `config.json`-ból módosíthatók):

| PCM5102A | ESP32-S3 / tápellátás |
|---|---|
| BCK | GPIO 15 (`i2s.bck`) |
| LRCK (WS) | GPIO 16 (`i2s.lrck`) |
| DIN | GPIO 17 (`i2s.din`) |
| SCK | GND |
| FMT | GND (I2S formátum) |
| XSMT | 3,3 V (nincs némítás) |
| VIN | 3,3 V (vagy a modul leírása szerint) |
| GND | GND |

A tűk a `config.json`-ban átállíthatók (0..48). Ne használj olyan GPIO-t, amelyet a panel a flash-hez, a PSRAM-hoz vagy az USB-hez használ: ellenőrizd a panel (RYMCU ESP32-S3-DevKitC-1) kiosztását.

## 3. Szerveroldali előfeltételek

1. Az admin felületen, az **Eszközök** fülön vedd fel az eszközt az ESP32 IP-címével, és másold ki a token értékét (csak egyszer látszik).
2. A `config.json`-ban a statikus IP (`net.ip`) **egyezzen** a felvett IP-vel, különben a szerver HTTP 403-mal elutasít.
3. A szerver 8766-os portja legyen elérhető az eszköz hálózatából.

## 4. Konfiguráció

```
copy data\config.example.json data\config.json     (Windows)
cp data/config.example.json data/config.json       (Linux/macOS)
```

Töltsd ki a `data/config.json`-t. **Titkot tartalmaz (WiFi jelszó, token), ezért a git figyelmen kívül hagyja; ne oszd meg.**

Az érvénytelen mezőt a firmware figyelmeztetéssel (`[cfg] config.json: ...`) kihagyja, és az alapértéket használja. A `net`, az `i2s` és a `buffer` csoport egyben érvényes vagy egyben elutasított. Teljesen hibás JSON esetén minden alapérték marad.

| Mező | Jelentés | Alapérték | Érvényes érték |
|---|---|---|---|
| `device.name` | Eszköz neve (induláskor kiírja a soros napló) | `Hangszoro` | 1..64 karakter, nincs vezérlőkarakter |
| `device.hostname` | Hosztnév a hálózaton (DHCP/router) | `hangszoro` | 1..32 betű, szám vagy `-`, nem kezdődhet/végződhet `-`-val |
| `wifi.ssid` | WiFi neve | `changeme` | 1..32 bájt |
| `wifi.password` | WiFi jelszó | `changeme` | legfeljebb 63 bájt |
| `net.ip` | Statikus IP (egyezzen az admin felületen felvettel) | `192.168.1.50` | érvényes IPv4 |
| `net.mask` | Alhálózati maszk | `255.255.255.0` | érvényes maszk |
| `net.gateway` | Átjáró | `192.168.1.1` | érvényes IPv4, azonos alhálózat mint `net.ip` |
| `net.dns` | DNS | `192.168.1.1` | érvényes IPv4 |
| `endpoint.host` | Szerver IP vagy hosztnév | `192.168.1.10` | 1..253 karakter, nincs szóköz vagy vezérlőkarakter |
| `endpoint.port` | Szerver port | `8766` | 1..65535 |
| `endpoint.path` | Stream útvonal | `/api/mixer/stream` | `/`-lel kezdődik, legfeljebb 256 karakter, nincs szóköz |
| `endpoint.token` | Bearer token | (üres) | legfeljebb 512 karakter, nincs szóköz vagy vezérlőkarakter |
| `i2s.bck` | BCK GPIO | `15` | 0..48 |
| `i2s.lrck` | LRCK GPIO | `16` | 0..48 |
| `i2s.din` | DIN GPIO | `17` | 0..48, a három tű páronként különböző |
| `buffer.ms` | Pufferméret (ms) | `500` | 50..5000 |
| `buffer.prebuffer_ms` | Előpuffer lejátszás előtt (ms) | `150` | 20..5000, nem lehet nagyobb a `buffer.ms`-nél |

A szerver címe IP-literál vagy hosztnév lehet. A firmware a hosztnevet induláskor feloldja, és csak a feloldott, engedélyezett címre csatlakozik.

## 5. Fordítás és feltöltés

```
pio run -e rymcu-esp32-s3-devkitc-1 -t uploadfs   # data/config.json feltöltése (LittleFS)
pio run -e rymcu-esp32-s3-devkitc-1 -t upload     # firmware fordítása és feltöltése
pio device monitor                                # soros napló, 115200 baud
```

Az `-e rymcu-esp32-s3-devkitc-1` kötelező: a `platformio.ini`-ben a `native` tesztkörnyezet is szerepel, és nélküle a `pio run` azt is megpróbálja lefordítani, illetve feltölteni.

Rögzített (pinelt) verziók a `platformio.ini`-ben: `espressif32@7.1.3` (Arduino-ESP32 2.0.17), `ArduinoJson@7.4.3`, `WebSockets@2.7.3`, `arduino-libhelix` `v0.9.4`.

## 6. Windows: nem ASCII karaktert tartalmazó útvonal

Ha a felhasználói profil vagy a projekt útvonala nem ASCII karaktert tartalmaz (pl. `Takács Dávid`), az alapértelmezett fordítás a linkelésnél hibával áll le, mert az `ld` nem tudja megnyitni a `firmware.map` fájlt.

Megoldás (bármelyik):

- Állítsd be a `PLATFORMIO_BUILD_DIR` környezeti változót egy ASCII mappára abban a shellben, ahol a `pio`-t futtatod, például PowerShellben: `$env:PLATFORMIO_BUILD_DIR='C:/pio_build'; pio run -e rymcu-esp32-s3-devkitc-1 -t upload`.
- Vagy költöztesd a projektet ASCII útvonalra.

Gépspecifikus útvonalat ne írj a `platformio.ini`-be.

## 7. Natív tesztek

```
pio test -e native
```

Gcc kell hozzá (Windowson pl. MinGW; a PlatformIO `toolchain-gccmingw32` csomagja megfelel). A natív környezet `-std=gnu++14`-gyel fordít. Nagyon hosszú `PATH` esetén (8191 karakteres korlát) a fordító indítása érthetetlen hibával elbukhat: ilyenkor rövid `PATH`-szal futtasd, például PowerShellben: `$env:PATH='C:\.platformio\packages\toolchain-gccmingw32\bin;C:\Windows\System32;C:\Windows'`. Várt eredmény: 50 teszt, 5 csomag sikeres.

## 8. Soros napló és hibaelhárítás

Normál indulás sorrendje: `[boot] hangszoro`, `[cfg] loaded`, `[i2s] started, 44100 Hz ...`, `[net] connected, ip=...`, `[ws] connecting to <ip>:8766/...`, `[ws] connected`, `[ws] STREAM_INFO mp3 44100 Hz 1ch 128 kbps`, `[ws] mixer status=0x..`.

| Napló / tünet | Jelentés, teendő |
|---|---|
| `[cfg] config.json: ...` | Hibás vagy érvénytelen mező, az alapérték él. Javítsd a fájlt és töltsd fel újra (`uploadfs`). `/config.json not found`: nincs feltöltve a fájl. |
| `[net] connect timeout`, `[net] link down, reconnecting` | A WiFi nem csatlakozik. Ellenőrizd az SSID-t, a jelszót, a térerőt, a statikus IP-t, maszkot, átjárót. |
| `[ws] disconnected (attempt N, reason: ... HTTP 403)` és `server rejected the handshake` | A szerver elutasította: hibás, lejárt vagy cserélt token, az eszköz IP-je nem egyezik a felvettel, vagy az eszköz le van tiltva az admin felületen. |
| Ismétlődő `disconnected`, várakozás 1, 2, 5, 10, majd 30 s | Hálózati vagy szerverhiba. 5 hiba után: `repeated failures: check endpoint, static IP, network and token`. Ellenőrizd a szerver címét, a portot, az útvonalat. |
| `[ws] cannot resolve host`, `repeated start failures: check host, DNS and firewall settings` | A hosztnév nem oldható fel (DNS), vagy a cím tiltott. Ugyanezzel a visszalépéssel újrapróbálja. |
| `[fw] endpoint blocked by firewall` | A cél nem engedélyezett. Ellenőrizd a hostot és a portot. |
| `[ws] no data for 5 s, reconnecting` | A mixer folyamatosan küld adatot (csend alatt is); 5 s hallgatás néma kapcsolatvesztésnek számít, ezért újracsatlakozik. Ha gyakori, nézd a WiFi minőségét és a szervert. |
| `[ws] buffer overflow, flushing` | A puffer megtelt, az eszköz kiüríti és újrakezdi. Ha gyakori, növeld a `buffer.ms`-t vagy ellenőrizd a hálózatot. |
| `[ws] seq gap N` | N csomag elveszett (hálózati csomagvesztés). |
| `[ws] malformed packets: N` | Hibás csomagok érkeztek, eldobva. |
| `[ws] mixer status=0x..` | A mixer állapota: 1 zene, 2 mikrofon, 4 csengő, 0 csend. |
| `[i2s] driver install failed`, `[i2s] set pin failed` | Az I2S nem indult: ellenőrizd a tűket (nem lehetnek foglalt GPIO-k). |
| Nincs hang, de a napló rendben | Ellenőrizd a PCM5102A bekötést (SCK, FMT, XSMT) és a tápot. |
