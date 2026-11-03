#include <unity.h>

#include "display/AudioTap.h"

void setUp() {}
void tearDown() {}

void test_mono_duplicated() {
  AudioTap t(16);
  const int16_t pcm[] = {1, 2, 3};
  int16_t out[16];
  t.push(pcm, 3, 1, 22050);
  TEST_ASSERT_EQUAL_UINT(3, t.pull(out, 8));
  const int16_t want[] = {1, 1, 2, 2, 3, 3};
  TEST_ASSERT_EQUAL_INT16_ARRAY(want, out, 6);
  TEST_ASSERT_EQUAL_UINT32(22050, t.sampleRate());
  TEST_ASSERT_EQUAL_UINT8(1, t.channels());
}

void test_stereo_passthrough_and_partial_pull() {
  AudioTap t(16);
  const int16_t pcm[] = {1, -1, 2, -2, 3, -3};
  int16_t out[16];
  t.push(pcm, 3, 2, 44100);
  TEST_ASSERT_EQUAL_UINT(2, t.pull(out, 2));
  TEST_ASSERT_EQUAL_INT16(2, out[2]);
  TEST_ASSERT_EQUAL_INT16(-2, out[3]);
  TEST_ASSERT_EQUAL_UINT(1, t.pull(out, 8));
  TEST_ASSERT_EQUAL_INT16(3, out[0]);
  TEST_ASSERT_EQUAL_INT16(-3, out[1]);
  TEST_ASSERT_EQUAL_UINT8(2, t.channels());
}

void test_overflow_keeps_whole_frames() {
  AudioTap t(4);
  const int16_t pcm[] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6};
  int16_t out[16];
  t.push(pcm, 6, 2, 44100);
  TEST_ASSERT_EQUAL_UINT(4, t.pull(out, 8));
  TEST_ASSERT_EQUAL_INT16(4, out[6]);
  TEST_ASSERT_EQUAL_INT16(4, out[7]);
  t.push(pcm, 1, 2, 44100);
  TEST_ASSERT_EQUAL_UINT(1, t.pull(out, 8));
  TEST_ASSERT_EQUAL_INT16(1, out[0]);
}

void test_large_mono_push_in_chunks() {
  AudioTap t(1000);
  static int16_t pcm[700];
  for (int i = 0; i < 700; i++) pcm[i] = (int16_t)i;
  static int16_t out[2000];
  t.push(pcm, 700, 1, 44100);  // több, mint egy 256 keretes darab
  TEST_ASSERT_EQUAL_UINT(700, t.pull(out, 1000));
  TEST_ASSERT_EQUAL_INT16(699, out[2 * 699]);
  TEST_ASSERT_EQUAL_INT16(699, out[2 * 699 + 1]);
  TEST_ASSERT_EQUAL_INT16(300, out[600]);
}

void test_bad_input_ignored() {
  AudioTap t(4);
  int16_t out[8];
  const int16_t pcm[] = {1, 2, 3};
  t.push(nullptr, 3, 2, 44100);
  t.push(pcm, 1, 3, 44100);
  t.push(pcm, 1, 0, 44100);
  TEST_ASSERT_EQUAL_UINT(0, t.pull(out, 4));
  TEST_ASSERT_EQUAL_UINT32(0, t.sampleRate());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mono_duplicated);
  RUN_TEST(test_stereo_passthrough_and_partial_pull);
  RUN_TEST(test_overflow_keeps_whole_frames);
  RUN_TEST(test_large_mono_push_in_chunks);
  RUN_TEST(test_bad_input_ignored);
  return UNITY_END();
}
