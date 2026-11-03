# OLED debug kijelző – megvalósítási terv

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A dev buildben egy 1.3"-os SH1106 I2C OLED három oldalon mutatja a hálózat, a stream és a hang állapotát. Az oldalakat a BOOT gomb lépteti.

**Architecture:** A tiszta logika (config `oled` csoport, szövegformázás, gomb-pergésmentesítés, PCM-csatorna, FFT és spektrumsávok) hardverfüggetlen C++ a `src/display/` alatt, natív tesztekkel. Egyetlen eszközfüggő fájl van (`src/display/DebugDisplay.cpp`): ez egy saját FreeRTOS taskban (0-s mag, prioritás 1) rajzol U8g2-vel. A dekóder task csak bemásolja a PCM-et egy SPSC ring bufferbe (`AudioTap`), az FFT és az I2C-küldés a kijelző taskjában fut.

**Tech Stack:** PlatformIO, Arduino-ESP32 2.0.17, `olikraus/U8g2@2.36.18` (csak `env:dev`), Unity (natív tesztek).

**Spec:** `docs/superpowers/specs/2026-11-03-oled-debug-display-design.md`

## Global Constraints

- Kijelző: 1.3", 128×64, SH1106, I2C cím `0x3C`, U8g2 konstruktor: `U8G2_SH1106_128X64_NONAME_F_HW_I2C`.
- Alap tűk: SDA = GPIO 8, SCL = GPIO 18; BOOT = GPIO 0 (aktív alacsony, `INPUT_PULLUP`).
- Build: `HANGSZORO_OLED` (alapértéke 0); `env:dev` = 1, `env:prod` = 0. A prod nem fordítja a `src/display/` alatti fájlokat, és nem húzza be az U8g2-t.
- A függőségek verziója rögzített (`olikraus/U8g2@2.36.18`).
- Egy sor legfeljebb 21 karakter. A feliratok ékezet nélküliek. Betűtípus: `u8g2_font_6x10_tf`, a sorok alapvonala `y = 8 + 9 * sor` (sor = 0..6).
- Frissítés: az 1. és 2. oldal 100 ms-onként, a 3. oldal 500 ms-onként, oldalváltáskor azonnal.
- A dekóder taskban nem lehet I2C-hívás, FFT vagy blokkoló művelet.
- A kódkommentek magyarul, az azonosítók angolul vannak, a meglévő stílus szerint.
- Natív tesztek: `pio test -e native`. Build: `pio run -e dev` és `pio run -e prod`.
- Commit csak a felhasználó jóváhagyásával. A commit üzenetek angolul, `feat:`/`test:`/`docs:` előtaggal.

## Review Focus

1. Nincs kijelző bekötve vagy rossz a bekötése: az eszköz elindul, egyszer naplózza a hibát, a hang megy, és az `onPcm` semmit nem csinál (Task 7, 3. lépés és kézi teszt).
2. Saját `i2s` tűk ütköznek az alapértelmezett OLED tűkkel (pl. `bck: 8`): a kijelző letiltva, a hang nem sérül (Task 1: `test_oled_default_conflicts_with_custom_i2s`; Task 7: `oledPinsValid` ellenőrzés a `begin`-ben).
3. Hosszú vagy ékezetes eszköznév (64 karakter, „Hangszóró Nappali”): az 1. sorban az RSSI megmarad, a név rövidül, az ékezet eltűnik (Task 2: `test_overview_long_accented_name`).
4. Szélsőséges értékek: `lost` = 123456, uptime 150 óra, teli puffer, 320 kbps. Egyik sor sem lehet 21 karakternél hosszabb (Task 2: `test_no_line_exceeds_21`).
5. Mono vagy alacsony mintavételű stream, illetve 0 Hz az első dekódolás előtt: `MO`, `-- kHz`, és a Nyquist feletti sávok üresek (Task 2: `test_sound_header_no_data`; Task 5: `test_bands_above_nyquist_empty`).

---

### Task 1: Config `oled` csoport

**Files:**
- Modify: `include/config_defaults.h` (a `CFG_I2S_*` után)
- Modify: `src/model/Config.h`, `src/model/Config.cpp` (`defaultConfig`, `applyJson`: új blokk az `i2s` blokk után)
- Modify: `data/config.example.json` (új `"oled": {"sda": 8, "scl": 18}` csoport az `i2s` után)
- Test: `test/test_config/test_main.cpp`

**Interfaces:**
- Produces: `Config::oledSda`, `Config::oledScl` (`int`); `bool oledPinsValid(const Config& cfg);`: igaz, ha mindkét tű 0..48 között van, különbözők, és egyik sem egyezik a `bck`/`lrck`/`din` tűvel.

- [ ] **Step 1: Write the failing tests** (`RUN_TEST`-tel regisztrálva a `main()`-ben)

