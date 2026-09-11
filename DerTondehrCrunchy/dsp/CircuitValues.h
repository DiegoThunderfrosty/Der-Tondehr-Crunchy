#pragma once
#include <algorithm>
#include <cmath>

namespace crunchy::circuit {
// PASS 9 authority policy:
// - handwritten original wins whenever it disagrees with a redraw;
// - redraw-only values may remain only where Pass 9 does not contradict them;
// - source-internal conflicts are kept explicit instead of silently reconciled.
constexpr double Pi = 3.14159265358979323846;
constexpr double Parallel(double a, double b) { return a*b/(a+b); }
inline double AudioTaper(double knob) {
  // A/log pot approximation: 10% resistance at half rotation. Exact CTS law unknown.
  return (std::pow(81.0, std::clamp(knob/10.0,0.0,1.0))-1.0)/80.0;
}

// -----------------------------------------------------------------------------
// Input / tone network - handwritten Pass 9 values.
// ----------------------------------------------------------------------------
constexpr double InputGridLeakOhms = 1000000.0;
constexpr double V1APlateOhms = 150000.0;
constexpr double V1ACathodeOhms = 1500.0;
constexpr double V1AFixedCathodeBypass = 0.47e-6;
constexpr double V1ALargeCathodeBypass = 15e-6;
constexpr double V1ACathodeVolts = 1.6; // handwritten operating-point annotation
constexpr double ToneFeedOhms = 100000.0;
constexpr double TrebleCap = 250e-12;
constexpr double ToneUpperCap = 0.1e-6;
constexpr double ToneLowerCap = 0.047e-6;
constexpr double TreblePotOhms = 250000.0;
constexpr double BassPotOhms = 250000.0;
constexpr double MiddlePotOhms = 10000.0;
constexpr double Volume1Ohms = 1000000.0;
constexpr double Volume1BrightCap = 180e-12;
constexpr double TrebleShiftCap = 750e-12;
constexpr double TrebleShiftResistor = 10000000.0;
constexpr double BassShiftSeriesOhms = 15000.0;

constexpr double V1BPlateOhms = 100000.0;
constexpr double V1BCathodeOhms = 1500.0;
constexpr double V1BCathodeBypass = 15e-6;
constexpr double V1BOutputCoupling = 0.047e-6;
constexpr double V1BCathodeVolts = 1.9; // handwritten operating-point annotation
constexpr double LeadInputNodeShunt = 100000.0;

// Common cathode-network aliases retained for existing call sites.
constexpr double CathodeBypass = V1ALargeCathodeBypass;
constexpr double FixedCathodeBypass = V1AFixedCathodeBypass;
constexpr double BassDeepReturnOhms = 15000.0;

// Cathode-switch AC plate loading. No new component value is introduced here:
// every term below is already a Pass-9 value. These equivalent resistances are
// used only to correct the ON/OFF cathode-bypass differential; the nonlinear
// triode load lines keep their original Pass-9 plate resistors.
// V1A sees the tone network after the 100K feed. Because that network is
// frequency dependent, use its resistor skeleton as a conservative load
// estimate rather than pretending Pass 9 supplied an exact complex impedance.
constexpr double V1AToneBranchApproxOhms = ToneFeedOhms +
  Parallel(TreblePotOhms, BassPotOhms + MiddlePotOhms);
constexpr double V1ACathodeFeedbackPlateLoadOhms =
  Parallel(V1APlateOhms, V1AToneBranchApproxOhms);

// Preamp rails as written on the handwritten preamp page.  Power-sheet C=397 V
// is a genuine source-internal conflict and is intentionally not substituted here.
constexpr double PreampSupplyD = 383.0; // V1A/V1B rail
constexpr double PreampSupplyC = 337.0; // V2/V3/V4 rail on preamp sheet
constexpr double PowerSheetSupplyA = 480.0;
constexpr double PowerSheetSupplyB = 435.0;
constexpr double PowerSheetSupplyC = 397.0;
constexpr double PowerSheetSupplyD = 383.0;

// -----------------------------------------------------------------------------
// Lead branch.
// -----------------------------------------------------------------------------
constexpr double V3BPlateOhms = 82000.0;
constexpr double V3BCathodeOhms = 1500.0;
constexpr double V3BCathodeBypass = 15e-6;
constexpr double V3BGridCathodeCap = 120e-12;

constexpr double V4APlateOhms = 270000.0;
constexpr double V4ACathodeOhms = 3300.0;
constexpr double V4ACathodeVolts = 2.0; // confirmed handwritten operating point
constexpr double V4ABrightReturnOhms = 22000.0;
constexpr double V4ABrightBypass = 0.22e-6;
constexpr double V4AGridSeries = 270000.0;
constexpr double V4AGridShunt = 68000.0;
constexpr double V4AGridShuntCap = 1e-9;
constexpr double V4APlateShunt = 1e-9;

// Existing names retained for source compatibility.
constexpr double LeadPlateOhms = V4APlateOhms;
constexpr double LeadCathodeOhms = V4ACathodeOhms;
constexpr double LeadBrightReturnOhms = V4ABrightReturnOhms;
constexpr double LeadDriveSeries = 680000.0;
constexpr double LeadDriveOhms = 1000000.0;
// R22 is a redraw-only lead-grid shunt (470K/475K family). Pass 9 does not
// contradict it, so keep the 475K value already used by build 0.9.
constexpr double LeadGridLeak = 475000.0;
constexpr double LeadGridCathodeCap = V3BGridCathodeCap;
constexpr double LeadInterstageCoupling = 20e-9; // handwritten .02 uF
constexpr double LeadInterstageSeries = V4AGridSeries;
constexpr double LeadInterstageLoad = V4AGridShunt;
constexpr double LeadInterstageShunt = V4AGridShuntCap;
constexpr double LeadPlateShunt = V4APlateShunt;
constexpr double LeadBrightBypass = V4ABrightBypass;

// V1B / Lead-return mixing node.
constexpr double RhythmMixSeries = 3300000.0;
constexpr double RhythmMixBright = 10e-12;
constexpr double LaterSimulRhythmMixBright = 20e-12; // handwritten later-Simul note
// R11/C11 are readable-redraw values and are not contradicted by Pass 9.
constexpr double RecoveryGridLeak = 680000.0;
constexpr double RecoveryGridHF = 47e-12;
constexpr double LeadOutputCoupling = 47e-9;
constexpr double LeadOutputSeries = 220000.0;
constexpr double LeadOutputBright = 330e-12;     // handwritten original
constexpr double LeadOutputShuntR = 100000.0;
constexpr double LeadOutputShuntC = 560e-12;
// In the Lead-Bright band C30 is effectively coupling V4A into the decoded
// 220K series / 100K shunt output network, so that network is part of V4A's AC
// plate load. The 330pF/560pF branches remain in LeadReturnMixer itself.
constexpr double V4ACathodeFeedbackPlateLoadOhms =
  Parallel(V4APlateOhms, LeadOutputSeries + LeadOutputShuntR);

// -----------------------------------------------------------------------------
// V2 / reverb / normalled FX-loop path.
// -----------------------------------------------------------------------------
constexpr double PreLoopPlateOhms = 100000.0;
constexpr double PreLoopCathodeOhms = 1500.0;
constexpr double RecoveryCoupling = 47e-9;
constexpr double RecoverySeriesR105 = 47000.0;
constexpr double ReverbBridgeR102 = 15000.0;
constexpr double FromReverbShuntR101 = 4700.0;
constexpr double ReturnShuntR103 = 47000.0;
constexpr double LeadMasterOhms = 250000.0;

// Reverb section values decoded in Pass 9. 0.10.1 instantiates V4B driver, spring
// boundary, V3A return and Reverb level in SpringReverb.h. The spring tank's
// mechanical constants are not present in Pass 9 and stay explicitly provisional.
constexpr double ReverbDriverCathodeBypass = 0.33e-6;
constexpr double ReverbDriverCathodeOhms = 1000.0;
constexpr double ReverbDriverPlateSupplyOhms = 10000.0;
constexpr double ReverbDriverPlateSupplyWatts = 2.0;
constexpr double ReverbFootswitchCathodeOhms = 15000.0;
constexpr double ReverbLevelOhms = 100000.0;
constexpr double ReverbV3ACathodeBypass = 15e-6;
constexpr double ReverbV3ACathodeOhms = 3300.0;
constexpr double ReverbV3AGridShuntOhms = 220000.0;
constexpr double ReverbV3AOutputCoupling = 0.01e-6;
constexpr double ReverbV3APlateFeedOhms = 100000.0;
constexpr double ReverbDriverPlateVolts = 339.0;
constexpr double ReverbV3ACathodeVolts = 2.6;
// MEDIUM-HIGH handwritten annotation: retain as uncertain model data only.
constexpr double ReverbDriverCathodeVoltsUncertain = 3.8;

// Post-loop V2 half -> Master / GEQ.
constexpr double PostLoopPlateOhms = 170000.0;
constexpr double DeepCathodeOhms = 1000.0;
constexpr double DeepFixedBypass = 0.47e-6;
constexpr double DeepSwitchedBypass = 15e-6;
constexpr double MasterCoupling = 47e-9;
constexpr double MasterSeries = 0.0; // no extra series resistor in handwritten source
constexpr double MasterOhms = 100000.0;
// The post-loop Deep stage is directly followed by the decoded 100K Master
// load through its .047uF coupling capacitor. Including the pot's total
// resistance in the AC plate load makes the Deep switch differential reflect
// the actual V2 -> Master topology while keeping Master itself downstream.
constexpr double PostLoopCathodeFeedbackPlateLoadOhms =
  Parallel(PostLoopPlateOhms, MasterOhms);


// -----------------------------------------------------------------------------
// Graphic EQ - handwritten Pass 9 values.  The frequency labels are the panel/
// schematic labels; the coupled passive branch resonances are not simply 1/(2π√LC).
// -----------------------------------------------------------------------------
constexpr double GraphicEqBand1CenterHz = 60.0;
constexpr double GraphicEqBand2CenterHz = 240.0;
constexpr double GraphicEqBand3CenterHz = 750.0;
constexpr double GraphicEqBand4CenterHz = 2200.0;
constexpr double GraphicEqBand5CenterHz = 6600.0;
constexpr double GraphicEqBand1L = 1.3;
constexpr double GraphicEqBand2L = 0.39;
constexpr double GraphicEqBand3L = 0.22;
constexpr double GraphicEqBand4L = 0.068;
constexpr double GraphicEqBand5L = 0.033;
constexpr double GraphicEqBand1C = 3.3e-6;
constexpr double GraphicEqBand2C = 0.47e-6;
constexpr double GraphicEqBand3C = 0.22e-6;
constexpr double GraphicEqBand4C = 0.15e-6;
constexpr double GraphicEqBand5C = 0.033e-6;
constexpr double GraphicEqBand1Series = 470.0;
constexpr double GraphicEqBand2Series = 470.0;
constexpr double GraphicEqBand3Series = 470.0;
constexpr double GraphicEqBand4Series = 1000.0;
constexpr double GraphicEqBand5Series = 1000.0;
constexpr double GraphicEqSliderOhms = 50000.0;
constexpr double GraphicEqSummingOhms = 3300.0;
constexpr double GraphicEqOutputCoupling = 47e-9;
constexpr double GraphicEqOutputShunt = 3300.0;
constexpr double GraphicEqQ1Q2Q4LocalOhms = 3300.0;
constexpr double GraphicEqSharedQ23Ohms = 22000.0;
constexpr double GraphicEqFeedbackCap = 10e-12;
constexpr double GraphicEqUpperRailOhms = 3300.0;
constexpr double GraphicEqBiasLoadOhms = 470000.0;

// -----------------------------------------------------------------------------
// LDR control-supply values. Pass 9 resolves the electrical control supply and
// LDR functions, but not the optocouplers' resistance-vs-light/time curves.
// -----------------------------------------------------------------------------
constexpr double LdrControlReservoir = 470e-6;
constexpr double LdrControlReservoirVolts = 16.0;
constexpr double LdrR88 = 680.0;
constexpr double LdrR89 = 475.0;
constexpr double LdrR90 = 3300.0;
constexpr double LdrR91 = 470.0;
constexpr double LdrR92 = 680.0;
constexpr double LdrR92Watts = 1.0;

// -----------------------------------------------------------------------------
// Power-supply values decoded on the handwritten power sheet. 0.10.1 uses the
// known B/C/D RC sections, reservoir/bleeder time constant and measured A/B/C/D
// rail differences for signal-dependent sag. The A-B choke inductance, rectifier
// source impedance and mains ripple are not decoded, so no fake L/ripple is added.
// -----------------------------------------------------------------------------
constexpr bool PowerABElementIsChoke = true;
constexpr double PowerDropBCOhms = 5600.0;
constexpr double PowerDropBCWatts = 2.0;
constexpr double PowerDropCDOhms = 1000.0;
constexpr double PowerDropCDWatts = 2.0;
constexpr double PowerBCDFilterCap = 30e-6;
constexpr double PowerBCDFilterCapVolts = 500.0;
constexpr double PowerReservoirBleederOhms = 150000.0;
constexpr double PowerReservoirBleederWatts = 1.0;
constexpr double PowerReservoirCap = 220e-6;
constexpr double PowerReservoirCapVolts = 350.0;

// -----------------------------------------------------------------------------
// Phase inverter / negative feedback / output stage.
// -----------------------------------------------------------------------------
constexpr double PhasePlatePositive = 82000.0;
constexpr double PhasePlateNegative = 91000.0;
constexpr double PhaseGridPositive = 100000.0;
constexpr double PhaseGridNegative = 150000.0;
constexpr double PhaseInterNode = 470.0;
constexpr double PhaseOutputCoupling = 0.1e-6;
constexpr double PhaseNegativePlateHF = 120e-12;
// Existing reduced-model PI input high-pass proxy. Pass 9 does not decode a
// distinct value for this simplified boundary, so do not present it as a new
// handwritten component.
constexpr double PowerPrototypeInputCoupling = 0.1e-6;

constexpr double PresencePotOhms = 250000.0;
constexpr double PresenceSeries = 3300.0;
constexpr double FeedbackSeries = 56000.0;
constexpr double FeedbackSeriesCap = 5.0e-9; // handwritten .005 uF
constexpr double FeedbackToPI = 22000.0;
constexpr double TailSeries = 1500.0;
constexpr double TailShunt = 3300.0;
constexpr double LowerFeedbackCapA = 47e-9;
constexpr double LowerFeedbackCapB = 0.1e-6;
// Compatibility alias for older call sites; the physical shunt uses A+B in parallel.
constexpr double TailShuntCap = LowerFeedbackCapA;
constexpr double FeedbackTapOhms = 8.0;

constexpr double PowerGridStopperOhms = 2200.0;
constexpr double PowerGridLeakOhms = 220000.0;
constexpr double PowerOuterShuntOhms = 680000.0;
constexpr double PowerScreenOhms = 470.0;
constexpr double DirectOutputFeedOhms = 4700.0;
constexpr double PowerScreenResistorWatts = 2.0;

// AC-heater artificial center tap.
constexpr double HeaterArtificialCenterTapA = 100.0;
constexpr double HeaterArtificialCenterTapB = 100.0;

// Power-sheet annotations used by the reduced power model where applicable.
constexpr double Bias60Volts = 47.0;
constexpr double Bias100Volts = 52.0;
constexpr double BiasSimulVolts = 67.0;
constexpr double Bias60ExportVolts = 47.0;
constexpr double Bias100ExportVolts = 47.0;
constexpr double BiasSimulExportVolts = 55.0;
constexpr double PowerOuterPlateVolts = 478.0;
constexpr double PowerInnerTapVolts = 430.0;

// -----------------------------------------------------------------------------
// Reduced 12AX7 model constants.
// -----------------------------------------------------------------------------
// Norman Koren 12AX7 plate-current parameters.  Load-line tables are additionally
// anchored to Pass-9 handwritten DC cathode annotations where those exist.
constexpr double TriodeMu = 100.0;
constexpr double TriodeEx = 1.4;
constexpr double TriodeKg1 = 1060.0;
constexpr double TriodeKp = 600.0;
constexpr double TriodeKvb = 300.0;
constexpr double TriodeRpEstimate = 62500.0;
constexpr double TriodeMuEstimate = 100.0;
}
