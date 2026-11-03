# OLED debug kijelző – terv

Dátum: 2026-11-03

## Cél és miért

A hangszóró hibáit most csak soros porton lehet nézni, ehhez gép kell a kártya mellé. Egy kis I2C-s OLED kijelző (4 láb: GND, VCC, SCL, SDA) gép nélkül, egy pillantással megmutatja, mi történik: van-e WiFi, él-e a stream, jön-e hang, és ha nem szól, hol akad el. Debug eszköz, a fejlesztői buildhez.

## Célok

- Három oldal, a BOOT gomb körbe lépteti őket (1 → 2 → 3 → 1). Induláskor az 1. oldal látszik.
- Az oldalak tartalma az alábbi, a felhasználó által jóváhagyott vázlatok szerint.
- A kijelző magától frissül, nem kell hozzá semmit csinálni.

## Nem cél

- Éles (prod) buildben a kijelző. Ott a kód ki sem fordul.
- Beállítás vagy vezérlés a kijelzőről (menü, hangerő stb.).
- SPI-s kijelző, érintés, további gombok.
- A `setup.py` varázsló bővítése. Az `oled` csoport opcionális, az alapérték elég.

## Amit semmiképp nem szabad

- A kijelző miatt nem ropoghat, akadhat vagy szakadhat meg a hang.
- Ha nincs kijelző bekötve, vagy rossz a bekötése, az eszköz ne akadjon meg, és ne induljon újra. Ilyenkor egyszer naplózza, hogy nem találja, és minden más ugyanúgy megy.
- A prod buildben nincs kijelzőkód, I2C és U8g2, és a GPIO 8-at és 18-at sem használja; a viselkedése nem változik. A config `oled` csoportjának beolvasása és a szálbiztos számlálók miatt kb. 1,7 KB-tal nagyobb; ezt a felhasználó elfogadta (2026-11-03).

## Hardver

- 1.3"-os, 128×64-es monokróm OLED, I2C, cím `0x3C`. Vezérlő: **SH1106** (a felhasználó adta meg). A választott könyvtárnak natívan kell támogatnia az SH1106-ot. Ennek a vezérlőnek 132 oszlopos a memóriája, ezért 2 pixeles oszlopeltolás kell, különben a kép elcsúszik vagy szemetes. Az SSD1306-os meghajtó ezzel a kijelzővel nem működik helyesen.
- Bekötés: GND → GND, VCC → 3V3, SDA és SCL a configban megadott tűkre.
- Alap tűk: SDA = GPIO 8, SCL = GPIO 18 (a felhasználó adta meg; nem ütköznek az I2S 15/16/17-tel). A configban átállíthatók.
- BOOT gomb: GPIO 0, a kártyán már rajta van, külön bekötés nem kell.

## Build

- Új kapcsoló: `HANGSZORO_OLED`. Az `env:dev` 1-re, az `env:prod` 0-ra állítja. Ha nincs megadva, 0. *Saját döntés: a felhasználó „debug formában” kérte.*
- 0 esetén a kijelző kódja és könyvtára nem fordul be, a `main.cpp` hívásai kiesnek (ugyanúgy, ahogy a `Log.h` kezeli a soros naplót).
- A kijelzőkönyvtár verziója rögzítve legyen a `platformio.ini`-ben, mint a többi függőségé.

## Config

Új, opcionális csoport a `config.json`-ban:

```json
"oled": {
  "sda": 8,
  "scl": 18
}
```

- A csoport egyben érvényes vagy egyben elutasított, ahogy az `i2s` csoport.
- Érvénytelen, ha a két tű azonos, vagy bármelyik egyezik egy I2S tűvel, vagy kívül esik a 0..48 tartományon. Ilyenkor figyelmeztetés kerül a naplóba, és az alapértékek maradnak.
- Alapértékek az `include/config_defaults.h`-ban (`CFG_OLED_SDA`, `CFG_OLED_SCL`).
- A `data/config.example.json` kapja meg az új csoportot. A README-be kerüljön egy rövid bekötési rész.

## Oldalak

A betűkészlet 6×8-as, egy sorba 21 karakter fér. A feliratok ékezet nélküliek.

### 1. oldal: Áttekintés

```
Hangszoro      -62dBm
192.168.1.50    WS:OK
128k 44.1k ST buf 78%
L ██████████░░░░  -12
R ████████░░░░░░  -15
SIGNAL  lost:3  1:23h
```

| Sor | Tartalom |
|---|---|
| 1 | Eszköznév (ha hosszú, levágva), jobbra a WiFi jelerőssége dBm-ben. Ha nincs WiFi: `NO WIFI`. |
| 2 | Saját IP, jobbra a stream állapota: `WS:OK` (kapcsolódva), `WS:..` (kapcsolódik), `WS:E<n>` (n egymás utáni hiba után vár az újrapróbálkozásra). *A jelölések saját döntés.* |
| 3 | Bitráta a `STREAM_INFO`-ból (kbps), a ténylegesen dekódolt mintavétel kHz-ben, `ST` vagy `MO` (mono), puffer telítettsége %-ban (`available / capacity`). |
| 4–5 | Bal és jobb csatorna csúcsszintje csíkként, mellette a szám dBFS-ben. A csík skálája -60..0 dBFS. *Saját döntés.* |
| 6 | Állapot: `SIGNAL`, `SILENCE`, `NO DATA` (a `SignalMeter` szerint), elveszett csomagok száma boot óta, uptime `<óra>:<perc>h` alakban (pl. `0:07h`, `1:23h`). |