```cpp
void test_oled_defaults() {
  Config c = defaultConfig();
  TEST_ASSERT_EQUAL_INT(8, c.oledSda);
  TEST_ASSERT_EQUAL_INT(18, c.oledScl);
  TEST_ASSERT_TRUE(oledPinsValid(c));
}
void test_oled_override() {
  Config c = defaultConfig();
  TEST_ASSERT_EQUAL(0, applyJson(c, R"({"oled":{"sda":4,"scl":5}})").size());
  TEST_ASSERT_EQUAL_INT(4, c.oledSda);
  TEST_ASSERT_EQUAL_INT(5, c.oledScl);
}
void test_oled_same_pins_rejected() {
  Config c = defaultConfig();
  auto w = applyJson(c, R"({"oled":{"sda":4,"scl":4}})");
  TEST_ASSERT_TRUE(anyContains(w, "oled group ignored"));
  TEST_ASSERT_EQUAL_INT(8, c.oledSda);
  TEST_ASSERT_EQUAL_INT(18, c.oledScl);
}
void test_oled_overlaps_i2s_rejected() {
  Config c = defaultConfig();
  auto w = applyJson(c, R"({"oled":{"sda":15,"scl":5}})");  // 15 = i2s.bck
  TEST_ASSERT_TRUE(anyContains(w, "oled group ignored"));
  TEST_ASSERT_EQUAL_INT(8, c.oledSda);
}
void test_oled_out_of_range() {
  Config c = defaultConfig();
  auto w = applyJson(c, R"({"oled":{"sda":49}})");
  TEST_ASSERT_TRUE(anyContains(w, "oled.sda is out of range"));
  TEST_ASSERT_EQUAL_INT(8, c.oledSda);
}
void test_oled_default_conflicts_with_custom_i2s() {
  Config c = defaultConfig();
  TEST_ASSERT_EQUAL(0, applyJson(c, R"({"i2s":{"bck":8,"lrck":2,"din":3}})").size());
  TEST_ASSERT_FALSE(oledPinsValid(c));
}
```

- [ ] **Step 2: Run, expect FAIL**

Run: `pio test -e native -f test_config`
Expected: fordítási hiba (`oledSda`, `oledPinsValid` nincs).

- [ ] **Step 3: Implement**

`config_defaults.h`: `#define CFG_OLED_SDA 8`, `#define CFG_OLED_SCL 18`. A `Config` struktúrába a `din` után `int oledSda = 0; int oledScl = 0;` kerül. Az `applyJson` blokkja az `i2s` mintáját követi: `readInt(oled["sda"], "oled.sda", 0, 48, …)` és ugyanez `scl`-re. Ha bármelyik megvan, egy jelölt `Config` másolaton `oledPinsValid` dönt. Ha érvénytelen, a figyelmeztetés: `"oled pins must be distinct and must not overlap i2s pins; oled group ignored"`.

- [ ] **Step 4: Run, expect PASS**

Run: `pio test -e native -f test_config`
Expected: minden teszt PASS, a régiek is.

- [ ] **Step 5: Commit**

```bash
git add include/config_defaults.h src/model/Config.h src/model/Config.cpp data/config.example.json test/test_config/test_main.cpp docs/superpowers/specs/2026-11-03-oled-debug-display-design.md docs/superpowers/plans/2026-11-03-oled-debug-display.md
git commit -m "feat: add oled pin config group"
```

---

### Task 2: Kijelző-szövegek (`Format`)

**Files:**
- Create: `src/display/Format.h`, `src/display/Format.cpp`
- Modify: `platformio.ini` (`env:native` → `build_src_filter`: `+<display/Format.cpp>`)
- Test: `test/test_display_format/test_main.cpp`

**Interfaces:**
- Consumes: `SignalMeter::State` (`src/services/SignalMeter.h`).
- Produces (namespace `disp`):

```cpp
constexpr size_t kCols = 21;
struct Snapshot {
  std::string deviceName;           // UTF-8
  bool wifiUp = false;  int rssi = 0;  uint32_t ip = 0;   // host byte order
  bool wsConnected = false;  uint32_t wsFailures = 0;      // StreamClient + begin() hibák összege
  uint32_t bitrateKbps = 0, sampleRate = 0;  uint8_t channels = 0;
  size_t bufAvailable = 0, bufCapacity = 0;
  SignalMeter::State state = SignalMeter::State::NoData;
  float dbL = -99, dbR = -99;
  uint32_t dropped = 0, malformed = 0, lost = 0, lastRxAgeMs = 0;
  uint32_t freeHeap = 0;  uint64_t uptimeMs = 0;  std::string buildDate;
};
// Bal szöveg + jobbra igazított jobb szöveg, pontosan kCols szélességben (szóközzel kitöltve).
// Ha nem fér ki, és keepRight igaz, a bal oldal rövidül (kCols - 1 - right.size() karakterre).
// Ha nem fér ki, és keepRight hamis, a jobb oldal elmarad: a bal oldal kitöltés nélkül, kCols-ra vágva.
std::string fitLine(const std::string& left, const std::string& right, bool keepRight);
std::string asciiFold(const std::string& utf8);    // á→a é→e í→i ó/ö/ő→o ú/ü/ű→u (nagybetűvel is), más nem ASCII kódpont → '?'
std::string khzText(uint32_t hz);                  // 44100→"44.1", 48000→"48", 22050→"22.1", 0→"--" (tizedek: (hz+50)/100)
std::string compactCount(uint32_t n);              // 999→"999", 1000→"1k", 12345→"12k", 2500000→"2M", 4e9→"4G" (lefelé kerekítve)
int dbToPixels(float dbfs, int maxPx);             // [-60,0] lineárisan 0..maxPx-re, kerekítve, levágva
std::string levelText(float dbfs);                 // kerekített egész [-60,0]-ra vágva: "-12", "-60", "0"
std::array<std::string, 4> overviewText(const Snapshot& s);  // 1. oldal 1., 2., 3. és 6. sora
std::string soundHeader(const Snapshot& s);                  // 2. oldal fejléce
std::array<std::string, 7> detailsText(const Snapshot& s);   // 3. oldal
extern const char* const kAxisLabels;                        // ".1  .3  1   3   8 16k"
```

