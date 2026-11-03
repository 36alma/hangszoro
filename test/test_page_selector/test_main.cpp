#include <unity.h>

#include "display/PageSelector.h"

void setUp() {}
void tearDown() {}

void test_starts_on_first_page() {
  PageSelector p;
  TEST_ASSERT_EQUAL_UINT8(0, p.page());
}

void test_stable_press_advances_once() {
  PageSelector p;
  TEST_ASSERT_FALSE(p.update(true, 0));
  TEST_ASSERT_FALSE(p.update(true, 29));
  TEST_ASSERT_TRUE(p.update(true, 30));
  TEST_ASSERT_EQUAL_UINT8(1, p.page());
  TEST_ASSERT_FALSE(p.update(true, 2000));  // nyomva tartás: nincs ismétlés
  TEST_ASSERT_EQUAL_UINT8(1, p.page());
}

void test_bounce_ignored() {
  PageSelector p;
  p.update(true, 0);
  p.update(false, 10);
  p.update(true, 20);
  p.update(false, 25);
  TEST_ASSERT_FALSE(p.update(false, 100));
  TEST_ASSERT_EQUAL_UINT8(0, p.page());
}

void test_short_release_does_not_retrigger() {
  PageSelector p;
  p.update(true, 0);
  p.update(true, 30);  // 1. oldal
  p.update(false, 40);
  TEST_ASSERT_FALSE(p.update(true, 50));  // 10 ms-os elengedés pergés
  TEST_ASSERT_FALSE(p.update(true, 200));
  TEST_ASSERT_EQUAL_UINT8(1, p.page());
}

void test_wraps_after_third_page() {
  PageSelector p;
  uint32_t t = 0;
  for (int i = 0; i < 3; i++) {
    p.update(true, t);
    p.update(true, t + 30);
    p.update(false, t + 40);
    p.update(false, t + 80);
    t += 100;
  }
  TEST_ASSERT_EQUAL_UINT8(0, p.page());
}

void test_millis_wraparound() {
  PageSelector p;
  p.update(true, 0xFFFFFFF0u);
  TEST_ASSERT_TRUE(p.update(true, 0x0000000Fu));  // 31 ms telt el
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_on_first_page);
  RUN_TEST(test_stable_press_advances_once);
  RUN_TEST(test_bounce_ignored);
  RUN_TEST(test_short_release_does_not_retrigger);
  RUN_TEST(test_wraps_after_third_page);
  RUN_TEST(test_millis_wraparound);
  return UNITY_END();
}
