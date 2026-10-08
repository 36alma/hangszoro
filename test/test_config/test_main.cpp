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
  auto w = applyJson(c, "{\"endpoint\":{\"token\":\"abc\r\nX-Evil: 1\"}}");
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
  auto w = applyJson(c, "{\"endpoint\":{\"token\":\"SECRET\r\nTOKEN\"}}");
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