A sorok szabályai (mind `fitLine`-on megy át):
- 1. oldal: `fitLine(asciiFold(név), wifiUp ? "-62dBm" : "NO WIFI", true)`. `fitLine(IP, ws, true)`, ahol `ws` = `WS:--` (nincs WiFi), `WS:OK` (kapcsolódva), `WS:..` (0 hiba), `WS:E<n>`. `fitLine("<kbps>k <khz>k <ST|MO|-->", "buf <pct>%", true)`, ahol `pct = available*100/capacity`, felülről 99-re vágva (0 kapacitásnál 0). `fitLine("<SIGNAL|SILENCE|NO DATA>  lost:<compact>", uptimeShort, false)`, ahol `uptimeShort` 100 óra alatt `ó:ppm` + `h` (`1:23h`, `0:07h`), felette `<ó>h`.
- 2. oldal fejléce: `fitLine("HANG", "<khz>kHz <ST|MO>", true)`. `NoData` vagy 0 Hz esetén a jobb oldal `-- kHz`.
- 3. oldal: `RESZLETEK`, `WiFi -62dBm` / `WiFi --`, `WS hibak: <n>`, `drop <c> bad <c> lost <c>` (compactCount), `utolso csomag: <kor>` (nincs kapcsolat: `-`; 10 s alatt `0.2s` egy tizedessel, felette `12s`), `fitLine("heap <kB>k", "up <ó>:<pp>:<mm>", true)`, `build <buildDate>`. A jobb rész nélküli sorok kitöltés nélkül, `kCols`-ra vágva jönnek vissza.

- [ ] **Step 1: Write the failing tests**

