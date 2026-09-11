#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "dsp/CircuitFilters.h"
#include "dsp/GraphicEqCircuit.h"
#include "dsp/ControlLogic.h"
#include "dsp/SpringReverb.h"

namespace {
[[noreturn]] void Fail(const std::string& message) {
  std::cerr << "PASS9 REGRESSION FAILED: " << message << "\n";
  std::exit(1);
}
void Require(bool condition, const std::string& message) {
  if (!condition) Fail(message);
}
void Near(double actual, double expected, double tolerance, const std::string& message) {
  if (std::abs(actual - expected) > tolerance) Fail(message);
}

void TestDecodedConstants() {
  using namespace crunchy::circuit;
  // Input / tone: assert the complete decoded component set, not only the
  // values that changed in the previous patch.
  Near(InputGridLeakOhms, 1000000.0, 1e-6, "Input grid leak must be 1M.");
  Near(V1ACathodeOhms, 1500.0, 1e-9, "V1A cathode must be 1.5K.");
  Near(ToneFeedOhms, 100000.0, 1e-9, "Tone feed must be 100K.");
  Near(TrebleCap, 250e-12, 1e-18, "Treble capacitor must be 250pF.");
  Near(ToneUpperCap, 0.1e-6, 1e-18, "Tone upper capacitor must be .1uF.");
  Near(ToneLowerCap, 0.047e-6, 1e-18, "Tone lower capacitor must be .047uF.");
  Near(TreblePotOhms, 250000.0, 1e-9, "Treble pot must be 250K.");
  Near(BassPotOhms, 250000.0, 1e-9, "Bass pot must be 250K.");
  Near(MiddlePotOhms, 10000.0, 1e-9, "Middle pot must be 10K.");
  Near(Volume1Ohms, 1000000.0, 1e-6, "Volume 1 must be 1M.");
  Near(Volume1BrightCap, 180e-12, 1e-18, "Volume 1 bright cap must be 180pF.");
  Near(BassShiftSeriesOhms, 15000.0, 1e-9, "Bass Shift series resistor must be 15K.");
  Near(V1BPlateOhms, 100000.0, 1e-9, "V1B plate must be 100K.");
  Near(V1BCathodeOhms, 1500.0, 1e-9, "V1B cathode must be 1.5K.");
  Near(V1BCathodeBypass, 15e-6, 1e-18, "V1B bypass must be 15uF.");
  Near(V1APlateOhms, 150000.0, 1e-9, "V1A plate must be 150K.");
  Near(V1AFixedCathodeBypass, 0.47e-6, 1e-18, "V1A fixed bypass must be .47uF.");
  Near(V1ALargeCathodeBypass, 15e-6, 1e-18, "V1A switched bypass must be 15uF.");
  Near(TrebleShiftCap, 750e-12, 1e-18, "Treble Shift capacitor must be 750pF.");
  Near(TrebleShiftResistor, 10e6, 1e-6, "Treble Shift resistor must be 10M.");
  Near(V1BOutputCoupling, 47e-9, 1e-18, "V1B Lead/Rhythm feed must be .047uF.");
  Near(LeadInputNodeShunt, 100000.0, 1e-9, "TO LEAD INPUT shunt must be 100K.");
  Near(RhythmMixSeries, 3300000.0, 1e-6, "Rhythm mix resistor must be 3.3M.");
  Near(RhythmMixBright, 10e-12, 1e-18, "Rhythm mix capacitor must be 10pF.");
  Near(DeepFixedBypass, 0.47e-6, 1e-18, "Deep fixed bypass must be .47uF.");
  Near(DeepSwitchedBypass, 15e-6, 1e-18, "Deep switched bypass must be 15uF.");
  Near(BassDeepReturnOhms, 15000.0, 1e-9, "Deep switched series resistor must be 15K.");
  Near(DeepCathodeOhms, 1000.0, 1e-9, "Deep cathode shunt must be 1K.");
  Near(PostLoopCathodeFeedbackPlateLoadOhms, Parallel(PostLoopPlateOhms, MasterOhms), 1e-9,
       "Deep cathode switch must account for the decoded downstream Master load.");
  Near(V4ACathodeFeedbackPlateLoadOhms, Parallel(V4APlateOhms, LeadOutputSeries + LeadOutputShuntR), 1e-9,
       "Lead Bright cathode switch must account for the decoded Lead-output load.");
  Near(V1ACathodeFeedbackPlateLoadOhms, Parallel(V1APlateOhms, V1AToneBranchApproxOhms), 1e-9,
       "Bass Shift cathode switch must account for the decoded tone-network load estimate.");
  Near(FromReverbShuntR101, 4700.0, 1e-9, "FROM REVERB shunt must be 4.7K.");
  Near(LeadMasterOhms, 250000.0, 1e-9, "Lead Master must be 250K.");
  Near(PreLoopPlateOhms, 100000.0, 1e-9, "Pre-loop plate must be 100K.");
  Near(PreLoopCathodeOhms, 1500.0, 1e-9, "Pre-loop cathode must be 1.5K.");
  Near(ReturnShuntR103, 47000.0, 1e-9, "Return shunt must be 47K.");
  Near(RecoverySeriesR105, 47000.0, 1e-9, "V2/reverb coupling branch R105 must be 47K.");
  Near(V3BPlateOhms, 82000.0, 1e-9, "V3B plate must be 82K.");
  Near(V3BCathodeOhms, 1500.0, 1e-9, "V3B cathode must be 1.5K.");
  Near(V3BGridCathodeCap, 120e-12, 1e-18, "V3B local capacitor must be 120pF.");
  Near(LeadDriveSeries, 680000.0, 1e-6, "Lead input series resistor must be 680K.");
  Near(LeadDriveOhms, 1000000.0, 1e-6, "Lead Drive must be 1M.");
  Near(V3BCathodeBypass, 15e-6, 1e-18, "V3B bypass must be 15uF.");
  Near(LeadInterstageCoupling, 20e-9, 1e-18, "V3B->V4A coupling must be .02uF.");
  Near(V4APlateOhms, 270000.0, 1e-9, "V4A plate must be 270K.");
  Near(V4ACathodeOhms, 3300.0, 1e-9, "V4A fixed cathode must be 3.3K.");
  Near(V4ABrightBypass, 0.22e-6, 1e-18, "V4A bright bypass must be .22uF.");
  Near(V4ABrightReturnOhms, 22000.0, 1e-9, "V4A bright return must be 22K.");
  Near(V4AGridSeries, 270000.0, 1e-9, "V4A grid series resistor must be 270K.");
  Near(V4AGridShunt, 68000.0, 1e-9, "V4A grid shunt resistor must be 68K.");
  Near(V4AGridShuntCap, 1e-9, 1e-18, "V4A grid shunt cap must be .001uF.");
  Near(V4APlateShunt, 1e-9, 1e-18, "V4A plate-supply small cap must be .001uF.");
  Near(LeadOutputCoupling, 47e-9, 1e-18, "V4A output coupling must be .047uF.");
  Near(LeadOutputSeries, 220000.0, 1e-9, "Lead output series resistor must be 220K.");
  Near(LeadOutputShuntR, 100000.0, 1e-9, "Lead output shunt resistor must be 100K.");
  Near(LeadOutputBright, 330e-12, 1e-18, "Lead-output bright cap must be 330pF.");
  Near(LeadOutputShuntC, 560e-12, 1e-18, "Lead-output shunt cap must be 560pF.");
  Near(ReverbBridgeR102, 15000.0, 1e-9, "Normalled reverb bridge must be 15K.");
  Near(PostLoopPlateOhms, 170000.0, 1e-9, "Post-loop plate must be 170K.");
  Near(MasterOhms, 100000.0, 1e-9, "Master must be 100K.");
  Near(FeedbackSeriesCap, 5e-9, 1e-18, "Presence shunt capacitor must be .005uF.");
  Near(LowerFeedbackCapA, 47e-9, 1e-18, "Lower feedback capacitor A must be .047uF.");
  Near(LowerFeedbackCapB, 0.1e-6, 1e-18, "Lower feedback capacitor B must be .1uF.");
  Near(PresenceSeries, 3300.0, 1e-9, "Presence series resistor must be 3.3K.");
  Near(PhaseNegativePlateHF, 120e-12, 1e-18, "V5B HF bypass must be 120pF.");
  Near(PowerGridLeakOhms, 220000.0, 1e-9, "Power grid-leak family must be 220K.");
  Near(PowerOuterShuntOhms, 680000.0, 1e-6, "Outer V8/V9 shunts must be 680K.");
  Near(PowerGridStopperOhms, 2200.0, 1e-9, "Power grid stopper must be 2.2K.");
  Near(GraphicEqBand1CenterHz, 60.0, 1e-9, "Pass 9 labels first GEQ band 60Hz.");
  Near(GraphicEqBand1L, 1.3, 1e-12, "First GEQ inductor must be 1.3H.");
  Near(GraphicEqBand1C, 3.3e-6, 1e-18, "First GEQ capacitor must be 3.3uF.");
  Near(GraphicEqBand1Series, 470.0, 1e-9, "First GEQ series resistor must be 470 ohms.");
  Near(GraphicEqBand2CenterHz, 240.0, 1e-9, "Second GEQ center label must be 240Hz.");
  Near(GraphicEqBand2L, 0.39, 1e-12, "Second GEQ inductor must be .39H.");
  Near(GraphicEqBand2C, 0.47e-6, 1e-18, "Second GEQ capacitor must be .47uF.");
  Near(GraphicEqBand2Series, 470.0, 1e-9, "Second GEQ series resistor must be 470 ohms.");
  Near(GraphicEqBand3CenterHz, 750.0, 1e-9, "Third GEQ center label must be 750Hz.");
  Near(GraphicEqBand3L, 0.22, 1e-12, "Third GEQ inductor must be .22H.");
  Near(GraphicEqBand3C, 0.22e-6, 1e-18, "Third GEQ capacitor must be .22uF.");
  Near(GraphicEqBand3Series, 470.0, 1e-9, "Third GEQ series resistor must be 470 ohms.");
  Near(GraphicEqBand4CenterHz, 2200.0, 1e-9, "Fourth GEQ center label must be 2200Hz.");
  Near(GraphicEqBand4L, 0.068, 1e-12, "Fourth GEQ inductor must be .068H.");
  Near(GraphicEqBand4C, 0.15e-6, 1e-18, "Fourth GEQ capacitor must be .15uF.");
  Near(GraphicEqBand4Series, 1000.0, 1e-9, "Fourth GEQ series resistor must be 1K.");
  Near(GraphicEqBand5CenterHz, 6600.0, 1e-9, "Fifth GEQ center label must be 6600Hz.");
  Near(GraphicEqBand5L, 0.033, 1e-12, "Fifth GEQ inductor must be .033H.");
  Near(GraphicEqBand5C, 0.033e-6, 1e-18, "Fifth GEQ capacitor must be .033uF.");
  Near(GraphicEqBand5Series, 1000.0, 1e-9, "Fifth GEQ series resistor must be 1K.");
  Near(GraphicEqSliderOhms, 50000.0, 1e-9, "GEQ sliders must be 50K.");
  Near(PreampSupplyC, 337.0, 1e-9, "Preamp-sheet C rail must remain 337V.");
  Near(PowerSheetSupplyC, 397.0, 1e-9, "Power-sheet C rail conflict must remain explicit at 397V.");

  // Pass-9 sections added to the active/reduced 0.10.1 model or retained as
  // electrically meaningful hardware metadata.
  Near(LaterSimulRhythmMixBright, 20e-12, 1e-18, "Later Simul mix note must remain 20pF.");

  Near(ReverbDriverCathodeBypass, 0.33e-6, 1e-18, "Reverb driver cathode bypass must be .33uF.");
  Near(ReverbDriverCathodeOhms, 1000.0, 1e-9, "Reverb driver cathode must be 1K.");
  Near(ReverbDriverPlateSupplyOhms, 10000.0, 1e-9, "Reverb driver plate supply must be 10K.");
  Near(ReverbDriverPlateSupplyWatts, 2.0, 1e-12, "Reverb driver plate supply rating must remain 2W.");
  Near(ReverbFootswitchCathodeOhms, 15000.0, 1e-9, "Reverb footswitch branch must retain 15K.");
  Near(ReverbLevelOhms, 100000.0, 1e-9, "Reverb pot must be 100K.");
  Near(ReverbV3ACathodeBypass, 15e-6, 1e-18, "V3A reverb bypass must be 15uF.");
  Near(ReverbV3ACathodeOhms, 3300.0, 1e-9, "V3A reverb cathode must be 3.3K.");
  Near(ReverbV3AGridShuntOhms, 220000.0, 1e-9, "V3A reverb grid shunt must be 220K.");
  Near(ReverbV3AOutputCoupling, 0.01e-6, 1e-18, "V3A reverb output coupling must be .01uF.");
  Near(ReverbV3APlateFeedOhms, 100000.0, 1e-9, "V3A reverb plate feed must be 100K.");
  Near(ReverbV3ACathodeVolts, 2.6, 1e-12, "V3A handwritten cathode point must be 2.6V.");
  Near(ReverbDriverPlateVolts, 339.0, 1e-9, "Reverb driver plate annotation must be 339V.");
  Near(ReverbDriverCathodeVoltsUncertain, 3.8, 1e-12, "Reverb driver cathode annotation must remain 3.8V MEDIUM-HIGH.");

  Near(GraphicEqQ1Q2Q4LocalOhms, 3300.0, 1e-9, "GEQ local transistor resistors must be 3.3K.");
  Near(GraphicEqSharedQ23Ohms, 22000.0, 1e-9, "GEQ shared Q2/Q3 resistor must be 22K.");
  Near(GraphicEqFeedbackCap, 10e-12, 1e-18, "GEQ feedback caps must be 10pF.");
  Near(GraphicEqUpperRailOhms, 3300.0, 1e-9, "GEQ upper-rail resistor must be 3.3K.");
  Near(GraphicEqBiasLoadOhms, 470000.0, 1e-9, "GEQ bias/load resistor must be 470K.");
  Near(GraphicEqOutputCoupling, 47e-9, 1e-18, "GEQ output coupling must be .047uF.");
  Near(GraphicEqOutputShunt, 3300.0, 1e-9, "GEQ output shunt must be 3.3K.");

  Near(LdrControlReservoir, 470e-6, 1e-18, "LDR control reservoir must be 470uF.");
  Near(LdrControlReservoirVolts, 16.0, 1e-12, "LDR reservoir rating must remain 16V.");
  Near(LdrR88, 680.0, 1e-9, "LDR control R88 must be 680 ohms.");
  Near(LdrR89, 475.0, 1e-9, "LDR control R89 must be 475 ohms.");
  Near(LdrR90, 3300.0, 1e-9, "LDR control R90 must be 3.3K.");
  Near(LdrR91, 470.0, 1e-9, "LDR control R91 must be 470 ohms.");
  Near(LdrR92, 680.0, 1e-9, "LDR control R92 must be 680 ohms.");
  Near(LdrR92Watts, 1.0, 1e-12, "LDR control R92 must retain the handwritten 1W rating.");

  Require(PowerABElementIsChoke, "Power A-B element must remain identified as a choke.");
  Near(PowerDropBCOhms, 5600.0, 1e-9, "B-C power drop must be 5.6K.");
  Near(PowerDropBCWatts, 2.0, 1e-12, "B-C resistor rating must remain 2W.");
  Near(PowerDropCDOhms, 1000.0, 1e-9, "C-D power drop must be 1K.");
  Near(PowerDropCDWatts, 2.0, 1e-12, "C-D resistor rating must remain 2W.");
  Near(PowerBCDFilterCap, 30e-6, 1e-18, "B/C/D filters must be 30uF.");
  Near(PowerBCDFilterCapVolts, 500.0, 1e-9, "B/C/D filter rating must remain 500V.");
  Near(PowerReservoirBleederOhms, 150000.0, 1e-9, "Power bleeder family must be 150K.");
  Near(PowerReservoirBleederWatts, 1.0, 1e-12, "Power bleeder rating must remain 1W.");
  Near(PowerReservoirCap, 220e-6, 1e-18, "Reservoir family must be 220uF.");
  Near(PowerReservoirCapVolts, 350.0, 1e-9, "Reservoir family must retain 350V handwritten rating.");
  Near(PowerSheetSupplyA, 480.0, 1e-9, "Power rail A must be 480V.");
  Near(PowerSheetSupplyB, 435.0, 1e-9, "Power rail B must be 435V.");
  Near(PowerSheetSupplyC, 397.0, 1e-9, "Power rail C must be 397V.");
  Near(PowerSheetSupplyD, 383.0, 1e-9, "Power rail D must be 383V.");

  Near(PresencePotOhms, 250000.0, 1e-9, "Presence pot must be 250K.");
  Near(FeedbackSeries, 56000.0, 1e-9, "Presence shunt resistor must be 56K.");
  Near(FeedbackToPI, 22000.0, 1e-9, "Presence-to-PI resistor must be 22K.");
  Near(TailSeries, 1500.0, 1e-9, "Feedback tail resistor must be 1.5K.");
  Near(TailShunt, 3300.0, 1e-9, "Lower feedback shunt must be 3.3K.");
  Near(PhasePlatePositive, 82000.0, 1e-9, "PI V5A plate must be 82K.");
  Near(PhasePlateNegative, 91000.0, 1e-9, "PI V5B plate must be 91K.");
  Near(PhaseGridPositive, 100000.0, 1e-9, "PI V5A grid/load must be 100K.");
  Near(PhaseGridNegative, 150000.0, 1e-9, "PI V5B grid/load must be 150K.");
  Near(PhaseInterNode, 470.0, 1e-9, "PI inter-node resistor must be 470 ohms.");
  Near(PhaseOutputCoupling, 0.1e-6, 1e-18, "PI output couplers must be .1uF.");
  Near(Bias60Volts, 47.0, 1e-9, "60W bias magnitude must remain 47V.");
  Near(Bias100Volts, 52.0, 1e-9, "100W bias magnitude must remain 52V.");
  Near(BiasSimulVolts, 67.0, 1e-9, "Simul bias magnitude must be 67V.");
  Near(Bias60ExportVolts, 47.0, 1e-9, "60W export bias magnitude must remain 47V.");
  Near(Bias100ExportVolts, 47.0, 1e-9, "100W export bias magnitude must remain 47V.");
  Near(BiasSimulExportVolts, 55.0, 1e-9, "Simul export bias magnitude must be 55V.");
  Near(PowerOuterPlateVolts, 478.0, 1e-9, "Outer V8/V9 plate annotation must remain 478V.");
  Near(PowerInnerTapVolts, 430.0, 1e-9, "Inner V6/V7 tap annotation must remain 430V.");
  Near(DirectOutputFeedOhms, 4700.0, 1e-9, "Direct/Slave feed must be 4.7K.");
  Near(PowerScreenOhms, 470.0, 1e-9, "Power screen resistors must be 470 ohms.");
  Near(PowerScreenResistorWatts, 2.0, 1e-12, "Power screen resistor rating must remain 2W.");
  Near(HeaterArtificialCenterTapA, 100.0, 1e-9, "Heater artificial center-tap resistor A must be 100 ohms.");
  Near(HeaterArtificialCenterTapB, 100.0, 1e-9, "Heater artificial center-tap resistor B must be 100 ohms.");
}

void TestMeasuredOperatingPointAnchors() {
  using namespace crunchy::circuit;
  crunchy::TriodeStage v1a;
  v1a.Prepare(PreampSupplyD, V1APlateOhms, V1ACathodeOhms, V1ACathodeVolts);
  Near(v1a.IdleCathodeVolts(), 1.6, 1e-12, "V1A measured cathode point was not applied.");
  Require(std::abs(v1a.Process(0.0)) < 1e-4, "V1A calibrated transfer is not centered at its Q point.");

  crunchy::TriodeStage v1b;
  v1b.Prepare(PreampSupplyD, V1BPlateOhms, V1BCathodeOhms, V1BCathodeVolts);
  Near(v1b.IdleCathodeVolts(), 1.9, 1e-12, "V1B measured cathode point was not applied.");
  Require(std::abs(v1b.Process(0.0)) < 1e-4, "V1B calibrated transfer is not centered at its Q point.");

  crunchy::TriodeStage v4a;
  v4a.Prepare(PreampSupplyC, V4APlateOhms, V4ACathodeOhms, V4ACathodeVolts);
  Near(v4a.IdleCathodeVolts(), 2.0, 1e-12, "V4A measured cathode point was not applied.");
  Require(std::abs(v4a.Process(0.0)) < 1e-4, "V4A calibrated transfer is not centered at its Q point.");
  Require(v4a.GridBiasCorrectionVolts() > 0.4 && v4a.GridBiasCorrectionVolts() < 0.9,
          "V4A Koren calibration correction is outside the expected Pass-9 anchored range.");
}

void TestReverbModel() {
  crunchy::Pass9ReverbCircuit reverb;
  constexpr double internalRate = 48000.0 * 8.0;
  reverb.Prepare(internalRate);
  double wetEnergy = 0.0;
  for (int n = 0; n < static_cast<int>(internalRate * 0.45); ++n) {
    const double y = reverb.Process(n == 0 ? 1.0 : 0.0, 10.0, 1.0);
    Require(std::isfinite(y), "Pass-9 spring reverb model produced a non-finite sample.");
    wetEnergy += y * y;
  }
  Require(wetEnergy > 1e-6, "Enabled Pass-9 reverb must produce a spring tail.");

  reverb.Reset();
  double mutedEnergy = 0.0;
  for (int n = 0; n < static_cast<int>(internalRate * 0.20); ++n) {
    const double y = reverb.Process(n == 0 ? 1.0 : 0.0, 10.0, 0.0);
    mutedEnergy += y * y;
  }
  Require(mutedEnergy < 1e-18, "Reverb footswitch off must mute the wet return.");
}

void TestLoadedPiImbalance() {
  using namespace crunchy::circuit;
  constexpr double positiveLoad = Parallel(PhasePlatePositive, PowerGridLeakOhms);
  constexpr double negativeLoad = Parallel(PhasePlateNegative, PowerGridLeakOhms);
  constexpr double positiveGain = TriodeMuEstimate * positiveLoad / (2 * (TriodeRpEstimate + positiveLoad));
  constexpr double negativeGain = TriodeMuEstimate * negativeLoad / (2 * (TriodeRpEstimate + negativeLoad));
  const double ratio = negativeGain / positiveGain;
  Require(ratio > 1.0 && ratio < 1.06,
          "Loaded 82K/91K PI imbalance should be modest, not the raw 91/82 ratio used in build 0.9.");
}

void TestSwitchIntent() {
  using crunchy::controls::TrebleShift;
  Require(TrebleShift(1.0, 0.0) == 0.0, "Pass-9 LDR1 Treble Shift must be inactive in Rhythm.");
  Require(TrebleShift(1.0, 1.0) == 1.0, "Pass-9 LDR1 Treble Shift must work in Lead.");
}
}

int main() {
  TestDecodedConstants();
  TestMeasuredOperatingPointAnchors();
  TestReverbModel();
  TestLoadedPiImbalance();
  TestSwitchIntent();
  std::cout << "Pass-9 decoded-value and operating-point regression checks passed.\n";
  return 0;
}