- Ha nincs WiFi, a 2. sor jobb oldalán `WS:--` áll. *Saját döntés.*
- Az eszköznév UTF-8 lehet ékezettel (a config ezt megengedi). A magyar ékezetes betűk ékezet nélkül jelennek meg (á → a, ő → o, Ű → U), a többi nem ASCII karakter helyén `?` áll. *Saját döntés.*

### 2. oldal: Hang

```
HANG    44.1kHz ST
   █
  ███ █
 █████████  █
████████████████▄▂ ▁
.1  .3   1    3   8 k
```

- Fejléc: a dekódolt mintavétel kHz-ben, és `ST` vagy `MO`.
- Alatta spektrum: függőleges csíkok, logaritmikus frekvenciasávokra osztva, kb. 100 Hz-től 16 kHz-ig, a zenére mozognak. A csíkok száma kb. 16–21. *A pontos szám az építőre van bízva.*
- Alsó sor: frekvenciatengely-feliratok. Minden jelölő a saját frekvenciáját tartalmazó sáv alatt áll (100 Hz, 300 Hz, 1, 3, 8 kHz), a sor végén a `k` a mértékegység.
- Ha nincs hang (`NO DATA`), a csíkok üresek, és a fejlécben `-- kHz` áll.

### 3. oldal: Részletek

```
RESZLETEK
WiFi -62dBm
WS hibak: 0
drop 0 bad 0 lost 3
utolso csomag: 0.2s
heap 182k  up 1:23:05
build Nov  3 2026
```

- WiFi jelerősség, egymás utáni WS-hibák száma.
- A `StreamClient` számlálói: eldobott bájtok (`dropped`), hibás csomagok (`malformed`), elveszett csomagok (`lost`).
- Mennyi ideje jött az utolsó csomag (ha nincs kapcsolat: `-`).
- Szabad heap kB-ban, uptime `ó:pp:mm` alakban (100 óra felett `<nap>d<óra>h`), build dátuma (`__DATE__`).
- Szám sosem vágódik félbe. Ha a számlálók sora nem fér ki, rövid alakra vált (`D:4G B:99k L:123k`). A leghosszabb IP mellett a `WS:` előtag elmarad (`192.168.100.200   E10`).

## Gomb

- BOOT (GPIO 0) rövid lenyomása: következő oldal. Pergésmentesítve, egy lenyomás egy lépés.
- A gomb csak boot után működik így. Induláskor a BOOT továbbra is a letöltési módot jelenti, ezen nem változtatunk.

## Frissítés

- Az 1. és a 2. oldal kb. 10-szer másodpercenként frissül, a 3. oldal elég 2-szer. *Saját döntés.*
- Oldalváltáskor azonnal kirajzolja az új oldalt.

## Adatok és szálak

- Ma a `SignalMeter`-t csak a soros napló tölti, és másodpercenként egyszer üríti (`reportSignal`). A kijelzőnek saját, gyakoribb mérés kell; a soros napló viselkedése nem változik.
- A `StreamClient` számlálói (`dropped_`, `malformed_`, `lost_`, `failures_`, `lastRxMs_`) most privátak. Csak olvasható lekérdezés kell hozzájuk, a meglévő viselkedés nem változik.
- A dekóder task (1-es mag, prioritás 2) a hang szempontjából kritikus. Az I2C-küldés (egy teljes kép kb. 1 kB) nem futhat benne. A spektrum számítása csak úgy kerülhet bele, hogy mérhetően ne lassítsa a dekódolást.
- Szálak közti adatátadás zár nélkül vagy rövid kritikus szakasszal, ahogy a projekt többi része (atomic, SPSC).

## Tesztek

- A tiszta logika (oldalléptetés, formázás: uptime, kHz, dBm, csíkszélesség dBFS-ből, spektrumsávok frekvenciahatárai, config `oled` csoport ellenőrzése) a hardvertől függetlenül, `native` környezetben tesztelve, a meglévő `test/` mintájára.
- A `native` env `build_src_filter`-je kapja meg az új, hardverfüggetlen fájlokat.
- Kézi ellenőrzés a kártyán: mind a három oldal, gombléptetés, kihúzott kijelzővel indítás, WiFi- és szerverkiesés közben a kijelző, a hang ropogásmentes-e kijelzővel és anélkül.

## Az építőre bízva

- A kijelzőkönyvtár (SH1106-képes legyen, pl. U8g2 vagy Adafruit SH110X + GFX) és az FFT megoldás (pl. ESP-DSP vagy arduinoFFT).
- Hol fusson a kijelző frissítése (`loop()` vagy külön, alacsony prioritású task), ha a fenti korlátok teljesülnek.
- A fájlok elnevezése és szerkezete a meglévő `src/services` és `src/model` minta szerint.