```cpp
static disp::Snapshot mock() {  // a jóváhagyott vázlat értékei
  disp::Snapshot s;
  s.deviceName = "Hangszoro"; s.wifiUp = true; s.rssi = -62; s.ip = 0xC0A80132u;
  s.wsConnected = true; s.bitrateKbps = 128; s.sampleRate = 44100; s.channels = 2;
  s.bufAvailable = 78; s.bufCapacity = 100; s.state = SignalMeter::State::Signal; s.lost = 3;
  s.lastRxAgeMs = 200; s.freeHeap = 182 * 1024; s.uptimeMs = (1 * 3600 + 23 * 60 + 5) * 1000ULL;
  s.buildDate = "Nov  3 2026";
  return s;
}
void test_overview_matches_mock() {
  auto t = disp::overviewText(mock());
  TEST_ASSERT_EQUAL_STRING("Hangszoro      -62dBm", t[0].c_str());
  TEST_ASSERT_EQUAL_STRING("192.168.1.50    WS:OK", t[1].c_str());
  TEST_ASSERT_EQUAL_STRING("128k 44.1k ST buf 78%", t[2].c_str());
  TEST_ASSERT_EQUAL_STRING("SIGNAL  lost:3  1:23h", t[3].c_str());
}
void test_details_matches_mock() {
  auto t = disp::detailsText(mock());
  TEST_ASSERT_EQUAL_STRING("RESZLETEK", t[0].c_str());
  TEST_ASSERT_EQUAL_STRING("WiFi -62dBm", t[1].c_str());
  TEST_ASSERT_EQUAL_STRING("WS hibak: 0", t[2].c_str());
  TEST_ASSERT_EQUAL_STRING("drop 0 bad 0 lost 3", t[3].c_str());
  TEST_ASSERT_EQUAL_STRING("utolso csomag: 0.2s", t[4].c_str());
  TEST_ASSERT_EQUAL_STRING("heap 182k  up 1:23:05", t[5].c_str());
  TEST_ASSERT_EQUAL_STRING("build Nov  3 2026", t[6].c_str());
}
void test_ws_labels() {
  auto s = mock();
  s.wifiUp = false;                          TEST_ASSERT_EQUAL_STRING("192.168.1.50    WS:--", disp::overviewText(s)[1].c_str());
  TEST_ASSERT_EQUAL_STRING("Hangszoro     NO WIFI", disp::overviewText(s)[0].c_str());
  s.wifiUp = true; s.wsConnected = false;    TEST_ASSERT_EQUAL_STRING("192.168.1.50    WS:..", disp::overviewText(s)[1].c_str());
  s.wsFailures = 3;                          TEST_ASSERT_EQUAL_STRING("192.168.1.50    WS:E3", disp::overviewText(s)[1].c_str());
  TEST_ASSERT_EQUAL_STRING("utolso csomag: -", disp::detailsText(s)[4].c_str());
}
void test_overview_long_accented_name() {
  auto s = mock();
  s.deviceName = "Hangsz\xC3\xB3r\xC3\xB3 Nappali \xC5\x91sz";  // "Hangszóró Nappali ősz"
  TEST_ASSERT_EQUAL_STRING("Hangszoro Napp -62dBm", disp::overviewText(s)[0].c_str());
  TEST_ASSERT_EQUAL_STRING("a?b", disp::asciiFold("a\xE2\x82\xAC" "b").c_str());  // € → ?
}
void test_sound_header_no_data() {
  auto s = mock();
  TEST_ASSERT_EQUAL_STRING("HANG       44.1kHz ST", disp::soundHeader(s).c_str());
  s.channels = 1; s.sampleRate = 22050;
  TEST_ASSERT_EQUAL_STRING("HANG       22.1kHz MO", disp::soundHeader(s).c_str());
  s.state = SignalMeter::State::NoData;
  TEST_ASSERT_EQUAL_STRING("HANG           -- kHz", disp::soundHeader(s).c_str());
}
void test_no_line_exceeds_21() {
  auto s = mock();
  s.deviceName = std::string(64, 'X'); s.lost = 123456; s.dropped = 4000000000u; s.malformed = 99999;
  s.uptimeMs = 150ULL * 3600 * 1000; s.bufAvailable = 100; s.bitrateKbps = 320; s.wsFailures = 12345;
  s.state = SignalMeter::State::NoData;
  for (auto& l : disp::overviewText(s)) TEST_ASSERT_TRUE(l.size() <= 21);
  for (auto& l : disp::detailsText(s)) TEST_ASSERT_TRUE(l.size() <= 21);
  TEST_ASSERT_TRUE(disp::soundHeader(s).size() <= 21);
  TEST_ASSERT_EQUAL_STRING("320k 44.1k ST buf 99%", disp::overviewText(s)[2].c_str());
  TEST_ASSERT_EQUAL_STRING("NO DATA  lost:123k", disp::overviewText(s)[3].c_str());  // az uptime nem fér ki, elmarad
}
void test_helpers() {
  TEST_ASSERT_EQUAL_STRING("48", disp::khzText(48000).c_str());
  TEST_ASSERT_EQUAL_STRING("--", disp::khzText(0).c_str());
  TEST_ASSERT_EQUAL_STRING("2M", disp::compactCount(2500000).c_str());
  TEST_ASSERT_EQUAL_INT(84, disp::dbToPixels(0.0f, 84));
  TEST_ASSERT_EQUAL_INT(0, disp::dbToPixels(-75.0f, 84));
  TEST_ASSERT_EQUAL_INT(42, disp::dbToPixels(-30.0f, 84));
  TEST_ASSERT_EQUAL_STRING("-12", disp::levelText(-12.4f).c_str());
  TEST_ASSERT_EQUAL_STRING("-60", disp::levelText(-99.0f).c_str());
  TEST_ASSERT_EQUAL_UINT(21, strlen(disp::kAxisLabels));
}
```

- [ ] **Step 2: Run, expect FAIL**

Run: `pio test -e native -f test_display_format`
Expected: fordítási hiba (`display/Format.h` nincs).

- [ ] **Step 3: Implement** a `src/display/Format.{h,cpp}`-ben a fenti szabályok szerint. Az IP-t a `Snapshot::ip` mezőből kell formázni (`a.b.c.d`), nem a WiFi-ből.

- [ ] **Step 4: Run, expect PASS**

Run: `pio test -e native`
Expected: minden teszt PASS.

- [ ] **Step 5: Commit**

```bash
git add src/display/Format.h src/display/Format.cpp test/test_display_format/test_main.cpp platformio.ini
git commit -m "feat: add debug display text formatting"
```

---

### Task 3: BOOT gomb és oldalléptetés (`PageSelector`)

**Files:**
- Create: `src/display/PageSelector.h`, `src/display/PageSelector.cpp`
- Modify: `platformio.ini` (`env:native`: `+<display/PageSelector.cpp>`)
- Test: `test/test_page_selector/test_main.cpp`

**Interfaces:**
- Produces:

```cpp
class PageSelector {
 public:
  static constexpr uint8_t kPages = 3;
  static constexpr uint32_t kDebounceMs = 30;
  // pressed: a gomb nyers állapota (pereghet). Igaz, ha ettől a hívástól oldal váltott.
  bool update(bool pressed, uint32_t nowMs);
  uint8_t page() const;  // 0..kPages-1, induláskor 0
};
```

Egy lenyomás akkor számít, ha a nyers állapot legalább `kDebounceMs`-ig változatlanul `pressed`, és előtte stabilan elengedett volt. Nyomva tartás nem ismétel. Az időkülönbség előjel nélküli kivonással számolandó (millis-túlcsordulás).

- [ ] **Step 1: Write the failing tests**

