#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "dsp/CircuitFilters.h"
#include "dsp/ControlLogic.h"
#include "dsp/PassiveToneStack.h"

namespace {
constexpr double kFs = 384000.0;
constexpr double kPi = 3.14159265358979323846;

[[noreturn]] void Fail(const std::string& message)
{
  std::cerr << "CONTROL VALIDATION FAILED: " << message << "\n";
  std::exit(1);
}

void Require(bool condition, const std::string& message)
{
  if (!condition) Fail(message);
}

double ToneRms(double frequency, double volume, double trebleShift, double bright)
{
  crunchy::PassiveToneStack stack;
  stack.Prepare(kFs);
  stack.Configure(5.0, 5.0, 5.0, volume, trebleShift, bright);

  constexpr int warmup = static_cast<int>(kFs * 0.10);
  constexpr int total = static_cast<int>(kFs * 0.35);
  double sum = 0.0;
  for (int n = 0; n < total; ++n) {
    const double x = std::sin(2.0 * kPi * frequency * static_cast<double>(n) / kFs);
    const double y = stack.Process(x);
    if (n >= warmup) sum += y * y;
  }
  return std::sqrt(sum / static_cast<double>(total - warmup));
}

double CathodeRms(double frequency, double plateOhms, double cathodeOhms,
                  double fixedCap, double switchedCap, double returnOhms)
{
  crunchy::CathodeNetwork network;
  network.Reset();
  network.Configure(plateOhms, cathodeOhms, fixedCap, switchedCap, returnOhms, kFs);

  constexpr int warmup = static_cast<int>(kFs * 0.10);
  constexpr int total = static_cast<int>(kFs * 0.35);
  double sum = 0.0;
  for (int n = 0; n < total; ++n) {
    const double x = std::sin(2.0 * kPi * frequency * static_cast<double>(n) / kFs);
    const double y = network.Process(x);
    if (n >= warmup) sum += y * y;
  }
  return std::sqrt(sum / static_cast<double>(total - warmup));
}

void TestVolumeBright()
{
  const double off8k = ToneRms(8000.0, 5.0, 0.0, 0.0);
  const double on8k = ToneRms(8000.0, 5.0, 0.0, 1.0);
  Require(on8k > off8k * 2.0, "Volume 1 Pull Bright is not increasing the upper treble.");

  // Across the top of the Volume pot, so the effect should collapse at max Volume.
  const double offMax = ToneRms(8000.0, 10.0, 0.0, 0.0);
  const double onMax = ToneRms(8000.0, 10.0, 0.0, 1.0);
  Require(std::abs(onMax - offMax) < 1.0e-8,
          "Volume 1 Pull Bright should become negligible at Volume 10.");
}

void TestTrebleShift()
{
  Require(crunchy::controls::TrebleShift(1.0, 0.0) == 0.0,
          "Treble Shift must be electrically inactive in Rhythm: LDR1 shares the Lead return bus.");
  Require(crunchy::controls::TrebleShift(1.0, 1.0) == 1.0,
          "Treble Shift must engage in Lead when the TREBLE SHIFT pull is on.");

  const double off1k = ToneRms(1000.0, 5.0, 0.0, 0.0);
  const double on1k = ToneRms(1000.0, 5.0, 1.0, 0.0);
  Require(on1k > off1k * 1.5, "Treble Shift is not producing the expected high-mid lift.");
}

void TestBassShift()
{
  const double off = CathodeRms(80.0, 150000.0, 1500.0, 0.47e-6, crunchy::circuit::CathodeBypass,
                                crunchy::controls::Switched15kReturn(0.0));
  const double on = CathodeRms(80.0, 150000.0, 1500.0, 0.47e-6, crunchy::circuit::CathodeBypass,
                               crunchy::controls::Switched15kReturn(1.0));
  Require(on > off * 1.35, "Bass Shift is not increasing the low-frequency cathode bypass.");
}


void TestPlateLoadedCathodeSwitchPreservesOff()
{
  crunchy::SwitchableCathodeNetwork legacy;
  legacy.Prepare(crunchy::circuit::PostLoopPlateOhms, crunchy::circuit::DeepCathodeOhms,
    crunchy::circuit::DeepFixedBypass, crunchy::circuit::DeepSwitchedBypass,
    crunchy::circuit::BassDeepReturnOhms, kFs);

  crunchy::PlateLoadedCathodeSwitch loaded;
  loaded.Prepare(crunchy::circuit::PostLoopPlateOhms,
    crunchy::circuit::PostLoopCathodeFeedbackPlateLoadOhms,
    crunchy::circuit::DeepCathodeOhms, crunchy::circuit::DeepFixedBypass,
    crunchy::circuit::DeepSwitchedBypass, crunchy::circuit::BassDeepReturnOhms, kFs);

  // 0.10.21 is allowed to strengthen the pull-switch delta, but it must not
  // move the already-calibrated amplifier when the switch is OFF.
  for (int n = 0; n < 8192; ++n) {
    const double x = 0.3 * std::sin(2.0 * kPi * 83.0 * static_cast<double>(n) / kFs);
    const double a = legacy.Process(x, 0.0);
    const double b = loaded.Process(x, 0.0);
    Require(std::abs(a - b) < 1.0e-15,
            "Plate-load correction changed the calibrated switch-OFF cathode response.");
  }
}

void TestCathodeSwitchDezipper()
{
  crunchy::SwitchableCathodeNetwork network;
  network.Prepare(crunchy::circuit::PostLoopPlateOhms, crunchy::circuit::DeepCathodeOhms, 0.47e-6, crunchy::circuit::CathodeBypass, crunchy::circuit::BassDeepReturnOhms, kFs);

  const double coeff = std::exp(-1.0 / (0.006 * kFs));
  double amount = 0.0;
  double previous = 0.0;
  double maxStep = 0.0;
  constexpr int total = static_cast<int>(kFs * 0.25);
  constexpr int switchAt = static_cast<int>(kFs * 0.10);
  for (int n = 0; n < total; ++n) {
    const double target = n >= switchAt ? 1.0 : 0.0;
    amount = target - coeff * (target - amount);
    const double x = std::sin(2.0 * kPi * 80.0 * static_cast<double>(n) / kFs);
    const double y = network.Process(x, amount);
    if (n > 0) maxStep = std::max(maxStep, std::abs(y - previous));
    previous = y;
  }
  Require(maxStep < 0.02,
          "The fixed-topology Deep/Bass switch crossfade produced a discontinuity large enough to click.");
}

double LeadMixRms(double frequency, double leadMode, bool driveLeadSource)
{
  crunchy::LeadReturnMixer mixer;
  mixer.Prepare(kFs);
  constexpr int warmup = static_cast<int>(kFs * 0.10);
  constexpr int total = static_cast<int>(kFs * 0.35);
  double sum = 0.0;
  for (int n = 0; n < total; ++n) {
    const double x = std::sin(2.0 * kPi * frequency * static_cast<double>(n) / kFs);
    const double y = mixer.Process(driveLeadSource ? 0.0 : x, driveLeadSource ? x : 0.0, leadMode);
    if (n >= warmup) sum += y * y;
  }
  return std::sqrt(sum / static_cast<double>(total - warmup));
}

void TestChannel()
{
  // In Rhythm LDR3 is open: R10/C10 sees R11/C11 only. In Lead LDR3
  // connects C30 -> (R31||C31) and the R32||C32 shunt to the SAME recovery-grid
  // node. The Lead network therefore loads the Rhythm feed exactly at that node.
  const double rhythmDry = LeadMixRms(1000.0, 0.0, false);
  const double leadDry = LeadMixRms(1000.0, 1.0, false);
  Require(leadDry < rhythmDry * 0.60,
          "LDR3 Lead mode is not applying the schematic Lead-output loading to the Rhythm feed.");

  const double disconnectedLead = LeadMixRms(1000.0, 0.0, true);
  const double connectedLead = LeadMixRms(1000.0, 1.0, true);
  Require(disconnectedLead < 1.0e-9, "LDR3 must disconnect the Lead source in Rhythm mode.");
  Require(connectedLead > 0.05, "LDR3 must connect the Lead source to the V2 recovery-grid node.");
}

void TestLeadOutputVoicing()
{
  // Handwritten source: C31=330pF is parallel with R31=220k, while
  // C32=560pF is parallel with the 100k output shunt. This is not a simple
  // monotonic low-pass: the 330p bright path raises the upper mids, then the
  // 560p shunt turns the response back down at the top of the guitar band.
  const double oneK = LeadMixRms(1000.0, 1.0, true);
  const double fourK = LeadMixRms(4000.0, 1.0, true);
  const double twelveK = LeadMixRms(12000.0, 1.0, true);
  Require(fourK > oneK * 1.05,
          "330pF across the 220K Lead-output resistor is not producing its upper-mid lift.");
  Require(twelveK < fourK * 0.92,
          "560pF Lead-output shunt is not producing the expected high-frequency turnover.");
}

void TestLeadBright()
{
  const double off = CathodeRms(5000.0, crunchy::circuit::LeadPlateOhms, crunchy::circuit::LeadCathodeOhms, 0.0, crunchy::circuit::LeadBrightBypass,
                                crunchy::controls::SwitchedReturn(0.0, crunchy::circuit::LeadBrightReturnOhms));
  const double on = CathodeRms(5000.0, crunchy::circuit::LeadPlateOhms, crunchy::circuit::LeadCathodeOhms, 0.0, crunchy::circuit::LeadBrightBypass,
                               crunchy::controls::SwitchedReturn(1.0, crunchy::circuit::LeadBrightReturnOhms));
  Require(on > off * 1.5, "Lead Bright is not increasing the upper-frequency gain of the Lead stage.");
}

void TestDeep()
{
  const double off = CathodeRms(80.0, crunchy::circuit::PostLoopPlateOhms, crunchy::circuit::DeepCathodeOhms, 0.47e-6, crunchy::circuit::CathodeBypass,
                                crunchy::controls::SwitchedReturn(0.0, crunchy::circuit::BassDeepReturnOhms));
  const double on = CathodeRms(80.0, crunchy::circuit::PostLoopPlateOhms, crunchy::circuit::DeepCathodeOhms, 0.47e-6, crunchy::circuit::CathodeBypass,
                               crunchy::controls::SwitchedReturn(1.0, crunchy::circuit::BassDeepReturnOhms));
  Require(on > off * 1.35, "Deep is not increasing low-frequency gain in the late preamp stage.");
}

double PresenceRms(double frequency, double presence)
{
  crunchy::PresenceNetwork network;
  network.Reset();
  network.Configure(presence, kFs);
  constexpr int warmup = static_cast<int>(kFs * 0.10);
  constexpr int total = static_cast<int>(kFs * 0.35);
  double sum = 0.0;
  for (int n = 0; n < total; ++n) {
    const double x = std::sin(2.0 * kPi * frequency * static_cast<double>(n) / kFs);
    const double y = network.Process(x);
    if (n >= warmup) sum += y * y;
  }
  return std::sqrt(sum / static_cast<double>(total - warmup));
}

void TestPresence()
{
  const double feedbackDark = PresenceRms(5000.0, 0.0);
  const double feedbackBright = PresenceRms(5000.0, 10.0);
  Require(feedbackBright < feedbackDark * 0.20,
          "Presence 10 must substantially reduce high-frequency negative feedback.");
}

void TestGraphicEqStartupAndResetPolicy()
{
  const double expected[5] = {6.0, 0.0, -6.0, 0.0, 6.0};
  for (int i = 0; i < 5; ++i)
    Require(std::abs(crunchy::controls::GraphicEqStartupDb[i] - expected[i]) < 1e-12,
            "Graphic EQ startup contour must remain +6/0/-6/0/+6 dB.");
  Require(std::abs(crunchy::controls::GraphicEqDoubleClickResetDb) < 1e-12,
          "Graphic EQ double-click reset target must remain 0 dB.");
}

void TestEqSwitching()
{
  using crunchy::controls::EqModeEngagement;
  Require(EqModeEngagement(0.0, 0.0) == 0.0, "EQ AUTO / LEAD must stay off in Rhythm.");
  Require(EqModeEngagement(0.0, 1.0) == 1.0, "EQ AUTO / LEAD must engage in Lead.");
  Require(EqModeEngagement(1.0, 0.0) == 0.0, "EQ OUT must bypass the EQ in Rhythm.");
  Require(EqModeEngagement(1.0, 1.0) == 0.0, "EQ OUT must bypass the EQ in Lead.");
  Require(EqModeEngagement(2.0, 0.0) == 1.0, "EQ IN / ALL must engage in Rhythm.");
  Require(EqModeEngagement(2.0, 1.0) == 1.0, "EQ IN / ALL must engage in Lead.");
}

void TestHandwrittenSchematicValues()
{
  using namespace crunchy::circuit;
  Require(std::abs(LeadCathodeOhms - 3300.0) < 1e-9, "V4A fixed cathode resistor must be 3.3K.");
  Require(std::abs(LeadBrightReturnOhms - 22000.0) < 1e-9, "V4A Pull Bright return must be 22K.");
  Require(std::abs(LeadInterstageCoupling - 20e-9) < 1e-18, "V3B->V4A coupling must follow handwritten .02uF.");
  Require(std::abs(LeadOutputBright - 330e-12) < 1e-18, "Lead series bright capacitor must be 330pF.");
  Require(std::abs(LeadOutputShuntC - 560e-12) < 1e-18, "Lead output shunt capacitor must be 560pF.");
  Require(std::abs(ReverbBridgeR102 - 15000.0) < 1e-9, "TO/FROM REVERB bridge must be 15K.");
  Require(std::abs(PostLoopPlateOhms - 170000.0) < 1e-9, "Post-loop plate resistor must be 170K.");
  Require(std::abs(DeepCathodeOhms - 1000.0) < 1e-9, "Deep-stage cathode resistor must be 1K.");
  Require(std::abs(FeedbackSeriesCap - 5e-9) < 1e-18, "Presence feedback capacitor must be .005uF.");
  Require(std::abs(PreampSupplyC - 337.0) < 1e-9 && std::abs(PreampSupplyD - 383.0) < 1e-9,
          "Preamp rails must follow the handwritten C/D annotations.");
}
} // namespace

int main()
{
  TestVolumeBright();
  TestTrebleShift();
  TestBassShift();
  TestPlateLoadedCathodeSwitchPreservesOff();
  TestCathodeSwitchDezipper();
  TestChannel();
  TestLeadOutputVoicing();
  TestLeadBright();
  TestDeep();
  TestPresence();
  TestGraphicEqStartupAndResetPolicy();
  TestEqSwitching();
  TestHandwrittenSchematicValues();
  std::cout << "Front-panel switches and handwritten-schematic regression checks passed.\n";
  return 0;
}
