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