```cpp
void test_starts_on_first_page() { PageSelector p; TEST_ASSERT_EQUAL_UINT8(0, p.page()); }
void test_stable_press_advances_once() {
  PageSelector p;
  TEST_ASSERT_FALSE(p.update(true, 0));
  TEST_ASSERT_FALSE(p.update(true, 29));
  TEST_ASSERT_TRUE(p.update(true, 30));
  TEST_ASSERT_EQUAL_UINT8(1, p.page());
  TEST_ASSERT_FALSE(p.update(true, 2000));  // nyomva tartás: nincs ismétlés
}
void test_bounce_ignored() {
  PageSelector p;
  p.update(true, 0); p.update(false, 10); p.update(true, 20); p.update(false, 25);
  TEST_ASSERT_FALSE(p.update(false, 100));
  TEST_ASSERT_EQUAL_UINT8(0, p.page());
}
void test_wraps_after_third_page() {
  PageSelector p;
  uint32_t t = 0;
  for (int i = 0; i < 3; i++) { p.update(true, t); p.update(true, t + 30); p.update(false, t + 40); p.update(false, t + 80); t += 100; }
  TEST_ASSERT_EQUAL_UINT8(0, p.page());
}
void test_millis_wraparound() {
  PageSelector p;
  p.update(true, 0xFFFFFFF0u);
  TEST_ASSERT_TRUE(p.update(true, 0x0000000Fu));  // 31 ms telt el
}
```

- [ ] **Step 2: Run, expect FAIL**: `pio test -e native -f test_page_selector`, fordítási hiba.
- [ ] **Step 3: Implement** `PageSelector`-t a fenti szerződés szerint.
- [ ] **Step 4: Run, expect PASS**: `pio test -e native -f test_page_selector`.
- [ ] **Step 5: Commit**

```bash
git add src/display/PageSelector.h src/display/PageSelector.cpp test/test_page_selector/test_main.cpp platformio.ini
git commit -m "feat: add debounced page selector for BOOT button"
```

---

### Task 4: PCM-csatorna a kijelzőhöz (`AudioTap`)

**Files:**
- Create: `src/display/AudioTap.h`, `src/display/AudioTap.cpp`
- Modify: `platformio.ini` (`env:native`: `+<display/AudioTap.cpp>`)
- Test: `test/test_audio_tap/test_main.cpp`

**Interfaces:**
- Consumes: `RingBuffer` (`src/services/RingBuffer.h`, SPSC: egy író, egy olvasó).
- Produces:

```cpp
// A dekóder task (író) és a kijelző task (olvasó) között. Mindig sztereó int16 kereteket tárol (4 B).
class AudioTap {
 public:
  explicit AudioTap(size_t capacityFrames);
  // Csak a dekóder task hívja. Mono → mindkét csatorna. Ami nem fér el, elveszik; csak egész keretet ír.
  void push(const int16_t* pcm, size_t frames, uint8_t channels, uint32_t sampleRate);
  // Csak a kijelző task hívja. Legfeljebb maxFrames sztereó keret (L,R,L,R…), visszaadja a darabszámot.
  size_t pull(int16_t* outStereo, size_t maxFrames);
  uint32_t sampleRate() const;  // utolsó push, előtte 0 (atomic)
  uint8_t channels() const;     // utolsó push, előtte 0 (atomic)
};
```

A `push` a mono→sztereó átalakítást kis, veremre tett darabokban (legfeljebb 256 keret) végzi, és mindig `freeSpace() / 4 * 4` bájtnál nem többet ír. Így a `RingBuffer` `used_` értéke mindig 4 többszöröse marad.

- [ ] **Step 1: Write the failing tests**

```cpp
void test_mono_duplicated() {
  AudioTap t(16); const int16_t pcm[] = {1, 2, 3}; int16_t out[16];
  t.push(pcm, 3, 1, 22050);
  TEST_ASSERT_EQUAL_UINT(3, t.pull(out, 8));
  const int16_t want[] = {1, 1, 2, 2, 3, 3};
  TEST_ASSERT_EQUAL_INT16_ARRAY(want, out, 6);
  TEST_ASSERT_EQUAL_UINT32(22050, t.sampleRate());
  TEST_ASSERT_EQUAL_UINT8(1, t.channels());
}
void test_stereo_passthrough_and_partial_pull() {
  AudioTap t(16); const int16_t pcm[] = {1, -1, 2, -2, 3, -3}; int16_t out[16];
  t.push(pcm, 3, 2, 44100);
  TEST_ASSERT_EQUAL_UINT(2, t.pull(out, 2));
  TEST_ASSERT_EQUAL_UINT(1, t.pull(out, 8));
  TEST_ASSERT_EQUAL_INT16(3, out[0]); TEST_ASSERT_EQUAL_INT16(-3, out[1]);
}
void test_overflow_keeps_whole_frames() {
  AudioTap t(4); const int16_t pcm[] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6}; int16_t out[16];
  t.push(pcm, 6, 2, 44100);
  TEST_ASSERT_EQUAL_UINT(4, t.pull(out, 8));
  TEST_ASSERT_EQUAL_INT16(4, out[6]); TEST_ASSERT_EQUAL_INT16(4, out[7]);
  t.push(pcm, 1, 2, 44100);
  TEST_ASSERT_EQUAL_UINT(1, t.pull(out, 8));
  TEST_ASSERT_EQUAL_INT16(1, out[0]);
}
void test_bad_input_ignored() {
  AudioTap t(4); int16_t out[8]; const int16_t pcm[] = {1, 2, 3};
  t.push(nullptr, 3, 2, 44100); t.push(pcm, 1, 3, 44100); t.push(pcm, 1, 0, 44100);
  TEST_ASSERT_EQUAL_UINT(0, t.pull(out, 4));
  TEST_ASSERT_EQUAL_UINT32(0, t.sampleRate());
}
```

