#include <unity.h>
#include "services/SignalMeter.h"

void setUp() {}
void tearDown() {}

void test_no_data() {
  SignalMeter m;
  SignalMeter::Report r = m.take();
  TEST_ASSERT_EQUAL_UINT32(0, r.frames);
  TEST_ASSERT_TRUE(r.state == SignalMeter::State::NoData);
}

void test_mono_counts_for_both_channels() {
  SignalMeter m;
  const int16_t pcm[] = {100, -2000, 500};
  m.add(pcm, 3, 1);
  SignalMeter::Report r = m.take();
  TEST_ASSERT_EQUAL_UINT32(3, r.frames);
  TEST_ASSERT_EQUAL_INT32(2000, r.peakL);
  TEST_ASSERT_EQUAL_INT32(2000, r.peakR);
  TEST_ASSERT_TRUE(r.state == SignalMeter::State::Signal);
}

void test_stereo_left_only() {
  SignalMeter m;
  const int16_t pcm[] = {3000, 0, -4000, 1, 10, 0};  // L,R párok
  m.add(pcm, 3, 2);
  SignalMeter::Report r = m.take();
  TEST_ASSERT_EQUAL_UINT32(3, r.frames);
  TEST_ASSERT_EQUAL_INT32(4000, r.peakL);
  TEST_ASSERT_EQUAL_INT32(1, r.peakR);
  TEST_ASSERT_TRUE(r.state == SignalMeter::State::Signal);
}

void test_quiet_is_silence() {
  SignalMeter m;
  const int16_t pcm[] = {5, -5, 20, -SignalMeter::kSilencePeak};
  m.add(pcm, 2, 2);
  SignalMeter::Report r = m.take();
  TEST_ASSERT_EQUAL_UINT32(2, r.frames);
  TEST_ASSERT_TRUE(r.state == SignalMeter::State::Silence);
}

void test_take_resets() {
  SignalMeter m;
  const int16_t pcm[] = {8000, 8000};
  m.add(pcm, 1, 2);
  m.take();
  SignalMeter::Report r = m.take();
  TEST_ASSERT_EQUAL_UINT32(0, r.frames);
  TEST_ASSERT_EQUAL_INT32(0, r.peakL);
  TEST_ASSERT_EQUAL_INT32(0, r.peakR);
  TEST_ASSERT_TRUE(r.state == SignalMeter::State::NoData);
}

void test_accumulates_across_calls() {
  SignalMeter m;
  const int16_t a[] = {100, 9000};
  const int16_t b[] = {-7000, 50};
  m.add(a, 1, 2);
  m.add(b, 1, 2);
  SignalMeter::Report r = m.take();
  TEST_ASSERT_EQUAL_UINT32(2, r.frames);
  TEST_ASSERT_EQUAL_INT32(7000, r.peakL);
  TEST_ASSERT_EQUAL_INT32(9000, r.peakR);
}

void test_full_scale_negative() {
  SignalMeter m;
  const int16_t pcm[] = {-32768};
  m.add(pcm, 1, 1);
  TEST_ASSERT_EQUAL_INT32(32768, m.take().peakL);
}

void test_ignores_bad_input() {
  SignalMeter m;
  const int16_t pcm[] = {1000, 1000, 1000};
  m.add(nullptr, 3, 1);
  m.add(pcm, 1, 3);
  m.add(pcm, 1, 0);
  TEST_ASSERT_TRUE(m.take().state == SignalMeter::State::NoData);
}

void test_dbfs() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, SignalMeter::toDbfs(32768));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, -20.0f, SignalMeter::toDbfs(3277));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -99.0f, SignalMeter::toDbfs(0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_data);
  RUN_TEST(test_mono_counts_for_both_channels);
  RUN_TEST(test_stereo_left_only);
  RUN_TEST(test_quiet_is_silence);
  RUN_TEST(test_take_resets);
  RUN_TEST(test_accumulates_across_calls);
  RUN_TEST(test_full_scale_negative);
  RUN_TEST(test_ignores_bad_input);
  RUN_TEST(test_dbfs);
  return UNITY_END();
}
