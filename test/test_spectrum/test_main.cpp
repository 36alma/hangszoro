#include <unity.h>

#include <math.h>
#include <string.h>

#include "display/Format.h"
#include "display/Spectrum.h"

void setUp() {}
void tearDown() {}

static Spectrum sp;  // nagy, nem a veremre
static int16_t buf[Spectrum::kSize];

static void sine(float hz, uint32_t rate, float amp) {
  for (size_t i = 0; i < Spectrum::kSize; i++)
    buf[i] = (int16_t)(amp * 32767.0f * sinf(2.0f * 3.14159265f * hz * (float)i / (float)rate));
}

static size_t bandOf(float hz) {
  size_t b = 0;
  while (b + 1 < Spectrum::kBands && Spectrum::bandEdgeHz(b + 1) <= hz) b++;
  return b;
}

void test_band_edges() {
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.0f, Spectrum::bandEdgeHz(0));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 16000.0f, Spectrum::bandEdgeHz(Spectrum::kBands));
  for (size_t i = 0; i < Spectrum::kBands; i++)
    TEST_ASSERT_TRUE(Spectrum::bandEdgeHz(i) < Spectrum::bandEdgeHz(i + 1));
}

void test_full_scale_sine_near_0_db() {
  float db[Spectrum::kBands];
  sine(1000.0f, 44100, 1.0f);
  sp.compute(buf, 44100, db);
  TEST_ASSERT_FLOAT_WITHIN(1.5f, -0.75f, db[bandOf(1000.0f)]);  // -2.25 .. +0.75 dB
  TEST_ASSERT_TRUE(db[0] < -40.0f);
}

void test_quieter_sine_lower() {
  float db[Spectrum::kBands];
  sine(3000.0f, 44100, 0.1f);
  sp.compute(buf, 44100, db);
  TEST_ASSERT_FLOAT_WITHIN(2.0f, -20.0f, db[bandOf(3000.0f)]);
}

void test_low_band_without_bin_uses_nearest() {
  float db[Spectrum::kBands];
  sine(113.0f, 44100, 1.0f);  // a 100-127 Hz-es sávba nem esik bin-közép
  sp.compute(buf, 44100, db);
  TEST_ASSERT_TRUE(db[0] > -10.0f);
}

void test_silence_all_low() {
  float db[Spectrum::kBands];
  memset(buf, 0, sizeof(buf));
  sp.compute(buf, 44100, db);
  for (size_t i = 0; i < Spectrum::kBands; i++) TEST_ASSERT_TRUE(db[i] <= -90.0f);
}

void test_bands_above_nyquist_empty() {
  float db[Spectrum::kBands];
  sine(1000.0f, 16000, 1.0f);  // Nyquist 8 kHz
  sp.compute(buf, 16000, db);
  TEST_ASSERT_EQUAL_FLOAT(-99.0f, db[Spectrum::kBands - 1]);
}

// A 2. oldal tengelyfelirata: minden jelölő karakter a saját frekvenciáját tartalmazó sáv alatt áll.
void test_axis_labels_under_their_bands() {
  TEST_ASSERT_EQUAL_UINT(Spectrum::kBands, strlen(disp::kAxisLabels));
  TEST_ASSERT_EQUAL_CHAR('.', disp::kAxisLabels[bandOf(100.0f)]);
  TEST_ASSERT_EQUAL_CHAR('.', disp::kAxisLabels[bandOf(300.0f)]);
  TEST_ASSERT_EQUAL_CHAR('3', disp::kAxisLabels[bandOf(300.0f) + 1]);
  TEST_ASSERT_EQUAL_CHAR('1', disp::kAxisLabels[bandOf(1000.0f)]);
  TEST_ASSERT_EQUAL_CHAR('3', disp::kAxisLabels[bandOf(3000.0f)]);
  TEST_ASSERT_EQUAL_CHAR('8', disp::kAxisLabels[bandOf(8000.0f)]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_band_edges);
  RUN_TEST(test_full_scale_sine_near_0_db);
  RUN_TEST(test_quieter_sine_lower);
  RUN_TEST(test_low_band_without_bin_uses_nearest);
  RUN_TEST(test_silence_all_low);
  RUN_TEST(test_bands_above_nyquist_empty);
  RUN_TEST(test_axis_labels_under_their_bands);
  return UNITY_END();
}