- [ ] **Step 2: Run, expect FAIL**: `pio test -e native -f test_audio_tap`, fordítási hiba.
- [ ] **Step 3: Implement** `AudioTap`-et a fenti szerződés szerint.
- [ ] **Step 4: Run, expect PASS**: `pio test -e native -f test_audio_tap`.
- [ ] **Step 5: Commit**

```bash
git add src/display/AudioTap.h src/display/AudioTap.cpp test/test_audio_tap/test_main.cpp platformio.ini
git commit -m "feat: add lock-free PCM tap for the debug display"
```

---

### Task 5: FFT és spektrumsávok (`Spectrum`)

**Files:**
- Create: `src/display/Spectrum.h`, `src/display/Spectrum.cpp`
- Modify: `platformio.ini` (`env:native`: `+<display/Spectrum.cpp>`)
- Test: `test/test_spectrum/test_main.cpp`

**Interfaces:**
- Produces:

```cpp
class Spectrum {
 public:
  static constexpr size_t kSize = 1024;   // FFT hossz (2 hatványa)
  static constexpr size_t kBands = 21;    // = kCols, egy sáv egy karakteroszlop (6 px)
  Spectrum();                             // Hann-ablak előszámítása
  // mono: kSize minta, a legrégebbi elöl. bandsDb: kBands érték dBFS-ben (teljes kivezérlésű szinusz ≈ 0), alsó határ -99.
  void compute(const int16_t* mono, uint32_t sampleRate, float* bandsDb);
  static float bandEdgeHz(size_t i);      // i = 0..kBands: 100 * 160^(i / kBands)
 private:
  float re_[kSize], im_[kSize], window_[kSize];
};
```

Hordozható, helyben számoló radix-2 komplex FFT (float), nincs külső könyvtár. A minták `/32768` normalizálással és Hann-ablakkal mennek be. Bin-szint: `20*log10(2*|X[k]| / Σwindow)`. Egy sáv értéke a `[lo, hi)` sávba eső középfrekvenciájú binek maximuma. Ha egy bin sem esik bele, a sáv mértani közepéhez legközelebbi bin számít. A Nyquist feletti bineket nem veszi figyelembe; ha a sáv alsó határa ≥ Nyquist, az érték -99. Az objektum kb. 12 KB, ezért statikus vagy heapen lévő példány kell, verembe nem tehető.

- [ ] **Step 1: Write the failing tests**

```cpp
static Spectrum sp;  // nagy, nem a veremre
static int16_t buf[Spectrum::kSize];
static void sine(float hz, uint32_t rate, float amp) {
  for (size_t i = 0; i < Spectrum::kSize; i++) buf[i] = (int16_t)(amp * 32767.0f * sinf(2.0f * 3.14159265f * hz * i / rate));
}
static size_t bandOf(float hz) { size_t b = 0; while (b + 1 < Spectrum::kBands && Spectrum::bandEdgeHz(b + 1) <= hz) b++; return b; }

void test_band_edges() {
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, Spectrum::bandEdgeHz(0));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 16000.0f, Spectrum::bandEdgeHz(Spectrum::kBands));
  for (size_t i = 0; i < Spectrum::kBands; i++) TEST_ASSERT_TRUE(Spectrum::bandEdgeHz(i) < Spectrum::bandEdgeHz(i + 1));
}
void test_full_scale_sine_near_0_db() {
  float db[Spectrum::kBands]; sine(1000.0f, 44100, 1.0f); sp.compute(buf, 44100, db);
  TEST_ASSERT_FLOAT_WITHIN(1.5f, -0.75f, db[bandOf(1000.0f)]);  // -2.25 .. +0.75 dB
  TEST_ASSERT_TRUE(db[0] < -40.0f);
}
void test_quieter_sine_lower() {
  float db[Spectrum::kBands]; sine(3000.0f, 44100, 0.1f); sp.compute(buf, 44100, db);
  TEST_ASSERT_FLOAT_WITHIN(2.0f, -20.0f, db[bandOf(3000.0f)]);
}
void test_silence_all_low() {
  float db[Spectrum::kBands]; memset(buf, 0, sizeof(buf)); sp.compute(buf, 44100, db);
  for (size_t i = 0; i < Spectrum::kBands; i++) TEST_ASSERT_TRUE(db[i] <= -90.0f);
}
void test_bands_above_nyquist_empty() {
  float db[Spectrum::kBands]; sine(1000.0f, 16000, 1.0f); sp.compute(buf, 16000, db);  // Nyquist 8 kHz
  TEST_ASSERT_EQUAL_FLOAT(-99.0f, db[Spectrum::kBands - 1]);
}
```

