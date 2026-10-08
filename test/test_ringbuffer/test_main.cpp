#include <unity.h>
#include <string.h>
#include "services/RingBuffer.h"

void setUp() {}
void tearDown() {}

void test_roundtrip() {
  RingBuffer rb(8);
  const uint8_t in[] = {1, 2, 3, 4};
  uint8_t out[8] = {0};
  TEST_ASSERT_EQUAL_size_t(4, rb.write(in, 4));
  TEST_ASSERT_EQUAL_size_t(4, rb.available());
  TEST_ASSERT_EQUAL_size_t(4, rb.read(out, 8));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, 4);
  TEST_ASSERT_EQUAL_size_t(0, rb.available());
}

void test_read_from_empty_returns_zero() {
  RingBuffer rb(4);
  uint8_t out[4];
  TEST_ASSERT_EQUAL_size_t(0, rb.read(out, 4));
}

void test_overflow_writes_only_free_space() {
  RingBuffer rb(4);
  const uint8_t in[] = {1, 2, 3, 4, 5, 6};
  TEST_ASSERT_EQUAL_size_t(4, rb.write(in, 6));
  TEST_ASSERT_EQUAL_size_t(0, rb.write(in, 1));
  TEST_ASSERT_EQUAL_size_t(0, rb.freeSpace());
  uint8_t out[4];
  TEST_ASSERT_EQUAL_size_t(4, rb.read(out, 4));
  const uint8_t expect[] = {1, 2, 3, 4};  // a régi adat marad, az új eldobódik
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 4);
}

void test_wraparound_preserves_order() {
  RingBuffer rb(5);
  const uint8_t a[] = {1, 2, 3, 4};
  uint8_t out[8];
  rb.write(a, 4);
  rb.read(out, 3);  // olvasó a 3. indexen
  const uint8_t b[] = {5, 6, 7, 8};
  TEST_ASSERT_EQUAL_size_t(4, rb.write(b, 4));  // az író körbefordul
  TEST_ASSERT_EQUAL_size_t(5, rb.read(out, 8));
  const uint8_t expect[] = {4, 5, 6, 7, 8};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, out, 5);
}

void test_many_cycles_stay_consistent() {
  RingBuffer rb(7);
  uint8_t counter = 0, expected = 0;
  for (int i = 0; i < 1000; i++) {
    uint8_t chunk[5];
    for (int j = 0; j < 5; j++) chunk[j] = counter + j;
    size_t w = rb.write(chunk, 5);
    counter += (uint8_t)w;
    uint8_t out[4];
    size_t r = rb.read(out, 4);
    for (size_t j = 0; j < r; j++) TEST_ASSERT_EQUAL_UINT8(expected++, out[j]);
  }
}

void test_zero_capacity_is_clamped() {
  RingBuffer rb(0);
  TEST_ASSERT_EQUAL_size_t(1, rb.capacity());
}

void test_null_and_zero_length_are_safe() {
  RingBuffer rb(4);
  TEST_ASSERT_EQUAL_size_t(0, rb.write(nullptr, 3));
  TEST_ASSERT_EQUAL_size_t(0, rb.write((const uint8_t*)"a", 0));
  uint8_t out[1];
  TEST_ASSERT_EQUAL_size_t(0, rb.read(nullptr, 3));
  TEST_ASSERT_EQUAL_size_t(0, rb.read(out, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip);
  RUN_TEST(test_read_from_empty_returns_zero);
  RUN_TEST(test_overflow_writes_only_free_space);
  RUN_TEST(test_wraparound_preserves_order);
  RUN_TEST(test_many_cycles_stay_consistent);
  RUN_TEST(test_zero_capacity_is_clamped);
  RUN_TEST(test_null_and_zero_length_are_safe);
  return UNITY_END();
}
