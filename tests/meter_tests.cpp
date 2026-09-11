#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "dsp/PeakMeterState.h"

namespace {

[[noreturn]] void Fail(const std::string& message) {
  std::cerr << "METER VALIDATION FAILED: " << message << "\n";
  std::exit(1);
}

void Require(bool condition, const std::string& message) {
  if (!condition) Fail(message);
}

void TestExactSamplePeak() {
  double ch0[] = {0.0, 0.1, -0.47646915912628174, 0.25, -0.04};
  double ch1[] = {0.0, -0.22, 0.11, 0.03, 0.0};
  double* buffers[] = {ch0, ch1};

  const auto m = crunchy::MeasureSamplePeak(buffers, 5, 2);
  Require(std::abs(m.peak - 0.47646915912628174f) < 1.0e-7f,
          "sample peak must equal the reference DI amplitude");
  Require(!m.clipped, "-6.439304 dBFS reference must not clip");

  const double db = 20.0 * std::log10(static_cast<double>(m.peak));
  Require(std::abs(db - (-6.439304102)) < 1.0e-4,
          "reference DI must resolve to about -6.439304 dBFS");
}

void TestClipThreshold() {
  float below[] = {0.9999f, -0.9999f};
  float* belowBuffers[] = {below};
  auto m = crunchy::MeasureSamplePeak(belowBuffers, 2, 1);
  Require(!m.clipped, "sub-0 dBFS samples must not clip");

  float exact[] = {1.0f, -0.2f};
  float* exactBuffers[] = {exact};
  m = crunchy::MeasureSamplePeak(exactBuffers, 2, 1);
  Require(m.clipped, "exact full scale must set clip");

  float over[] = {-1.15f, 0.2f};
  float* overBuffers[] = {over};
  m = crunchy::MeasureSamplePeak(overBuffers, 2, 1);
  Require(m.clipped && m.peak > 1.0f,
          "floating-point over must preserve its >0 dBFS peak and set clip");
}

void TestVisibleInputTrimMetering() {
  // Reference DI at INPUT = 0.0 dB must remain the raw host peak.
  crunchy::SamplePeakMeasurement raw;
  raw.peak = 0.47646915912628174f;
  raw.clipped = false;

  auto m = crunchy::ApplySamplePeakGain(raw, 1.0);
  double db = 20.0 * std::log10(static_cast<double>(m.peak));
  Require(std::abs(db - (-6.439304102)) < 1.0e-4,
          "INPUT 0 dB must preserve the raw reference DI sample peak");
  Require(!m.clipped, "reference DI must not clip at INPUT 0 dB");

  // The meter must follow the visible INPUT trim exactly. This is what makes
  // lowering INPUT visibly lower the bar and raising it capable of lighting
  // the 0 dBFS clip cube.
  m = crunchy::ApplySamplePeakGain(raw, std::pow(10.0, -12.0 / 20.0));
  db = 20.0 * std::log10(static_cast<double>(m.peak));
  Require(std::abs(db - (-18.439304102)) < 1.0e-4,
          "INPUT -12 dB must lower the meter by exactly 12 dB");
  Require(!m.clipped, "INPUT -12 dB must not clip the reference DI");

  const double toFullScaleDb = 6.439304102;
  m = crunchy::ApplySamplePeakGain(raw, std::pow(10.0, toFullScaleDb / 20.0));
  Require(std::abs(m.peak - 1.0f) < 2.0e-5f,
          "raising INPUT by the reference headroom must reach about 0 dBFS");
  Require(m.clipped || m.peak > 0.99998f,
          "INPUT trim must be capable of reaching the clip threshold");

  m = crunchy::ApplySamplePeakGain(raw, std::pow(10.0, 12.0 / 20.0));
  Require(m.peak > 1.0f && m.clipped,
          "INPUT +12 dB must produce >0 dBFS meter peak and clip for the reference DI");
}

void TestAtomicMailbox() {
  crunchy::PeakMeterState state;
  state.Accumulate(0.10f, false);
  state.Accumulate(0.47646916f, false);
  state.Accumulate(0.25f, false);

  auto s = state.Consume();
  Require(s.hasData, "mailbox must report new audio data");
  Require(std::abs(s.peak - 0.47646916f) < 1.0e-7f,
          "mailbox must preserve the largest peak between GUI frames");
  Require(!s.clipped, "non-clipping mailbox window must stay non-clipping");

  s = state.Consume();
  Require(!s.hasData && s.peak == 0.f && !s.clipped,
          "a consumed mailbox must be empty until the audio thread publishes again");

  state.Accumulate(0.2f, false);
  state.Accumulate(1.05f, true);
  state.Accumulate(0.3f, false);
  s = state.Consume();
  Require(s.hasData && s.clipped && s.peak >= 1.05f,
          "clip must latch alongside the maximum peak until GUI consumption");

  // Silence is still data. This allows the GUI to intentionally fall to zero
  // instead of confusing a silent audio block with 'no host callback'.
  state.Accumulate(0.f, false);
  s = state.Consume();
  Require(s.hasData && s.peak == 0.f && !s.clipped,
          "silent ProcessBlock windows must explicitly publish zero peak");
}

} // namespace

int main() {
  TestExactSamplePeak();
  TestClipThreshold();
  TestVisibleInputTrimMetering();
  TestAtomicMailbox();
  std::cout << "PASS: exact sample-peak algorithm, visible Input-trim metering, 0 dBFS clip threshold, and atomic GUI mailbox\n";
  return 0;
}