- [ ] **Step 2: Run, expect FAIL**: `pio test -e native -f test_spectrum`, fordítási hiba.
- [ ] **Step 3: Implement** `Spectrum`-ot a fenti szerződés szerint.
- [ ] **Step 4: Run, expect PASS**: `pio test -e native -f test_spectrum`.
- [ ] **Step 5: Commit**

```bash
git add src/display/Spectrum.h src/display/Spectrum.cpp test/test_spectrum/test_main.cpp platformio.ini
git commit -m "feat: add FFT spectrum bands for the debug display"
```

---

### Task 6: `StreamClient` állapot lekérdezése más taskból

**Files:**
- Modify: `src/services/StreamClient.h`, `src/services/StreamClient.cpp`
- Modify: `src/main.cpp` (`beginFailures` → `std::atomic<uint32_t>`)

**Interfaces:**
- Produces:

```cpp
struct StreamStats {
  bool connected;        // a tűzfal-ellenőrzés után kapcsolódva
  uint32_t failures;     // egymás utáni szétkapcsolások (sikeres kapcsolatnál 0)
  uint32_t dropped, malformed, lost;
  uint32_t lastRxMs;     // millis() az utolsó fogadott üzenetkor
};
StreamStats StreamClient::stats() const;  // bármelyik taskból hívható
```

A `failures_`, `dropped_`, `malformed_`, `lost_` és `lastRxMs_` mező `std::atomic<uint32_t>` lesz, és jön egy új `std::atomic<bool> connected_` mező. A `connected_` értéke `WStype_CONNECTED`-nél, a tűzfal-ellenőrzés sikere után lesz `true`; `WStype_DISCONNECTED`-nél és a tűzfal miatti bontásnál `false`. A naplóüzenetek és a viselkedés nem változnak. A `main.cpp`-ben `std::atomic<uint32_t> beginFailures{0};` lesz, a használat ugyanaz marad.

- [ ] **Step 1: Implement** a fentieket.
- [ ] **Step 2: Build és tesztek**

Run: `pio run -e dev && pio run -e prod && pio test -e native`
Expected: mindhárom sikeres, figyelmeztetés nélkül az érintett fájlokra.

- [ ] **Step 3: Commit**

```bash
git add src/services/StreamClient.h src/services/StreamClient.cpp src/main.cpp
git commit -m "feat: expose thread-safe stream stats"
```

---

### Task 7: `DebugDisplay`: U8g2, task, oldalak, bekötés

**Files:**
- Create: `src/display/DebugDisplay.h`, `src/display/DebugDisplay.cpp`
- Modify: `platformio.ini` (`env:dev`, `env:prod`)
- Modify: `src/main.cpp`
- Modify: `README.md` (új „OLED debug kijelző (dev build)” rész)

**Interfaces:**
- Consumes: `oledPinsValid` (Task 1); `disp::Snapshot`, `overviewText`, `soundHeader`, `detailsText`, `dbToPixels`, `levelText`, `kAxisLabels` (Task 2); `PageSelector` (Task 3); `AudioTap` (Task 4); `Spectrum` (Task 5); `StreamClient::stats()`, `bitrateKbps()` (Task 6); `RingBuffer::available()`, `capacity()`; `SignalMeter` (külön példány, csak a kijelző taskban).
- Produces (`DebugDisplay.h`, namespace `debugdisplay`). `HANGSZORO_OLED` alapértéke 0; 0 esetén mindkét függvény inline üres stub, és a fejléc semmit nem húz be a `src/display/` többi fájljából:

```cpp
// Ellenőrzi a tűket, megkeresi a kijelzőt a 0x3C címen, és ha megvan, elindítja a "display" taskot. Különben false.
bool begin(const Config& cfg, StreamClient& client, const RingBuffer& ring, const std::atomic<uint32_t>& beginFailures);
// A dekóder task hívja; ha a kijelző nem indult el, azonnal visszatér.
void onPcm(const int16_t* pcm, size_t frames, uint8_t channels, uint32_t sampleRate);
```

- [ ] **Step 1: `platformio.ini`**

`env:dev`: `build_flags = -DHANGSZORO_SERIAL_LOG=1 -DHANGSZORO_OLED=1` és `lib_deps = ${esp32.lib_deps} olikraus/U8g2@2.36.18`. `env:prod`: a `build_flags`-hez jön a `-DHANGSZORO_OLED=0`, és új sor: `build_src_filter = +<*> -<display/>`.

- [ ] **Step 2: `begin` implementálása**

