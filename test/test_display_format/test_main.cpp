#include <unity.h>

#include <cstring>

#include "display/Format.h"

void setUp() {}
void tearDown() {}

// A jóváhagyott vázlat értékei.
static disp::Snapshot mock() {
  disp::Snapshot s;
  s.deviceName = "Hangszoro";
  s.wifiUp = true;
  s.rssi = -62;
  s.ip = 0xC0A80132u;
  s.wsConnected = true;
  s.bitrateKbps = 128;
  s.sampleRate = 44100;
  s.channels = 2;
  s.bufAvailable = 78;
  s.bufCapacity = 100;
  s.state = SignalMeter::State::Signal;
  s.lost = 3;
  s.lastRxAgeMs = 200;
  s.freeHeap = 182 * 1024;
  s.uptimeMs = (1 * 3600 + 23 * 60 + 5) * 1000ULL;
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
  s.wifiUp = false;
  TEST_ASSERT_EQUAL_STRING("192.168.1.50    WS:--", disp::overviewText(s)[1].c_str());
  TEST_ASSERT_EQUAL_STRING("Hangszoro     NO WIFI", disp::overviewText(s)[0].c_str());
  TEST_ASSERT_EQUAL_STRING("WiFi --", disp::detailsText(s)[1].c_str());
  s.wifiUp = true;
  s.wsConnected = false;
  TEST_ASSERT_EQUAL_STRING("192.168.1.50    WS:..", disp::overviewText(s)[1].c_str());
  s.wsFailures = 3;
  TEST_ASSERT_EQUAL_STRING("192.168.1.50    WS:E3", disp::overviewText(s)[1].c_str());
  TEST_ASSERT_EQUAL_STRING("utolso csomag: -", disp::detailsText(s)[4].c_str());
}

void test_last_packet_age_over_10s() {
  auto s = mock();
  s.lastRxAgeMs = 12400;
  TEST_ASSERT_EQUAL_STRING("utolso csomag: 12s", disp::detailsText(s)[4].c_str());
}

void test_overview_long_accented_name() {
  auto s = mock();
  s.deviceName = "Hangsz\xC3\xB3r\xC3\xB3 Nappali \xC5\x91sz";  // "Hangszóró Nappali ősz"
  TEST_ASSERT_EQUAL_STRING("Hangszoro Napp -62dBm", disp::overviewText(s)[0].c_str());
  TEST_ASSERT_EQUAL_STRING("a?b", disp::asciiFold("a\xE2\x82\xAC" "b").c_str());  // € → ?
  TEST_ASSERT_EQUAL_STRING("AEIOOOUUU", disp::asciiFold("\xC3\x81\xC3\x89\xC3\x8D\xC3\x93\xC3\x96\xC5\x90"
                                                        "\xC3\x9A\xC3\x9C\xC5\xB0").c_str());
}

void test_sound_header_no_data() {
  auto s = mock();
  TEST_ASSERT_EQUAL_STRING("HANG       44.1kHz ST", disp::soundHeader(s).c_str());
  s.channels = 1;
  s.sampleRate = 22050;
  TEST_ASSERT_EQUAL_STRING("HANG       22.1kHz MO", disp::soundHeader(s).c_str());
  s.state = SignalMeter::State::NoData;
  TEST_ASSERT_EQUAL_STRING("HANG           -- kHz", disp::soundHeader(s).c_str());
}

void test_no_line_exceeds_21() {
  auto s = mock();
  s.deviceName = std::string(64, 'X');
  s.lost = 123456;
  s.dropped = 4000000000u;
  s.malformed = 99999;
  s.uptimeMs = 150ULL * 3600 * 1000;
  s.bufAvailable = 100;
  s.bitrateKbps = 320;
  s.wsConnected = false;
  s.wsFailures = 12345;
  s.state = SignalMeter::State::NoData;
  for (auto& l : disp::overviewText(s)) TEST_ASSERT_TRUE(l.size() <= 21);
  for (auto& l : disp::detailsText(s)) TEST_ASSERT_TRUE(l.size() <= 21);
  TEST_ASSERT_TRUE(disp::soundHeader(s).size() <= 21);
  TEST_ASSERT_EQUAL_STRING("320k 44.1k ST buf 99%", disp::overviewText(s)[2].c_str());
  // Az uptime nem fér ki, elmarad.
  TEST_ASSERT_EQUAL_STRING("NO DATA  lost:123k", disp::overviewText(s)[3].c_str());
  // Szám sosem vágódik félbe: a teljes sorok.
  TEST_ASSERT_EQUAL_STRING("192.168.1.50  WS:E12k", disp::overviewText(s)[1].c_str());
  TEST_ASSERT_EQUAL_STRING("D:4G B:99k L:123k", disp::detailsText(s)[3].c_str());
  TEST_ASSERT_EQUAL_STRING("heap 182k    up 6d06h", disp::detailsText(s)[5].c_str());
}

