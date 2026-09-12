#include "dsp/CrunchyAmp.h"
#include "dsp/PeakMeterState.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

using namespace crunchy;
void Check(bool passed, const char* message) {
  if (!passed) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
std::vector<double> Render(Parameters p, double rate, int block, bool stereo = false) {
  Amp amp; amp.Prepare(rate, p);
  constexpr int length = 1800;
  std::vector<double> left(length), right(length), out(length), outR(length);
  for (int i = 0; i < length; ++i) {
    left[i] = 0.08 * std::sin(2 * kPi * 110 * i / rate)
              + 0.03 * std::sin(2 * kPi * 997 * i / rate);
    right[i] = left[i];
  }
  for (int i = 0; i < length; i += block) {
    double* inputs[] = {left.data() + i, right.data() + i};
    double* outputs[] = {out.data() + i, outR.data() + i};
    amp.Process(inputs, outputs, std::min(block, length - i), stereo ? 2 : 1, 2);
  }
  double energy = 0;
  for (int i = 0; i < length; ++i) {
    Check(std::isfinite(out[i]), "finite output");
    Check(std::abs(out[i]) < 8, "bounded nominal output");
    Check(std::abs(out[i] - outR[i]) < 1e-12, "matched stereo / mono duplication");
    energy += out[i] * out[i];
  }
  Check(energy > 1e-8, "non-silent nominal output");
  return out;
}

double RenderToneRms(Parameters p, double frequency, double level = 0.04, double rate = 48000.0) {
  AmpChannel amp;
  amp.Prepare(rate, p);
  constexpr int warmup = 2400;
  constexpr int measured = 2400;
  double sum = 0.0;
  for (int i = 0; i < warmup + measured; ++i) {
    const double x = level * std::sin(2.0 * kPi * frequency * static_cast<double>(i) / rate);
    const double y = amp.Process(x);
    Check(std::isfinite(y), "audibility regression output remains finite");
    if (i >= warmup) sum += y * y;
  }
  return std::sqrt(sum / measured);
}

int main() {
  {
    // Host-format-neutral meter bridge: peaks accumulate across arbitrary host
    // blocks until the GUI consumes them, and clip is sticky for the interval.
    PeakMeterState meter;
    meter.Accumulate(0.10f, false);
    meter.Accumulate(0.4764691591f, false); // reference DI peak
    meter.Accumulate(0.25f, false);
    const auto referenceDi = meter.Consume();
    Check(std::abs(referenceDi.peak - 0.4764691591f) < 1e-7f,
          "meter accumulator must keep the highest sample peak across host blocks");
    Check(!referenceDi.clipped, "reference DI must not set the clip latch");
    const auto cleared = meter.Consume();
    Check(cleared.peak == 0.f && !cleared.clipped,
          "meter consume must clear the interval without stale UI data");
    meter.Accumulate(0.7f, false);
    meter.Accumulate(1.0f, true);
    const auto clipped = meter.Consume();
    Check(clipped.peak >= 1.0f && clipped.clipped,
          "0 dBFS sample must survive to UI as both peak and clip event");
  }
  {
    const auto defaults = Defaults();
    for (int i = Volume; i <= LeadMaster; ++i)
      Check(std::abs(defaults[i] - 5.0) < 1e-12,
            "all seven main amplifier knobs must default/reset to 5.00");

    Check(defaults[ReverbOn] == 0.0,
          "Reverb footswitch must start OFF in the shared VST3/Standalone defaults");
    Check(defaults[Bypass] == 0.0,
          "shared/VST3 bypass default must remain OFF");

    const auto vst3Defaults = StartupDefaults(false);
    const auto appDefaults = StartupDefaults(true);
    Check(vst3Defaults[Bypass] == 0.0,
          "VST3 must start with BYPASS OFF");
    Check(appDefaults[Bypass] == 1.0,
          "Standalone must start with BYPASS ON");
    for (int i = 0; i < NumParams; ++i) {
      if (i == Bypass) continue;
      Check(std::abs(vst3Defaults[i] - appDefaults[i]) < 1e-12,
            "Standalone and VST3 startup defaults may differ only in Bypass");
    }
  }
  {
    const auto defaults = Defaults();
    const double expectedEq[5] = {6.0, 0.0, -6.0, 0.0, 6.0};
    for (int i = 0; i < 5; ++i)
      Check(std::abs(defaults[Eq80 + i] - expectedEq[i]) < 1e-12,
            "fresh-instance EQ defaults use +6/0/-6/0/+6 dB contour");
  }
  {
    const auto defaults = Defaults();
    Check(std::abs(defaults[InputCalibration]) < 1e-12, "Input default must be 0 dB");
    Check(std::abs(defaults[OutputGain]) < 1e-12, "Output default must be 0 dB");
    auto extremes = defaults;
    extremes[InputCalibration] = -100.0;
    extremes[OutputGain] = 100.0;
    extremes = Sanitize(extremes);
    Check(std::abs(extremes[InputCalibration] + 24.0) < 1e-12, "Input minimum must clamp to -24 dB");
    Check(std::abs(extremes[OutputGain] - 24.0) < 1e-12, "Output maximum must clamp to +24 dB");
  }
  {
    // 0 dB on the new trims must preserve the established pre-0.10.6 amp
    // operating point, not raw DSP unity.  +/-6 dB must remain a conventional
    // gain trim around those references.
    const double oldInputNominal = 0.775 * std::sqrt(2.0) * Db(9.0);
    const double oldOutputNominal = 0.5 * Db(-6.0);
    Check(std::abs(kInputDisplayZeroOffsetDb - 12.0) < 1e-12,
          "Input visual zero must carry a hidden +12 dB trim offset");
    Check(std::abs(kOutputDisplayZeroOffsetDb + 2.5) < 1e-12,
          "Output visual zero must carry a hidden -2.5 dB trim offset");
    Check(std::abs(InputTrimScale(0.0) - oldInputNominal * Db(12.0)) < 1e-12,
          "Input visual 0 dB must sound exactly like 0.10.8 at +12 dB");
    Check(std::abs(OutputTrimScale(0.0) - oldOutputNominal * Db(-2.5)) < 1e-12,
          "Output visual 0 dB must sound exactly like 0.10.8 at -2.5 dB");
    Check(std::abs(InputTrimScale(6.0) / InputTrimScale(0.0) - Db(6.0)) < 1e-12,
          "Input +6 dB must remain a conventional +6 dB trim around the new zero");
    Check(std::abs(OutputTrimScale(-6.0) / OutputTrimScale(0.0) - Db(-6.0)) < 1e-12,
          "Output -6 dB must remain a conventional -6 dB trim around the new zero");
    Check(!IsDigitalSampleClipped(0.999999), "Below 0 dBFS must not flag clipping");
    Check(IsDigitalSampleClipped(1.0), "+1.0 sample must flag clipping at 0 dBFS");
    Check(IsDigitalSampleClipped(-1.0), "-1.0 sample must flag clipping at 0 dBFS");

    // 0.10.11 corrected source reference: the supplied 24-bit DI peaks at
    // 3,996,913 / 8,388,608 = 0.47646915912628174 = -6.439304102 dBFS.
    // This is a meter reference only; it must never trigger digital clipping.
    constexpr double referenceDiPeak = 3996913.0 / 8388608.0;
    constexpr double referenceDiPeakDbfs = -6.4393041021814;
    Check(!IsDigitalSampleClipped(referenceDiPeak),
          "-6.4393 dBFS reference must not flag input clipping");
    Check(std::abs(20.0 * std::log10(referenceDiPeak) - referenceDiPeakDbfs) < 1.0e-9,
          "reference peak must decode to -6.439304102 dBFS");
  }
  {
    const auto defaults = Defaults();
    Check(defaults[Oversampling] == 0.0, "Oversampling must start at 1x");
    for (int mode = 0; mode < 4; ++mode) {
      auto p = defaults; p[Oversampling] = static_cast<double>(mode);
      AmpChannel channel; channel.Prepare(48000.0, p);
      Check(channel.CurrentOversamplingFactor() == (1 << mode),
            "Oversampling enum must map to 1x/2x/4x/8x");
      for (int i = 0; i < 128; ++i)
        Check(std::isfinite(channel.Process(0.05 * std::sin(i * 0.13))),
              "Every oversampling mode must remain finite");
    }
    auto invalid = defaults; invalid[Oversampling] = 99.0;
    Check(Sanitize(invalid)[Oversampling] == 3.0, "Oversampling must clamp to 8x maximum");
    invalid[Oversampling] = -10.0;
    Check(Sanitize(invalid)[Oversampling] == 0.0, "Oversampling must clamp to 1x minimum");

    // Runtime changes must complete without rebuilding the expensive triode
    // lookup tables or producing invalid audio. The 3ms dry bridge gives the
    // rate-dependent recursive path time to restart cleanly.
    AmpChannel channel; channel.Prepare(48000.0, defaults);
    auto p8 = defaults; p8[Oversampling] = 3.0; channel.SetParameters(p8);
    for (int i = 0; i < 700; ++i)
      Check(std::isfinite(channel.Process(0.07 * std::sin(i * 0.07))),
            "Runtime oversampling transition to 8x must remain finite");
    Check(channel.CurrentOversamplingFactor() == 8, "Runtime oversampling transition must reach 8x");
    auto p1 = p8; p1[Oversampling] = 0.0; channel.SetParameters(p1);
    for (int i = 0; i < 700; ++i)
      Check(std::isfinite(channel.Process(0.07 * std::sin(i * 0.09))),
            "Runtime oversampling transition back to 1x must remain finite");
    Check(channel.CurrentOversamplingFactor() == 1, "Runtime oversampling transition must return to 1x");
  }
  for (double rate : {22050., 44100., 48000., 88200., 96000., 192000.}) {
    const auto a = Render(Defaults(), rate, 1);
    const auto b = Render(Defaults(), rate, 257, true);
    for (size_t i = 0; i < a.size(); ++i)
      Check(std::abs(a[i] - b[i]) < 1e-12, "host block size invariance");
    AmpChannel amp; amp.Prepare(rate, Defaults());
    for (int i = 0; i < 10000; ++i) Check(std::abs(amp.Process(0)) < 1e-14, "silent input stays silent");
    auto bypass = Defaults(); bypass[Bypass] = 1; amp.Prepare(rate, bypass);
    for (int i = 0; i < 1000; ++i) {
      const double x = 0.3 * std::sin(i * 0.1);
      Check(std::abs(amp.Process(x) - x) < 1e-12, "settled bypass is unity");
    }
  }
  const auto base = Render(Defaults(), 48000, 64);
  auto p = Defaults(); p[Lead] = 1;
  const auto lead = Render(p, 48000, 64);
  double difference = 0;
  for (size_t i = 0; i < base.size(); ++i) difference += std::abs(base[i] - lead[i]);
  Check(difference > 1, "Lead changes the signal");

  // Every visible panel control must affect an appropriate active signal path.
  // EqAuto/EqIn remain allocated only as legacy host-state IDs; 0.10.3 exposes
  // the single EqMode parameter instead.
  for (int param = Volume; param < NumParams; ++param) {
    if (param == EqAuto || param == EqIn) continue;
    auto before = Defaults(); before[Lead] = 1; before[EqMode] = 2; // EQ IN / ALL
    if (param == RhythmBright) before[Lead] = 0;
    if (param == ReverbOn) before[Reverb] = 6.0;
    if (param == Reverb) before[ReverbOn] = 1.0;
    if (param == EqMode) { before[EqMode] = 1; before[Eq750] = -10; } // OUT
    auto after = before;
    if (param <= LeadMaster || param == Presence || param == Reverb) after[param] = before[param] < 7 ? 9 : 2;
    else if (param <= OutputGain) after[param] += 6;
    else if (param == EqMode) after[param] = 2; // IN / ALL
    else after[param] = 1.0 - before[param];
    const auto a = Render(before, 48000, 128), b = Render(after, 48000, 128);
    double diff = 0;
    for (size_t i = 1000; i < a.size(); ++i) diff += std::abs(a[i] - b[i]);
    if (diff < 1e-6) { std::cerr << "Unresponsive parameter ID: " << param << '\n'; return 1; }
  }

  // Three-position EQ switch: AUTO follows Lead, OUT bypasses, IN / ALL is always on.
  auto autoEQ = Defaults(); autoEQ[EqMode] = 0; autoEQ[Eq750] = -8;
  auto offEQ = autoEQ; offEQ[EqMode] = 1;
  Check(Render(autoEQ, 48000, 64) == Render(offEQ, 48000, 64), "EQ AUTO / LEAD off on Rhythm");
  autoEQ[Lead] = 1;
  auto onEQ = autoEQ; onEQ[EqMode] = 2;
  Check(Render(autoEQ, 48000, 64) == Render(onEQ, 48000, 64), "EQ AUTO / LEAD on with Lead");

  auto rhythm = Defaults(); rhythm[TrebleShift] = 1;
  Check(Render(rhythm, 48000, 64) == base,
        "Treble Shift must be electrically inactive in Rhythm because LDR1 shares the Lead switching return");
  auto leadTrebleOff = Defaults(); leadTrebleOff[Lead] = 1.0; leadTrebleOff[TrebleShift] = 0.0;
  auto leadTrebleOn = leadTrebleOff; leadTrebleOn[TrebleShift] = 1.0;
  Check(Render(leadTrebleOn, 48000, 64) != Render(leadTrebleOff, 48000, 64),
        "Treble Shift must remain active in Lead");

  // 0.10.21 Pass-9 switch-audibility regression. The component values remain
  // untouched; these checks make sure the schematic plate loading is not lost
  // from the cathode-bypass *difference* again.
  auto trebleShiftOff = Defaults(); trebleShiftOff[Lead] = 1.0; trebleShiftOff[TrebleShift] = 0.0;
  auto trebleShiftOn = trebleShiftOff; trebleShiftOn[TrebleShift] = 1.0;
  Check(RenderToneRms(trebleShiftOn, 8000.0, 0.02) > RenderToneRms(trebleShiftOff, 8000.0, 0.02) * 1.05,
        "Treble Shift must retain its upper-band voicing change in Lead");

  // 0.10.25 Middle audibility regression. Pass 9 already had the correct 10K
  // pot and passive topology, but the complete high-gain model compressed the
  // 400-500 Hz level delta almost flat in Lead. Middle=5 stays the exact neutral
  // calibration point; both directions must now remain clearly audible.
  auto middleLow = Defaults(); middleLow[Lead] = 1.0; middleLow[EqMode] = 1; middleLow[Middle] = 0.0;
  auto middleMid = middleLow; middleMid[Middle] = 5.0;
  auto middleHigh = middleLow; middleHigh[Middle] = 10.0;
  const double middleLow450 = RenderToneRms(middleLow, 450.0, 0.04);
  const double middleMid450 = RenderToneRms(middleMid, 450.0, 0.04);
  const double middleHigh450 = RenderToneRms(middleHigh, 450.0, 0.04);
  Check(middleMid450 > middleLow450 * 1.20,
        "Middle 0->5 must produce a clearly audible 450 Hz rise in saturated Lead");
  Check(middleHigh450 > middleMid450 * 1.20,
        "Middle 5->10 must produce a clearly audible 450 Hz rise in saturated Lead");

  auto bassShiftOff = Defaults(); bassShiftOff[BassShift] = 0.0;
  auto bassShiftOn = bassShiftOff; bassShiftOn[BassShift] = 1.0;
  Check(RenderToneRms(bassShiftOn, 80.0, 0.02) > RenderToneRms(bassShiftOff, 80.0, 0.02) * 1.26,
        "Bass Shift plate-loaded cathode delta is too weak in the complete amplifier");

  auto deepOff = Defaults(); deepOff[Deep] = 0.0; deepOff[Master] = 5.0;
  auto deepOn = deepOff; deepOn[Deep] = 1.0;
  Check(RenderToneRms(deepOn, 80.0, 0.02) > RenderToneRms(deepOff, 80.0, 0.02) * 1.23,
        "Deep must remain clearly audible at the default Master 1 setting");

  // Lead Bright belongs to the V4A Lead branch. In Rhythm LDR2/LDR3 keep that
  // branch out of the audio path, so forcing a Rhythm effect would contradict
  // the source. In Lead, at a non-saturated Lead Drive setting it must be clear.
  // Channel-role regression: all controls dimmed in Rhythm must also be
  // electrically disconnected there, while their stored values remain intact.
  auto rhythmLeadControlsA = Defaults(); rhythmLeadControlsA[Lead] = 0.0;
  auto rhythmLeadControlsB = rhythmLeadControlsA;
  rhythmLeadControlsB[LeadDrive] = 9.0;
  rhythmLeadControlsB[LeadMaster] = 9.0;
  rhythmLeadControlsB[LeadBright] = 1.0;
  rhythmLeadControlsB[TrebleShift] = 1.0;
  Check(Render(rhythmLeadControlsA, 48000, 64) == Render(rhythmLeadControlsB, 48000, 64),
        "Lead Drive, Lead Master, Lead Bright and Treble Shift must all be inert in Rhythm");

  auto leadBrightRhythmOff = Defaults(); leadBrightRhythmOff[Lead] = 0.0; leadBrightRhythmOff[LeadBright] = 0.0;
  auto leadBrightRhythmOn = leadBrightRhythmOff; leadBrightRhythmOn[LeadBright] = 1.0;
  Check(std::abs(RenderToneRms(leadBrightRhythmOn, 4000.0, 0.002) -
                 RenderToneRms(leadBrightRhythmOff, 4000.0, 0.002)) < 1.0e-12,
        "Lead Bright must not leak into Rhythm mode");
  auto leadBrightOff = Defaults(); leadBrightOff[Lead] = 1.0; leadBrightOff[LeadDrive] = 1.0;
  auto leadBrightOn = leadBrightOff; leadBrightOn[LeadBright] = 1.0;
  Check(RenderToneRms(leadBrightOn, 8000.0, 0.002) > RenderToneRms(leadBrightOff, 8000.0, 0.002) * 2.0,
        "Lead Bright must be clearly audible in the Lead path before heavy saturation masks its level delta");

  auto bright = Defaults(); bright[Lead] = 1; bright[LeadMaster] = 10;
  auto noBright = bright; bright[LeadBright] = 1;
  Check(Render(bright, 48000, 64) != Render(noBright, 48000, 64),
        "Pass-9 V4A .22uF/22K Lead Bright cathode branch must remain active at maximum Lead Master");

  // Pass-9 additions: internal tube spring-reverb path and true Simul-Class/Class-A selection.
  auto reverbOff = Defaults(); reverbOff[Reverb] = 7.0; reverbOff[ReverbOn] = 0.0;
  auto reverbOn = reverbOff; reverbOn[ReverbOn] = 1.0;
  const auto revA = Render(reverbOff, 48000, 64), revB = Render(reverbOn, 48000, 64);
  double reverbDiff = 0.0;
  for (size_t i = 1500; i < revA.size(); ++i) reverbDiff += std::abs(revA[i] - revB[i]);
  Check(reverbDiff > 1e-4, "Reverb footswitch/mix must alter the signal when Reverb is raised");

  auto classA = Defaults(); classA[PowerMode] = 0.0;
  auto simul = classA; simul[PowerMode] = 1.0;
  Check(Render(classA, 48000, 64) != Render(simul, 48000, 64), "CLASS A and SIMUL-CLASS must be distinct power topologies");

  auto later10 = Defaults(); later10[Lead] = 1.0; later10[LaterSimul20p] = 0.0;
  auto later20 = later10; later20[LaterSimul20p] = 1.0;
  Check(Render(later10, 48000, 64) != Render(later20, 48000, 64), "10pF and later-Simul 20pF mix variants must differ");

  auto domestic = Defaults(); domestic[ExportBias] = 0.0;
  auto exportModel = domestic; exportModel[ExportBias] = 1.0;
  Check(Render(domestic, 48000, 64) != Render(exportModel, 48000, 64), "Pass-9 -67V and export -55V Simul bias variants must differ");

  // 0.10.3 audibility regressions. These thresholds intentionally validate the
  // user-facing calibration, not undocumented component values. Pass 9 does not
  // contain the exact OT/tube loop gain, parasitic capacitances or 6L6 curves.
  auto presence0 = Defaults(); presence0[Presence] = 0.0;
  auto presence10 = presence0; presence10[Presence] = 10.0;
  const double presenceDarkRms = RenderToneRms(presence0, 5000.0, 0.04);
  const double presenceBrightRms = RenderToneRms(presence10, 5000.0, 0.04);
  Check(presenceBrightRms > presenceDarkRms * 1.45,
        "Presence 0..10 must produce a clearly audible upper-band change");

  auto cap10 = Defaults(); cap10[Lead] = 1.0; cap10[LaterSimul20p] = 0.0;
  auto cap20 = cap10; cap20[LaterSimul20p] = 1.0;
  const double cap10Rms = RenderToneRms(cap10, 8000.0, 0.04);
  const double cap20Rms = RenderToneRms(cap20, 8000.0, 0.04);
  Check(cap20Rms < cap10Rms * 0.82,
        "Later-Simul 20pF mode must be clearly darker than 10pF in the upper band");

  auto bias67 = Defaults(); bias67[ExportBias] = 0.0;
  auto bias55 = bias67; bias55[ExportBias] = 1.0;
  const double bias67Rms = RenderToneRms(bias67, 997.0, 0.08);
  const double bias55Rms = RenderToneRms(bias55, 997.0, 0.08);
  Check(std::abs(bias55Rms - bias67Rms) > bias67Rms * 0.10,
        "-67V / -55V Bias Mode must produce a clearly audible level/compression change");

  Amp amp; amp.Prepare(48000);
  double left[32] {}, right[32] {};
  for (int i = 0; i < 32; ++i) left[i] = std::sin(i * 0.1) * 0.1;
  double* ins[] = {left}; double* outs[] = {left, right};
  amp.Process(ins, outs, 32, 1, 2);
  for (int i = 0; i < 32; ++i) Check(left[i] == right[i], "in-place mono-to-stereo");
  amp.Prepare(48000); amp.Process<double>(nullptr, outs, 32, 0, 2);
  for (int i = 0; i < 32; ++i) Check(left[i] == 0 && right[i] == 0, "missing input is silence");

  AmpChannel stress; stress.Prepare(48000, Defaults());
  std::mt19937 rng(1234);
  std::uniform_real_distribution<double> noise(-2.0, 2.0);
  for (int i = 0; i < 8000; ++i) {
    if (i % 67 == 0) {
      auto random = Defaults();
      for (int j = 0; j < NumParams; ++j) random[j] = noise(rng) * 20;
      stress.SetParameters(random);
    }
    const double x = i == 300 ? std::numeric_limits<double>::quiet_NaN() : noise(rng);
    Check(std::isfinite(stress.Process(x)), "automation and invalid input remain finite");
  }
  stress.Prepare(48000, Defaults());
  for (int i = 0; i < 1000; ++i) Check(stress.Process(0) == 0, "reset clears history");
  std::cout << "PASS: six sample rates, block sizes, silence, stereo, bypass, all controls, "
               "EQ routing, in-place processing, missing input, automation, invalid samples, reset.\n";
}