Ha `!oledPinsValid(cfg)`, a napló: `"[oled] pins overlap i2s or each other, display disabled"`, és `false`. Utána `Wire.begin(cfg.oledSda, cfg.oledScl, 400000)` és `Wire.beginTransmission(0x3C)`. Ha az `endTransmission()` nem 0, a napló: `"[oled] no display at 0x3C (SDA %d, SCL %d), display disabled"`, és `false`. Ha a kijelző megvan: `new U8G2_SH1106_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE)`, `setBusClock(400000)`, `begin()`, `pinMode(0, INPUT_PULLUP)`, `AudioTap(8192)` (32 KB) és a `Spectrum` példány a heapen. Ezután `std::atomic<bool> active = true`, csak ezt követően a task: `xTaskCreatePinnedToCore(…, "display", 6144, …, 1, …, 0)`. Ha a task nem indul, a napló: `"[oled] display task start failed"`.

- [ ] **Step 3: `onPcm` implementálása**: `if (!active.load(std::memory_order_acquire)) return;`, utána `tap->push(...)`. Mást nem csinál.

- [ ] **Step 4: Task ciklus implementálása** (10 ms-os `vTaskDelayUntil`; a nagy pufferek, vagyis a pull-darab, a mono ablak és a `Spectrum`-bemenet, statikusak vagy heapen vannak, nem a task vermén)

- Minden ciklusban: `pages.update(digitalRead(0) == LOW, millis())`. Ha oldal váltott, azonnal rajzol.
- 100 ms-onként: a tap ürítése 512 keretes darabokban. A keretek a kijelző saját `SignalMeter`-ébe mennek, a `(L+R)/2` mono minták pedig egy 1024 elemű körkörös ablakba (a legutolsó 1024 minta). A `meter.take()` adja a `state`-et és a `dbL`/`dbR`-t (`SignalMeter::toDbfs`).
- Rajzolás oldalanként a Global Constraints szerinti periódusban: `Snapshot` összeállítása, ahol `wifiUp = WiFi.status() == WL_CONNECTED`, `rssi = WiFi.RSSI()`, `ip = cfg.ip`, `wsFailures = stats.failures + beginFailures`, `lastRxAgeMs = millis() - stats.lastRxMs`, `freeHeap = ESP.getFreeHeap()`, `uptimeMs = esp_timer_get_time() / 1000`, `buildDate = __DATE__`. Utána `clearBuffer` → rajz → `sendBuffer`.
- 1. oldal: az `overviewText` 4 sora a 0., 1., 2. és 5. szövegsorba kerül. A 3. és 4. sorban `L`/`R` felirat x=0-n, keret x=12..95 (84 px széles, 6 px magas), kitöltés `dbToPixels(db, 84)` szélességgel, és jobbra igazítva a `levelText`.
- 2. oldal: a `soundHeader` a 0. sorba kerül. A csíkok a sor x = i·6 helyén, 5 px szélesek, alul y=53-hoz igazítva, magasságuk `dbToPixels(bandsDb[i], 44)`; a `Spectrum::compute` a körkörös ablak időrendbe tett másolatán fut. A `kAxisLabels` a 6. sorba kerül. `NoData` esetén nincs csík.
- 3. oldal: a `detailsText` 7 sora a 0..6. sorba kerül.

- [ ] **Step 5: `main.cpp` bekötése**

`#include "display/DebugDisplay.h"`. Az `onPcm`-ben a `setSampleRate` után és az `audio.write` előtt: `debugdisplay::onPcm(pcm, samples / channels, channels, rate);`. A `setup()`-ban a `client = new StreamClient(...)` után és a `net.connect(cfg)` előtt: `debugdisplay::begin(cfg, *client, *ring, beginFailures);`. Így a WiFi-csatlakozás alatt már a `NO WIFI` látszik.

- [ ] **Step 6: README**

Új rész a build leírása után. Bekötési táblázat: GND → GND, VCC → 3V3, SCL → GPIO 18, SDA → GPIO 8; a kijelző 1.3" SH1106. Leírja a BOOT gombos oldalléptetést és a három oldal tartalmát egy-egy mondatban, az `oled` config csoportot és azt, hogy csak a `dev` buildben működik.

- [ ] **Step 7: Build és tesztek**

Run: `pio test -e native && pio run -e dev && pio run -e prod -v`
Expected: minden teszt PASS, mindkét build sikeres, és a prod build kimenetében nem szerepel az `U8g2` (a dependency graphban sincs).

- [ ] **Step 8: Kézi ellenőrzés a kártyán** (`pio run -e dev -t upload`, `pio device monitor`)

- Kijelző nélkül indítva: egy `[oled] no display…` sor jelenik meg, a hang megy.
- Kijelzővel: az 1. oldal a vázlat szerint néz ki. A BOOT 1 → 2 → 3 → 1 sorrendben lépteti az oldalakat, és egy nyomás egy lépés.
- Zene közben a spektrum mozog, a fejlécben `44.1kHz ST` áll. A hang ropogásmentes, egy 5 perces hallgatás alatt sincs akadás.
- WiFi-router kikapcsolva: `NO WIFI` és `WS:--`. Szerver leállítva: `WS:..`, majd `WS:E<n>`.

- [ ] **Step 9: Commit**

```bash
git add src/display/DebugDisplay.h src/display/DebugDisplay.cpp src/main.cpp platformio.ini README.md
git commit -m "feat: add SH1106 OLED debug display with three pages"
```
