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