void test_long_ip_keeps_every_digit() {
  auto s = mock();
  s.ip = 0xC0A864C8u;  // 192.168.100.200, a leghosszabb IP
  s.wsConnected = false;
  s.wsFailures = 10;
  TEST_ASSERT_EQUAL_STRING("192.168.100.200   E10", disp::overviewText(s)[1].c_str());
  s.wsConnected = true;
  TEST_ASSERT_EQUAL_STRING("192.168.100.200 WS:OK", disp::overviewText(s)[1].c_str());
  s.ip = 0xC0A8010Au;  // 192.168.1.10
  s.wsConnected = false;
  s.wsFailures = 1000;
  TEST_ASSERT_EQUAL_STRING("192.168.1.10   WS:E1k", disp::overviewText(s)[1].c_str());
}

void test_counters_never_cut() {
  auto s = mock();
  s.dropped = 12345;
  s.lost = 1500;
  TEST_ASSERT_EQUAL_STRING("D:12k B:0 L:1k", disp::detailsText(s)[3].c_str());
}

void test_long_uptime_and_age_fit() {
  auto s = mock();
  s.uptimeMs = (99ULL * 3600 + 59 * 60 + 59) * 1000;
  TEST_ASSERT_EQUAL_STRING("heap 182k up 99:59:59", disp::detailsText(s)[5].c_str());
  s.uptimeMs = 1000ULL * 3600 * 1000;
  TEST_ASSERT_EQUAL_STRING("heap 182k   up 41d16h", disp::detailsText(s)[5].c_str());
  s.lastRxAgeMs = 4000000;  // 4000 s
  TEST_ASSERT_EQUAL_STRING("utolso csomag: 66m", disp::detailsText(s)[4].c_str());
}

void test_uptime_short_forms() {
  auto s = mock();
  s.uptimeMs = 7 * 60 * 1000ULL;
  TEST_ASSERT_EQUAL_STRING("SIGNAL  lost:3  0:07h", disp::overviewText(s)[3].c_str());
  s.uptimeMs = 150ULL * 3600 * 1000;
  TEST_ASSERT_EQUAL_STRING("SIGNAL  lost:3   150h", disp::overviewText(s)[3].c_str());
}

void test_helpers() {
  TEST_ASSERT_EQUAL_STRING("44.1", disp::khzText(44100).c_str());
  TEST_ASSERT_EQUAL_STRING("48", disp::khzText(48000).c_str());
  TEST_ASSERT_EQUAL_STRING("22.1", disp::khzText(22050).c_str());
  TEST_ASSERT_EQUAL_STRING("--", disp::khzText(0).c_str());
  TEST_ASSERT_EQUAL_STRING("999", disp::compactCount(999).c_str());
  TEST_ASSERT_EQUAL_STRING("1k", disp::compactCount(1000).c_str());
  TEST_ASSERT_EQUAL_STRING("12k", disp::compactCount(12345).c_str());
  TEST_ASSERT_EQUAL_STRING("2M", disp::compactCount(2500000).c_str());
  TEST_ASSERT_EQUAL_STRING("4G", disp::compactCount(4000000000u).c_str());
  TEST_ASSERT_EQUAL_INT(84, disp::dbToPixels(0.0f, 84));
  TEST_ASSERT_EQUAL_INT(0, disp::dbToPixels(-75.0f, 84));
  TEST_ASSERT_EQUAL_INT(42, disp::dbToPixels(-30.0f, 84));
  TEST_ASSERT_EQUAL_INT(84, disp::dbToPixels(3.0f, 84));
  TEST_ASSERT_EQUAL_STRING("-12", disp::levelText(-12.4f).c_str());
  TEST_ASSERT_EQUAL_STRING("-60", disp::levelText(-99.0f).c_str());
  TEST_ASSERT_EQUAL_STRING("0", disp::levelText(-0.2f).c_str());
  TEST_ASSERT_EQUAL_UINT(21, strlen(disp::kAxisLabels));
}

void test_buffer_percent_zero_capacity() {
  auto s = mock();
  s.bufCapacity = 0;
  TEST_ASSERT_EQUAL_STRING("128k 44.1k ST  buf 0%", disp::overviewText(s)[2].c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_overview_matches_mock);
  RUN_TEST(test_details_matches_mock);
  RUN_TEST(test_ws_labels);
  RUN_TEST(test_last_packet_age_over_10s);
  RUN_TEST(test_overview_long_accented_name);
  RUN_TEST(test_sound_header_no_data);
  RUN_TEST(test_no_line_exceeds_21);
  RUN_TEST(test_uptime_short_forms);
  RUN_TEST(test_helpers);
  RUN_TEST(test_buffer_percent_zero_capacity);
  RUN_TEST(test_long_ip_keeps_every_digit);
  RUN_TEST(test_counters_never_cut);
  RUN_TEST(test_long_uptime_and_age_fit);
  return UNITY_END();
}
