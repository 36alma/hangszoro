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
