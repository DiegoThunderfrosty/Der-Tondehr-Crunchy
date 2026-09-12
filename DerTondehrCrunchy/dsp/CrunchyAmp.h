#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include "HIIR/FPUUpsampler2x.h"
#include "HIIR/FPUDownsampler2x.h"
#include "PassiveToneStack.h"
#include "CircuitFilters.h"
#include "GraphicEqCircuit.h"
#include "ControlLogic.h"
#include "SpringReverb.h"

namespace crunchy {
constexpr double kPi = 3.14159265358979323846;
inline double Db(double dB) { return std::pow(10.0, dB / 20.0); }
inline double Mix(double a, double b, double t) { return a + (b - a) * t; }
inline double Taper(double value) { return circuit::AudioTaper(value); }

// 0.10.9 I/O calibration
// ----------------------
// The panel remains a conventional -24..+24 dB trim around a *displayed* 0 dB,
// but the user's chosen working calibration is intentionally hidden underneath:
//   INPUT  displayed 0 dB == the previous +12 dB trim position internally.
//   OUTPUT displayed 0 dB == the previous -2.5 dB trim position internally.
// This preserves the exact sound of those 0.10.8 knob positions while making
// them the new ergonomic zero points. The original electrical reference scales
// from 0.10.8 are retained; only the user-facing trim origin is translated.
constexpr double kInputReferenceDbu = 9.0;
constexpr double kDbuRmsVolts = 0.775;
constexpr double kOutputReferenceParamDb = -6.0;
constexpr double kOutputReferenceFixedScale = 0.5;
constexpr double kInputDisplayZeroOffsetDb = 12.0;
constexpr double kOutputDisplayZeroOffsetDb = -2.5;
constexpr double kDigitalClipThreshold = 1.0; // 0 dBFS sample peak
inline double InputUserTrimScale(double displayedTrimDb) {
  return Db(displayedTrimDb + kInputDisplayZeroOffsetDb);
}
inline double InputTrimScale(double displayedTrimDb) {
  return kDbuRmsVolts * std::sqrt(2.0) * Db(kInputReferenceDbu) *
         InputUserTrimScale(displayedTrimDb);
}
inline double OutputTrimScale(double displayedTrimDb) {
  return kOutputReferenceFixedScale * Db(kOutputReferenceParamDb) *
         Db(displayedTrimDb + kOutputDisplayZeroOffsetDb);
}
inline bool IsDigitalSampleClipped(double sample) {
  return std::isfinite(sample) && std::abs(sample) >= kDigitalClipThreshold;
}

// Append new parameters: these IDs are part of the saved host state.
enum Param {
  Volume, Treble, Bass, Middle, Master, LeadDrive, LeadMaster,
  Eq80, Eq240, Eq750, Eq2200, Eq6600, InputCalibration, EqGain, OutputGain,
  RhythmBright, TrebleShift, BassShift, Lead, LeadBright, Deep, EqAuto, EqIn,
  Bypass, Presence, PowerMode, Reverb, ReverbOn, LaterSimul20p, ExportBias,
  EqMode, Oversampling, NumParams
};
using Parameters = std::array<double, NumParams>;
inline Parameters Defaults() {
  // Shared VST3/Standalone startup defaults. The seven main amplifier knobs use
  // 5.00 as their real parameter default, so iPlug2's built-in double-click
  // reset returns each of them to 5.00 in both formats.
  return {5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0,
          controls::GraphicEqStartupDb[0], controls::GraphicEqStartupDb[1], controls::GraphicEqStartupDb[2],
          controls::GraphicEqStartupDb[3], controls::GraphicEqStartupDb[4], 0.0, 0.0, 0.0,
          0, 0, 0, 0, 0, 0, 0, 0, 0, 5.0, 1.0,
          0.0, 0.0, 0.0, 0.0, 1.0, 0.0};
}

// Only the Standalone startup state differs from the shared defaults: it opens
// bypassed for safe monitoring/device setup. VST3 remains active on insertion.
// Keeping this as a pure helper prevents the DSP defaults from depending on an
// API compile macro and makes the APP/VST3 distinction regression-testable.
inline Parameters StartupDefaults(bool standalone) {
  auto values = Defaults();
  values[Bypass] = standalone ? 1.0 : 0.0;
  return values;
}
inline Parameters Sanitize(Parameters values) {
  const auto defaults = Defaults();
  for (int i = 0; i < NumParams; ++i) {
    if (!std::isfinite(values[i])) values[i] = defaults[i];
    if (i <= LeadMaster || i == Presence || i == Reverb) values[i] = std::clamp(values[i], 0.0, 10.0);
    else if ((i >= Eq80 && i <= Eq6600) || i == EqGain) values[i] = std::clamp(values[i], -12.0, 12.0);
    else if (i == InputCalibration) values[i] = std::clamp(values[i], -24.0, 24.0);
    else if (i == OutputGain) values[i] = std::clamp(values[i], -24.0, 24.0);
    else if (i == EqMode) values[i] = std::clamp(std::round(values[i]), 0.0, 2.0);
    else if (i == Oversampling) values[i] = std::clamp(std::round(values[i]), 0.0, 3.0);
    else values[i] = values[i] >= 0.5 ? 1.0 : 0.0;
  }
  return values;
}

inline int OversamplingFactor(double mode) {
  const int index = std::clamp(static_cast<int>(std::lround(mode)), 0, 3);
  return 1 << index; // 0/1/2/3 -> 1x/2x/4x/8x
}

class OnePole {
public:
  void Set(double hz, double rate) { a = std::exp(-2.0 * kPi * hz / rate); }
  double Low(double x) { z = x + a * (z - x); return z; }
  double High(double x) { return x - Low(x); }
  void Reset() { z = 0; }
private:
  double a = 0, z = 0;
};

// Pass-9 four-tube Simul-Class power stage. The handwritten values anchor
// the PI loading, grid networks, bias magnitude, supply rails and screen/grid
// resistors. The 6L6/transformer nonlinear curves remain a real-time behavioral
// approximation because Pass 9 does not provide tube characteristic curves or
// output-transformer primary impedance/leakage data.
class PowerSimulClassStage {
public:
  void Prepare(double rate) {
    sampleRate = rate;
    transformerFullLow.Set(14000.0, rate);
    transformerClassALow.Set(12000.0, rate);
    transformerFullDC.Set(18.0, rate);
    transformerClassADC.Set(32.0, rate);

    // Reduced PI input boundary. Pass 9 resolves both grid/load legs (100K and
    // 150K) plus their 470-ohm inter-node resistor. The exact preceding coupling
    // part is not separately decoded, so the existing 0.1uF boundary proxy is
    // retained, but its load now includes the complete decoded PI reference
    // network instead of silently ignoring the V5B/470-ohm side.
    constexpr double piReferenceLoad = circuit::Parallel(circuit::PhaseGridPositive,
      circuit::PhaseGridNegative + circuit::PhaseInterNode);
    inputCoupling.Set(1.0 / (2 * kPi * piReferenceLoad * circuit::PowerPrototypeInputCoupling), rate);

    constexpr double innerGrid = circuit::PowerGridLeakOhms + circuit::PowerGridStopperOhms;
    constexpr double outerLeak = circuit::Parallel(circuit::PowerGridLeakOhms, circuit::PowerOuterShuntOhms);
    constexpr double outerGrid = outerLeak + circuit::PowerGridStopperOhms;
    innerPositiveCoupling.Set(1.0 / (2 * kPi * innerGrid * circuit::PhaseOutputCoupling), rate);
    innerNegativeCoupling.Set(1.0 / (2 * kPi * innerGrid * circuit::PhaseOutputCoupling), rate);
    outerPositiveCoupling.Set(1.0 / (2 * kPi * outerGrid * circuit::PhaseOutputCoupling), rate);
    outerNegativeCoupling.Set(1.0 / (2 * kPi * outerGrid * circuit::PhaseOutputCoupling), rate);

    const double negativePlateLoad = circuit::Parallel(circuit::PhasePlateNegative, innerGrid);
    phaseNegativeHF.Set(1.0 / (2 * kPi * negativePlateLoad * circuit::PhaseNegativePlateHF), rate);

    // Known RC sections of the B/C/D supply. No inductance is decoded for the
    // A-B choke, so the model uses its measured A/B rail difference but does
    // not invent a choke L value or resonance.
    const double tauBC = circuit::PowerDropBCOhms * circuit::PowerBCDFilterCap;
    const double tauCD = circuit::PowerDropCDOhms * circuit::PowerBCDFilterCap;
    const double tauReservoir = circuit::PowerReservoirBleederOhms * circuit::PowerReservoirCap;
    supplyBC.Set(1.0 / (2 * kPi * tauBC), rate);
    supplyCD.Set(1.0 / (2 * kPi * tauCD), rate);
    reservoirAverage.Set(1.0 / (2 * kPi * tauReservoir), rate);

    // Presence is physically a negative-feedback control.  Pass 9 gives the
    // passive network but not the exact output-transformer loop gain.  Keep the
    // decoded network and add a calibrated upper-mid/high shelf around it so the
    // 0..10 panel range is audibly useful instead of collapsing to a sub-dB
    // difference in the reduced transformer model.
    presenceShelf.Set(1800.0, rate);
    feedbackNetwork.Configure(5.0, rate);
    Configure(5.0, rate);
    BuildModeCache();
    transferTables = &GetTransferTables(); // build once, outside the realtime path
    Reset();
  }

  void Reset() {
    transformerFullLow.Reset(); transformerClassALow.Reset();
    transformerFullDC.Reset(); transformerClassADC.Reset();
    inputCoupling.Reset(); innerPositiveCoupling.Reset(); innerNegativeCoupling.Reset();
    outerPositiveCoupling.Reset(); outerNegativeCoupling.Reset(); phaseNegativeHF.Reset();
    supplyBC.Reset(); supplyCD.Reset(); reservoirAverage.Reset(); feedbackNetwork.Reset();
    presenceShelf.Reset();
    feedbackVolts = 0.0;
    transformerRegion = -1;
    innerPathActive = true;
  }

  void Configure(double presence, double rate) {
    const double p = std::clamp(presence / 10.0, 0.0, 1.0);
    // The passive network remains authoritative.  ±3.25 dB is a perceptual
    // calibration for the transformer/tube loop-gain data that Pass 9 does not
    // contain; Presence 5 remains neutral.
    constexpr double halfRangeDb = 3.25;
    const double shelfDb = (2.0 * p - 1.0) * halfRangeDb;
    presenceHighGain = std::pow(10.0, shelfDb / 20.0);
    feedbackNetwork.Configure(presence, rate);
  }

  // simulAmount: 0 = CLASS A (outer V8/V9 only), 1 = SIMUL-CLASS (all four).
  // exportAmount selects the Pass-9 export Simul bias magnitude (-55V instead
  // of -67V). It is a hardware-variant option, not a front-panel circuit part.
  double Process(double input, double simulAmount, double exportAmount) {
    constexpr double endpoint = 1.0e-9;
    const double simulClamped = std::clamp(simulAmount, 0.0, 1.0);
    const double exportClamped = std::clamp(exportAmount, 0.0, 1.0);
    const bool simulEndpoint = simulClamped <= endpoint || simulClamped >= 1.0 - endpoint;
    const bool exportEndpoint = exportClamped <= endpoint || exportClamped >= 1.0 - endpoint;
    const int simulIndex = simulClamped >= 0.5 ? 1 : 0;
    const int exportIndex = exportClamped >= 0.5 ? 1 : 0;
    const double simul = simulEndpoint ? static_cast<double>(simulIndex) : simulClamped;
    const double exportBias = exportEndpoint ? static_cast<double>(exportIndex) : exportClamped;
    const ModeCache* cached = (simulEndpoint && exportEndpoint) ? &modeCache[simulIndex * 2 + exportIndex] : nullptr;

    constexpr double innerGrid = circuit::PowerGridLeakOhms + circuit::PowerGridStopperOhms;
    constexpr double outerLeak = circuit::Parallel(circuit::PowerGridLeakOhms, circuit::PowerOuterShuntOhms);
    constexpr double outerGrid = outerLeak + circuit::PowerGridStopperOhms;

    double positivePhaseGain, negativePhaseGain, speakerPeakVolts, invBiasMagnitude, loadDriveScale;
    if (cached) {
      positivePhaseGain = cached->positivePhaseGain;
      negativePhaseGain = cached->negativePhaseGain;
      speakerPeakVolts = cached->speakerPeakVolts;
      invBiasMagnitude = cached->invBiasMagnitude;
      loadDriveScale = cached->loadDriveScale;
    } else {
      const double activeGridLoad = Mix(outerGrid, circuit::Parallel(innerGrid, outerGrid), simul);
      const double positiveLoad = circuit::Parallel(circuit::PhasePlatePositive, activeGridLoad);
      const double negativeLoad = circuit::Parallel(circuit::PhasePlateNegative, activeGridLoad);
      positivePhaseGain = circuit::TriodeMuEstimate * positiveLoad /
        (2.0 * (circuit::TriodeRpEstimate + positiveLoad));
      negativePhaseGain = circuit::TriodeMuEstimate * negativeLoad /
        (2.0 * (circuit::TriodeRpEstimate + negativeLoad));
      const double watts = Mix(25.0, 75.0, simul);
      constexpr double loadedSpeakerOhms = circuit::Parallel(circuit::FeedbackTapOhms,
        circuit::DirectOutputFeedOhms);
      speakerPeakVolts = std::sqrt(2.0 * loadedSpeakerOhms * watts);
      const double biasMagnitude = Mix(circuit::BiasSimulVolts, circuit::BiasSimulExportVolts, exportBias);
      invBiasMagnitude = 1.0 / std::max(1.0, biasMagnitude);
      loadDriveScale = Mix(0.58, 1.0, simul) * Mix(1.0, 1.16, exportBias);
    }

    const double gridVolts = inputCoupling.High(input) - feedbackVolts;
    const double phaseP = gridVolts * positivePhaseGain * invBiasMagnitude;
    const double phaseN = gridVolts * negativePhaseGain * invBiasMagnitude;

    const double rawDrive = std::clamp(0.5 * (std::abs(phaseP) + std::abs(phaseN)), 0.0, 2.0);
    const double loadDrive = rawDrive * loadDriveScale;
    const double bc = std::clamp(supplyBC.Low(loadDrive), 0.0, 1.5);
    const double cd = std::clamp(supplyCD.Low(loadDrive), 0.0, 1.5);
    const double longAverage = std::clamp(reservoirAverage.Low(loadDrive), 0.0, 1.5);

    const double outerDroopV = (circuit::PowerSheetSupplyA - circuit::PowerSheetSupplyB) * bc;
    const double innerDroopV = (circuit::PowerSheetSupplyB - circuit::PowerSheetSupplyC) * bc +
                               (circuit::PowerSheetSupplyC - circuit::PowerSheetSupplyD) * cd;
    const double reservoirDroopV = 4.0 * longAverage;
    const double outerRailScale = std::clamp((circuit::PowerOuterPlateVolts - outerDroopV - reservoirDroopV) /
                                              circuit::PowerOuterPlateVolts, 0.65, 1.02);
    const double innerRailScale = std::clamp((circuit::PowerInnerTapVolts - innerDroopV - reservoirDroopV) /
                                              circuit::PowerInnerTapVolts, 0.65, 1.02);

    const double positivePlate = phaseP;
    const double negativePlate = phaseNegativeHF.Low(-phaseN);

    // In settled CLASS A the inner AB pair is physically absent. Keep it asleep
    // until a mode transition actually needs it again.
    double inner = 0.0;
    const bool needInner = !simulEndpoint || simulIndex == 1;
    if (needInner) {
      if (!innerPathActive) {
        innerPositiveCoupling.Reset(); innerNegativeCoupling.Reset();
        innerPathActive = true;
      }
      const double innerP = innerPositiveCoupling.High(positivePlate * innerRailScale);
      const double innerN = innerNegativeCoupling.High(negativePlate * innerRailScale);
      constexpr double screenRatio = circuit::PowerScreenOhms /
        (circuit::PowerScreenOhms + circuit::PowerGridStopperOhms);
      const double screenCompression = 1.0 / (1.0 + screenRatio * bc);
      if (exportEndpoint)
        inner = exportIndex ? ClassABPairHot(innerP * screenCompression, innerN * screenCompression)
                            : ClassABPairCold(innerP * screenCompression, innerN * screenCompression);
      else
        inner = ClassABPair(innerP * screenCompression, innerN * screenCompression, exportBias);
    } else if (innerPathActive) {
      innerPositiveCoupling.Reset(); innerNegativeCoupling.Reset();
      innerPathActive = false;
    }

    const double outerP = outerPositiveCoupling.High(positivePlate * outerRailScale);
    const double outerN = outerNegativeCoupling.High(negativePlate * outerRailScale);
    const double outer = exportEndpoint
      ? (exportIndex ? ClassATriodePairHot(outerP, outerN) : ClassATriodePairCold(outerP, outerN))
      : ClassATriodePair(outerP, outerN, exportBias);

    const double full = (2.0 / 3.0) * inner + (1.0 / 3.0) * outer;
    const double classA = kClassAVoltageScale * outer;

    double out;
    if (simulEndpoint && simulIndex == 1) {
      // If we arrived through the morph region the full-band path is already
      // warm; do not reset it again at the endpoint.
      if (transformerRegion == 0) { transformerFullLow.Reset(); transformerFullDC.Reset(); }
      transformerRegion = 1;
      out = transformerFullDC.High(transformerFullLow.Low(full));
    } else if (simulEndpoint && simulIndex == 0) {
      if (transformerRegion == 1) { transformerClassALow.Reset(); transformerClassADC.Reset(); }
      transformerRegion = 0;
      out = transformerClassADC.High(transformerClassALow.Low(classA));
    } else {
      if (transformerRegion == 0) { transformerFullLow.Reset(); transformerFullDC.Reset(); }
      else if (transformerRegion == 1) { transformerClassALow.Reset(); transformerClassADC.Reset(); }
      transformerRegion = 2;
      const double fullBand = transformerFullDC.High(transformerFullLow.Low(full));
      const double classABand = transformerClassADC.High(transformerClassALow.Low(classA));
      out = Mix(classABand, fullBand, simul);
    }

    double biasVoicedOut;
    if (exportEndpoint) {
      biasVoicedOut = exportIndex ? (0.88 * std::tanh(1.48 * out)) : out;
    } else {
      const double hotBiasOut = 0.88 * std::tanh(1.48 * out);
      biasVoicedOut = Mix(out, hotBiasOut, exportBias);
    }

    const double lowBand = presenceShelf.Low(biasVoicedOut);
    const double voicedOut = lowBand + (biasVoicedOut - lowBand) * presenceHighGain;

    constexpr double feedbackLoopCalibration = 1.45;
    feedbackVolts = feedbackLoopCalibration * feedbackNetwork.Process(voicedOut * speakerPeakVolts);
    return voicedOut;
  }

private:
  struct ModeCache {
    double positivePhaseGain = 1.0;
    double negativePhaseGain = 1.0;
    double speakerPeakVolts = 1.0;
    double invBiasMagnitude = 1.0;
    double loadDriveScale = 1.0;
  };

  void BuildModeCache() {
    constexpr double innerGrid = circuit::PowerGridLeakOhms + circuit::PowerGridStopperOhms;
    constexpr double outerLeak = circuit::Parallel(circuit::PowerGridLeakOhms, circuit::PowerOuterShuntOhms);
    constexpr double outerGrid = outerLeak + circuit::PowerGridStopperOhms;
    constexpr double loadedSpeakerOhms = circuit::Parallel(circuit::FeedbackTapOhms,
      circuit::DirectOutputFeedOhms);
    for (int sim = 0; sim < 2; ++sim) {
      for (int ex = 0; ex < 2; ++ex) {
        auto& c = modeCache[sim * 2 + ex];
        const double s = static_cast<double>(sim);
        const double e = static_cast<double>(ex);
        const double activeGridLoad = Mix(outerGrid, circuit::Parallel(innerGrid, outerGrid), s);
        const double positiveLoad = circuit::Parallel(circuit::PhasePlatePositive, activeGridLoad);
        const double negativeLoad = circuit::Parallel(circuit::PhasePlateNegative, activeGridLoad);
        c.positivePhaseGain = circuit::TriodeMuEstimate * positiveLoad /
          (2.0 * (circuit::TriodeRpEstimate + positiveLoad));
        c.negativePhaseGain = circuit::TriodeMuEstimate * negativeLoad /
          (2.0 * (circuit::TriodeRpEstimate + negativeLoad));
        c.speakerPeakVolts = std::sqrt(2.0 * loadedSpeakerOhms * Mix(25.0, 75.0, s));
        c.invBiasMagnitude = 1.0 / std::max(1.0,
          Mix(circuit::BiasSimulVolts, circuit::BiasSimulExportVolts, e));
        c.loadDriveScale = Mix(0.58, 1.0, s) * Mix(1.0, 1.16, e);
      }
    }
  }

  static double SmoothConduction(double v) {
    constexpr double knee = 0.12;
    const double z = v / knee;
    if (z > 40.0) return v;
    if (z < -40.0) return knee * std::exp(z);
    return knee * std::log1p(std::exp(z));
  }

  static double ClassABPair(double positiveDrive, double negativeDrive, double hotBias) {
    const double h = std::clamp(hotBias, 0.0, 1.0);
    // Hotter (-55V) bias keeps the pair conducting further through the
    // crossover region, drives it a little harder and reaches compression
    // earlier.  The endpoints are calibration parameters around the decoded
    // -67/-55V values, not claimed 6L6 transfer-curve measurements.
    const double conductionBias = Mix(0.32, 0.48, h);
    const double driveCalibration = Mix(1.12, 1.30, h);
    const double headroom = Mix(1.80, 1.56, h);
    const double p = headroom * std::tanh((positiveDrive * driveCalibration) / headroom);
    const double n = headroom * std::tanh((negativeDrive * driveCalibration) / headroom);
    const double positive = std::tanh(SmoothConduction(p + conductionBias));
    const double negative = std::tanh(SmoothConduction(n + conductionBias));
    return positive - negative;
  }

  static double ClassATriodePair(double positiveDrive, double negativeDrive, double hotBias) {
    // Both halves remain conducting through the cycle; no class-B cutoff
    // operator is used. The softer triode-wired saturation is behavioral, while
    // pair selection and outer-tube loading/rail values come from Pass 9/manual.
    const double h = std::clamp(hotBias, 0.0, 1.0);
    const double drive = Mix(0.92, 1.06, h);
    const double headroom = Mix(1.45, 1.30, h);
    const double p = headroom * std::tanh((positiveDrive * drive) / headroom);
    const double n = headroom * std::tanh((negativeDrive * drive) / headroom);
    return Mix(0.90, 0.96, h) * (std::tanh(p) - std::tanh(n));
  }


  static double ExactClassABHalfCold(double drive) {
    constexpr double conductionBias = 0.32;
    constexpr double driveCalibration = 1.12;
    constexpr double headroom = 1.80;
    const double p = headroom * std::tanh((drive * driveCalibration) / headroom);
    return std::tanh(SmoothConduction(p + conductionBias));
  }
  static double ExactClassABHalfHot(double drive) {
    constexpr double conductionBias = 0.48;
    constexpr double driveCalibration = 1.30;
    constexpr double headroom = 1.56;
    const double p = headroom * std::tanh((drive * driveCalibration) / headroom);
    return std::tanh(SmoothConduction(p + conductionBias));
  }
  double ClassABPairCold(double positiveDrive, double negativeDrive) const {
    return LookupTransfer(transferTables->abCold, positiveDrive, ExactClassABHalfCold) -
           LookupTransfer(transferTables->abCold, negativeDrive, ExactClassABHalfCold);
  }
  double ClassABPairHot(double positiveDrive, double negativeDrive) const {
    return LookupTransfer(transferTables->abHot, positiveDrive, ExactClassABHalfHot) -
           LookupTransfer(transferTables->abHot, negativeDrive, ExactClassABHalfHot);
  }
  static double ExactClassAHalfCold(double drive) {
    constexpr double scale = 0.92;
    constexpr double headroom = 1.45;
    const double p = headroom * std::tanh((drive * scale) / headroom);
    return 0.90 * std::tanh(p);
  }
  static double ExactClassAHalfHot(double drive) {
    constexpr double scale = 1.06;
    constexpr double headroom = 1.30;
    const double p = headroom * std::tanh((drive * scale) / headroom);
    return 0.96 * std::tanh(p);
  }
  double ClassATriodePairCold(double positiveDrive, double negativeDrive) const {
    return LookupTransfer(transferTables->aCold, positiveDrive, ExactClassAHalfCold) -
           LookupTransfer(transferTables->aCold, negativeDrive, ExactClassAHalfCold);
  }
  double ClassATriodePairHot(double positiveDrive, double negativeDrive) const {
    return LookupTransfer(transferTables->aHot, positiveDrive, ExactClassAHalfHot) -
           LookupTransfer(transferTables->aHot, negativeDrive, ExactClassAHalfHot);
  }
  static constexpr int kTransferTableSize = 16385;
  static constexpr double kTransferTableMin = -8.0;
  static constexpr double kTransferTableMax = 8.0;
  static constexpr double kTransferTableScale =
    static_cast<double>(kTransferTableSize - 1) / (kTransferTableMax - kTransferTableMin);
  using TransferTable = std::array<double, kTransferTableSize>;
  struct TransferTables {
    TransferTable abCold{}, abHot{}, aCold{}, aHot{};
  };

  static const TransferTables& GetTransferTables() {
    static const TransferTables tables = [] {
      TransferTables t;
      for (int i = 0; i < kTransferTableSize; ++i) {
        const double x = kTransferTableMin +
          (kTransferTableMax - kTransferTableMin) * static_cast<double>(i) /
          static_cast<double>(kTransferTableSize - 1);
        t.abCold[i] = ExactClassABHalfCold(x);
        t.abHot[i] = ExactClassABHalfHot(x);
        t.aCold[i] = ExactClassAHalfCold(x);
        t.aHot[i] = ExactClassAHalfHot(x);
      }
      return t;
    }();
    return tables;
  }

  static double LookupTransfer(const TransferTable& table, double x, double (*fallback)(double)) {
    if (x <= kTransferTableMin || x >= kTransferTableMax)
      return fallback(x);
    const double pos = (x - kTransferTableMin) * kTransferTableScale;
    const int i = static_cast<int>(pos);
    const double frac = pos - static_cast<double>(i);
    return table[i] + (table[i + 1] - table[i]) * frac;
  }

  static constexpr double kClassAVoltageScale = 0.57735026918962576451; // sqrt(25/75)
  std::array<ModeCache, 4> modeCache{};
  const TransferTables* transferTables = nullptr;
  int transformerRegion = -1; // 0=Class A endpoint, 1=Simul endpoint, 2=morph
  bool innerPathActive = true;
  double sampleRate = 384000.0;
  OnePole transformerFullLow, transformerClassALow, transformerFullDC, transformerClassADC;
  OnePole inputCoupling, innerPositiveCoupling, innerNegativeCoupling;
  OnePole outerPositiveCoupling, outerNegativeCoupling, phaseNegativeHF;
  OnePole supplyBC, supplyCD, reservoirAverage, presenceShelf;
  PresenceNetwork feedbackNetwork;
  double feedbackVolts = 0.0;
  double presenceHighGain = 1.0;
};

class AmpChannel {
public:
  void Prepare(double sampleRate, const Parameters& params) {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0) sampleRate = 48000.0;
    baseRate = sampleRate;
    current = target = Sanitize(params);
    oversamplingMode = std::clamp(static_cast<int>(std::lround(current[Oversampling])), 0, 3);
    pendingOversamplingMode = oversamplingMode;
    oversamplingFactor = OversamplingFactor(current[Oversampling]);

    // The nonlinear 12AX7 lookup tables do not depend on sample rate. Build them
    // once at host reset, not every time the user changes oversampling.
    constexpr double plates[] = {circuit::V1APlateOhms, circuit::V1BPlateOhms, circuit::V3BPlateOhms,
      circuit::V4APlateOhms, circuit::PreLoopPlateOhms, circuit::PostLoopPlateOhms};
    constexpr double cathodeR[] = {circuit::V1ACathodeOhms, circuit::V1BCathodeOhms, circuit::V3BCathodeOhms,
      circuit::V4ACathodeOhms, circuit::PreLoopCathodeOhms, circuit::DeepCathodeOhms};
    constexpr double supplies[] = {circuit::PreampSupplyD, circuit::PreampSupplyD,
      circuit::PreampSupplyC, circuit::PreampSupplyC, circuit::PreampSupplyC, circuit::PreampSupplyC};
    constexpr double measuredVk[] = {circuit::V1ACathodeVolts, circuit::V1BCathodeVolts, 0.0,
      circuit::V4ACathodeVolts, 0.0, 0.0};
    for (int i = 0; i < 6; ++i)
      stages[i].Prepare(supplies[i], plates[i], cathodeR[i], measuredVk[i]);

    // Reverb is intentionally post-distortion, so it belongs at the HOST rate,
    // not inside the nonlinear oversampling loop. This is both more faithful to
    // the requested routing and dramatically cheaper at 2x/4x/8x.
    reverb.Prepare(baseRate);
    outputDC.Set(10.0, baseRate);
    outputDC.Reset();

    PrepareHighRatePath(oversamplingFactor);
    parametersMoving = false;
    osTransitionState = 0;
    osWetMix = 1.0;
    osFadeStep = 1.0 / std::max(1.0, 0.003 * baseRate); // 3 ms dry bridge per side
  }

  void SetParameters(const Parameters& params) {
    const Parameters next = Sanitize(params);
    pendingOversamplingMode = std::clamp(static_cast<int>(std::lround(next[Oversampling])), 0, 3);

    bool needsSmoothing = false;
    for (int i = 0; i < NumParams; ++i) {
      if (i == Oversampling) continue;
      if (std::abs(next[i] - target[i]) > 1.0e-12) {
        target[i] = next[i];
        if (i == EqMode) current[i] = target[i]; // discrete 3-position hardware selector
        else needsSmoothing = true;
      }
    }
    target[Oversampling] = next[Oversampling];
    current[Oversampling] = target[Oversampling];
    parametersMoving = parametersMoving || needsSmoothing;

    if (pendingOversamplingMode != oversamplingMode && osTransitionState == 0)
      osTransitionState = 1; // fade processed signal to dry before rebuilding HR state
  }

  int CurrentOversamplingFactor() const { return oversamplingFactor; }

  double Process(double input) {
    if (!std::isfinite(input)) input = 0.0;
    const double dry = input;

    // Once bypass is fully settled there is no reason to spend CPU on the amp.
    // Apply any pending oversampling change while silent/bypassed, then sleep.
    if (target[Bypass] >= 1.0 && current[Bypass] >= 1.0 - 1.0e-9 && !parametersMoving) {
      if (pendingOversamplingMode != oversamplingMode)
        ApplyOversamplingMode(pendingOversamplingMode);
      return dry;
    }

    input = std::clamp(input, -32.0, 32.0);
    const double distorted = ProcessOversampled(input);

    // POST-DISTORTION spring reverb at host rate. At Reverb=0 / F/S OFF the
    // reverb class itself sleeps and returns immediately.
    const double wetReverb = reverb.Process(distorted, current[Reverb], current[ReverbOn]);
    const double wet = outputDC.High(distorted + wetReverb) * outputScale;

    // Oversampling changes are bridged through dry for 3 ms -> rebuild -> 3 ms
    // back to wet. This avoids an abrupt recursive-state discontinuity without
    // keeping four full amp engines alive all the time.
    double transitioned = wet;
    if (osTransitionState == 1) {
      osWetMix = std::max(0.0, osWetMix - osFadeStep);
      transitioned = Mix(dry, wet, osWetMix);
      if (osWetMix <= 0.0) {
        ApplyOversamplingMode(pendingOversamplingMode);
        osTransitionState = 2;
      }
    } else if (osTransitionState == 2) {
      osWetMix = std::min(1.0, osWetMix + osFadeStep);
      transitioned = Mix(dry, wet, osWetMix);
      if (osWetMix >= 1.0) {
        osTransitionState = 0;
        if (pendingOversamplingMode != oversamplingMode)
          osTransitionState = 1;
      }
    }

    return Mix(transitioned, dry, current[Bypass]);
  }

private:
  static bool ParamNeedsFilterUpdate(int i) {
    return (i >= Volume && i <= Master) || i == LeadDrive || i == LeadMaster ||
           (i >= Eq80 && i <= OutputGain) || i == RhythmBright || i == TrebleShift ||
           i == Lead || i == Presence;
  }

  void SetOversamplerCoefficients() {
    // Coefficients from iPlug2 IPlug/Extras/OverSampler.h (see THIRD_PARTY.md).
    static constexpr double c2[12] = {0.036681502163648017, 0.13654762463195794,
      0.27463175937945444, 0.42313861743656711, 0.56109869787919531,
      0.67754004997416184, 0.76974183386322703, 0.83988962484963892,
      0.89226081800387902, 0.9315419599631839, 0.96209454837808417, 0.98781637073289585};
    static constexpr double c4[4] = {0.041893991997656171, 0.16890348243995201,
      0.39056077292116603, 0.74389574826847926};
    static constexpr double c8[3] = {0.055748680811302048, 0.24305119574153072,
      0.64669913119268196};
    up2.set_coefs(c2); down2.set_coefs(c2);
    up4.set_coefs(c4); down4.set_coefs(c4);
    up8.set_coefs(c8); down8.set_coefs(c8);
  }

  void ClearOversamplers() {
    up2.clear_buffers(); up4.clear_buffers(); up8.clear_buffers();
    down8.clear_buffers(); down4.clear_buffers(); down2.clear_buffers();
  }

  void PrepareHighRatePath(int factor) {
    oversamplingFactor = std::clamp(factor, 1, 8);
    rate = baseRate * static_cast<double>(oversamplingFactor);

    inputDC.Set(8.0, rate);
    simulHfAudibility.Set(3200.0, rate);
    inputDC.Reset(); simulHfAudibility.Reset();
    power.Prepare(rate);
    toneStack.Prepare(rate);
    middleAudibility.Reset();
    middleAudibilityActive = false;
    eq.Prepare(rate);

    constexpr double rp = circuit::TriodeRpEstimate;
    for (auto& c : cathodes) c.Reset();
    cathodes[1].Configure(circuit::V1BPlateOhms, circuit::V1BCathodeOhms, circuit::V1BCathodeBypass, 0, 0, rate);
    cathodes[2].Configure(circuit::V3BPlateOhms, circuit::V3BCathodeOhms, circuit::V3BCathodeBypass, 0, 0, rate);
    cathodes[4].Configure(circuit::PreLoopPlateOhms, circuit::PreLoopCathodeOhms, 0, 0, 0, rate);
    bassShiftCathode.Prepare(circuit::V1APlateOhms, circuit::V1ACathodeFeedbackPlateLoadOhms,
      circuit::V1ACathodeOhms, circuit::V1AFixedCathodeBypass,
      circuit::V1ALargeCathodeBypass, circuit::BassShiftSeriesOhms, rate);
    leadBrightCathode.Prepare(circuit::LeadPlateOhms, circuit::V4ACathodeFeedbackPlateLoadOhms,
      circuit::LeadCathodeOhms, 0, circuit::LeadBrightBypass,
      circuit::LeadBrightReturnOhms, rate);
    deepCathode.Prepare(circuit::PostLoopPlateOhms, circuit::PostLoopCathodeFeedbackPlateLoadOhms,
      circuit::DeepCathodeOhms, circuit::DeepFixedBypass,
      circuit::DeepSwitchedBypass, circuit::BassDeepReturnOhms, rate);

    preampCoupling.Coupling(circuit::V1BOutputCoupling,
      circuit::Parallel(circuit::V1BPlateOhms, rp), circuit::LeadInputNodeShunt, rate);
    leadReturnMixerBase.Prepare(rate, circuit::RhythmMixBright);
    leadReturnMixerLaterSimul.Prepare(rate, circuit::LaterSimulRhythmMixBright);
    leadInterstage.CoupledDivider(circuit::LeadInterstageCoupling,
      circuit::Parallel(circuit::V3BPlateOhms, rp) + circuit::LeadInterstageSeries,
      circuit::LeadInterstageLoad, circuit::LeadInterstageShunt, rate);
    leadPlate.LowPass(circuit::Parallel(circuit::LeadPlateOhms, rp), circuit::LeadPlateShunt, rate);

    eqInput.SetAnalog(1.0, 0.0, 0.0, 1.0, 0.0, 0.0, rate);
    eqFeedbackHF.LowPass(circuit::GraphicEqSharedQ23Ohms, circuit::GraphicEqFeedbackCap, rate);
    constexpr double geqLocalSource = circuit::Parallel(circuit::GraphicEqOutputShunt,
      circuit::GraphicEqQ1Q2Q4LocalOhms);
    constexpr double geqOutputSource = circuit::Parallel(geqLocalSource,
      circuit::GraphicEqUpperRailOhms);
    eqOutput.Coupling(circuit::GraphicEqOutputCoupling, geqOutputSource,
      circuit::GraphicEqBiasLoadOhms, rate);

    for (auto* f : {&preampCoupling, &leadInput, &leadInterstage, &leadPlate,
                   &recoveryCoupling, &masterCoupling, &eqInput, &eqFeedbackHF, &eqOutput})
      f->Reset();
    leadReturnMixerBase.Reset(); leadReturnMixerLaterSimul.Reset();
    laterMixerRegion = -1;
    simulHfActive = false;
    eq.Reset();
    SetOversamplerCoefficients();
    ClearOversamplers();

    smoothing = std::exp(-1.0 / (0.020 * rate));
    switchSmoothing = std::exp(-1.0 / (0.006 * rate));
    controlClock = 0;
    filtersMoving = true;
    leadPathActive = false;
    eqProcessingActive = false;
    eqEngagement = controls::EqModeEngagement(current[EqMode], current[Lead]);
    UpdateFilters();
    filtersMoving = false;
  }

  void ApplyOversamplingMode(int mode) {
    oversamplingMode = std::clamp(mode, 0, 3);
    oversamplingFactor = 1 << oversamplingMode;
    PrepareHighRatePath(oversamplingFactor);
  }

  void ResetLeadSignalPath() {
    cathodes[2].Reset();
    leadInput.Reset();
    leadInterstage.Reset();
    leadPlate.Reset();
    leadBrightCathode.Reset();
  }

  void ResetEqSignalPath() {
    eq.Reset(); eqInput.Reset(); eqFeedbackHF.Reset(); eqOutput.Reset();
  }

  double ProcessLeadReturnMix(double rhythm, double lead, double amount) {
    const double a = std::clamp(amount, 0.0, 1.0);
    constexpr double endpoint = 1.0e-6;
    if (a <= endpoint) {
      laterMixerRegion = 0;
      return leadReturnMixerBase.Process(rhythm, lead, current[Lead]);
    }
    if (a >= 1.0 - endpoint) {
      laterMixerRegion = 2;
      return leadReturnMixerLaterSimul.Process(rhythm, lead, current[Lead]);
    }
    if (laterMixerRegion == 0) leadReturnMixerLaterSimul.Reset();
    else if (laterMixerRegion == 2) leadReturnMixerBase.Reset();
    laterMixerRegion = 1;
    const double base = leadReturnMixerBase.Process(rhythm, lead, current[Lead]);
    const double later = leadReturnMixerLaterSimul.Process(rhythm, lead, current[Lead]);
    return Mix(base, later, a);
  }

  void UpdateFilters() {
    const double shift = controls::TrebleShift(current[TrebleShift], current[Lead]);
    toneStack.Configure(current[Treble], current[Bass], current[Middle], current[Volume], shift, current[RhythmBright]);

    // Pass 9 resolves the physical Middle control as a 10K pot; that exact
    // component and the complete passive T/M/B topology remain untouched in
    // PassiveToneStack. The reduced real-time nonlinear stages can compress most
    // of that passive 400-500 Hz delta in high-gain Lead operation, making the
    // panel control much less audible than the decoded network itself. Restore
    // only the masked perceptual delta with a broad unity-at-5 compensation bell.
    // This is an explicit model calibration (like the Presence audibility shelf),
    // not a new schematic component and not a replacement for the Pass-9 10K pot.
    constexpr double middleAudibilityCenterHz = 450.0;
    constexpr double middleAudibilityQ = 0.75;
    constexpr double middleAudibilityDbPerKnobUnit = 0.60; // 0..10 => -3..+3 dB
    const double middleGainDb = (current[Middle] - 5.0) * middleAudibilityDbPerKnobUnit;
    const double middleA = std::pow(10.0, middleGainDb / 40.0);
    const double middleW = 2.0 * kPi * middleAudibilityCenterHz;
    middleAudibility.SetAnalog(middleW * middleW, middleA * middleW / middleAudibilityQ, 1.0,
                              middleW * middleW, middleW / (middleA * middleAudibilityQ), 1.0, rate);

    for (int i = 0; i < 5; ++i) eq.SetBand(i, current[Eq80 + i]);

    const double leadPosition = Taper(current[LeadDrive]);
    const double leadUpper = circuit::LeadDriveOhms * (1.0 - leadPosition);
    const double leadLower = std::max(1.0, circuit::LeadDriveOhms * leadPosition);
    const double leadGridLoad = circuit::Parallel(leadLower, circuit::LeadGridLeak);
    const double leadBranchInputR = circuit::LeadDriveSeries + leadUpper + leadGridLoad;
    const double rhythmBranchInputR = circuit::RhythmMixSeries + circuit::RecoveryGridLeak;
    const double inputNodeConductance = 1.0 / circuit::LeadInputNodeShunt +
      1.0 / rhythmBranchInputR + current[Lead] / leadBranchInputR;
    const double inputNodeLoad = 1.0 / inputNodeConductance;
    preampCoupling.Coupling(circuit::V1BOutputCoupling,
      circuit::Parallel(circuit::V1BPlateOhms, circuit::TriodeRpEstimate), inputNodeLoad, rate);

    leadInput.Divider(0.0, circuit::LeadDriveSeries + leadUpper, 0.0,
      leadGridLoad, circuit::LeadGridCathodeCap, rate);

    constexpr double returnNodeLoad = circuit::Parallel(circuit::FromReverbShuntR101, circuit::ReturnShuntR103);
    constexpr double normalledLoopLeg = circuit::ReverbBridgeR102 + returnNodeLoad;
    const double leadMasterR = std::max(1.0, circuit::LeadMasterOhms * Taper(current[LeadMaster]));
    const double leadMasterConductance = current[Lead] / leadMasterR;
    const double nodeLoad = 1.0 / (1.0 / normalledLoopLeg + leadMasterConductance);
    recoveryCoupling.Coupling(circuit::RecoveryCoupling,
      circuit::Parallel(circuit::PreLoopPlateOhms, circuit::TriodeRpEstimate) + circuit::RecoverySeriesR105,
      nodeLoad, rate);
    recoveryScale = returnNodeLoad / normalledLoopLeg;

    masterCoupling.Coupling(circuit::MasterCoupling,
      circuit::Parallel(circuit::PostLoopPlateOhms, circuit::TriodeRpEstimate) + circuit::MasterSeries,
      circuit::MasterOhms, rate);
    masterScale = Taper(current[Master]);
    power.Configure(current[Presence], rate);
    inputScale = InputTrimScale(current[InputCalibration]);
    outputScale = OutputTrimScale(current[OutputGain]);
    eqScale = Db(current[EqGain]);
  }

  void AdvanceParameters() {
    bool stillMoving = false;
    bool coefficientMoving = false;

    if (parametersMoving) {
      for (int i = 0; i < NumParams; ++i) {
        if (i == EqMode || i == Oversampling) continue;
        const double difference = target[i] - current[i];
        if (std::abs(difference) < 1.0e-9) {
          current[i] = target[i];
          continue;
        }
        const bool isSwitch = (i >= RhythmBright && i <= Bypass) || i == PowerMode ||
          i == ReverbOn || i == LaterSimul20p || i == ExportBias;
        const double coeff = isSwitch ? switchSmoothing : smoothing;
        current[i] = target[i] - coeff * difference;
        stillMoving = true;
        if (ParamNeedsFilterUpdate(i)) coefficientMoving = true;
      }
      parametersMoving = stillMoving;
    }

    // Expensive matrix/filter coefficient work runs only while a parameter that
    // actually changes those coefficients is moving. At steady state this path
    // is completely skipped; the old build scanned every parameter at 8x rate.
    if (controlClock == 0 && (coefficientMoving || filtersMoving)) {
      UpdateFilters();
      filtersMoving = coefficientMoving;
    }
    controlClock = (controlClock + 1) & 31;
  }

  double ProcessHighRate(double x) {
    // At steady state there is no parameter work at all. This branch matters
    // increasingly at 4x/8x because ProcessHighRate runs once per sub-sample.
    if (parametersMoving || filtersMoving)
      AdvanceParameters();

    x = inputDC.High(x * inputScale);
    x = stages[0].Process(bassShiftCathode.Process(x, current[BassShift])); // V1A
    x = toneStack.Process(x);
    x = preampCoupling.Process(stages[1].Process(cathodes[1].Process(x))); // V1B

    // LDR2 fully disconnects the two Lead triode stages in Rhythm. Do not spend
    // CPU on V3B/V4A until the channel switch is actually crossing toward Lead.
    double lead = 0.0;
    const bool needLeadPath = current[Lead] > 1.0e-7 || target[Lead] > 1.0e-7;
    if (needLeadPath) {
      if (!leadPathActive) { ResetLeadSignalPath(); leadPathActive = true; }
      const double ldr2Signal = x * current[Lead];
      lead = stages[2].Process(cathodes[2].Process(leadInput.Process(ldr2Signal))); // V3B
      lead = stages[3].Process(leadBrightCathode.Process(leadInterstage.Process(lead), current[LeadBright])); // V4A
      lead = leadPlate.Process(lead);
    } else if (leadPathActive) {
      ResetLeadSignalPath();
      leadPathActive = false;
    }

    // The 10pF and later-Simul 20pF return networks are hard hardware
    // alternatives. At a settled endpoint process only the selected network;
    // run both only during the short de-click transition.
    x = ProcessLeadReturnMix(x, lead, current[LaterSimul20p]);

    const double toReturn = recoveryCoupling.Process(stages[4].Process(cathodes[4].Process(x)));
    x = recoveryScale * toReturn;
    x = masterScale * masterCoupling.Process(stages[5].Process(deepCathode.Process(x, current[Deep])));

    const double eqTarget = controls::EqModeEngagement(current[EqMode], current[Lead]);
    eqEngagement = eqTarget - switchSmoothing * (eqTarget - eqEngagement);
    if (eqEngagement > 1.0e-6 || eqTarget > 0.0) {
      if (!eqProcessingActive) { ResetEqSignalPath(); eqProcessingActive = true; }
      const double equalized = eqOutput.Process(eqFeedbackHF.Process(eq.Process(eqInput.Process(x))));
      x = Mix(x, equalized * eqScale, eqEngagement);
    } else if (eqProcessingActive) {
      ResetEqSignalPath();
      eqProcessingActive = false;
    }

    x = power.Process(x, current[PowerMode], current[ExportBias]);

    // Apply the correction after the reduced nonlinear chain so heavy Lead
    // saturation cannot erase it again. At the exact default Middle=5.00 this
    // path sleeps, preserving the 0.10.24 default sound sample-for-sample.
    const bool needMiddleAudibility = std::abs(current[Middle] - 5.0) > 1.0e-7 ||
                                       std::abs(target[Middle] - 5.0) > 1.0e-7;
    if (needMiddleAudibility) {
      if (!middleAudibilityActive) { middleAudibility.Reset(); middleAudibilityActive = true; }
      x = middleAudibility.Process(x);
    } else if (middleAudibilityActive) {
      middleAudibility.Reset();
      middleAudibilityActive = false;
    }

    // The extra later-Simul HF loss is inaudible in 10pF mode, so its recursive
    // filter can sleep completely at that settled endpoint.
    const double laterAmount = current[LaterSimul20p];
    if (laterAmount > 1.0e-6 || target[LaterSimul20p] > 1.0e-6) {
      if (!simulHfActive) { simulHfAudibility.Reset(); simulHfActive = true; }
      x -= 0.35 * laterAmount * simulHfAudibility.High(x);
    } else if (simulHfActive) {
      simulHfAudibility.Reset();
      simulHfActive = false;
    }
    return x;
  }

  double ProcessOversampled(double input) {
    if (oversamplingFactor <= 1)
      return ProcessHighRate(input);

    double twice[2];
    up2.process_sample(twice[0], twice[1], input);
    if (oversamplingFactor == 2) {
      twice[0] = ProcessHighRate(twice[0]);
      twice[1] = ProcessHighRate(twice[1]);
      return down2.process_sample(twice);
    }

    double four[4];
    up4.process_sample(four[0], four[1], twice[0]);
    up4.process_sample(four[2], four[3], twice[1]);
    if (oversamplingFactor == 4) {
      for (double& v : four) v = ProcessHighRate(v);
      twice[0] = down4.process_sample(four);
      twice[1] = down4.process_sample(four + 2);
      return down2.process_sample(twice);
    }

    double eight[8];
    for (int i = 0; i < 4; ++i)
      up8.process_sample(eight[i * 2], eight[i * 2 + 1], four[i]);
    for (double& v : eight) v = ProcessHighRate(v);
    for (int i = 0; i < 4; ++i)
      four[i] = down8.process_sample(eight + i * 2);
    twice[0] = down4.process_sample(four);
    twice[1] = down4.process_sample(four + 2);
    return down2.process_sample(twice);
  }

  double baseRate = 48000.0;
  double rate = 48000.0;
  double smoothing = 0.0, switchSmoothing = 0.0;
  double inputScale = 1.0, outputScale = 1.0, eqScale = 1.0;
  double recoveryScale = 1.0, masterScale = 1.0;
  int controlClock = 0;
  bool filtersMoving = true;
  bool parametersMoving = false;
  bool leadPathActive = false;
  bool eqProcessingActive = false;
  bool middleAudibilityActive = false;
  bool simulHfActive = false;
  int laterMixerRegion = -1;
  double eqEngagement = 0.0;

  int oversamplingMode = 0;
  int pendingOversamplingMode = 0;
  int oversamplingFactor = 1;
  int osTransitionState = 0; // 0=stable, 1=fade to dry, 2=fade from dry
  double osWetMix = 1.0;
  double osFadeStep = 1.0;

  Parameters current = Defaults(), target = Defaults();
  PassiveToneStack toneStack;
  GraphicEqCircuit eq;
  std::array<TriodeStage, 6> stages;
  std::array<CathodeNetwork, 6> cathodes;
  PlateLoadedCathodeSwitch bassShiftCathode, leadBrightCathode, deepCathode;
  CircuitFilter middleAudibility;
  CircuitFilter preampCoupling, leadInput, leadInterstage, leadPlate;
  CircuitFilter recoveryCoupling, masterCoupling, eqInput, eqFeedbackHF, eqOutput;
  LeadReturnMixer leadReturnMixerBase, leadReturnMixerLaterSimul;
  Pass9ReverbCircuit reverb;
  OnePole inputDC, outputDC, simulHfAudibility;
  PowerSimulClassStage power;
  hiir::Upsampler2xFPU<12, double> up2;
  hiir::Upsampler2xFPU<4, double> up4;
  hiir::Upsampler2xFPU<3, double> up8;
  hiir::Downsampler2xFPU<3, double> down8;
  hiir::Downsampler2xFPU<4, double> down4;
  hiir::Downsampler2xFPU<12, double> down2;
};

class Amp {
public:
  void Prepare(double rate, const Parameters& params = Defaults()) {
    if (!std::isfinite(rate) || rate < 8000) rate = 48000;
    for (auto& channel : channels) channel.Prepare(rate, params);
  }
  void SetParameters(const Parameters& params) {
    for (auto& channel : channels) channel.SetParameters(params);
  }
  template<typename T>
  void Process(T** inputs, T** outputs, int frames, int nInputs, int nOutputs) {
    nInputs = std::clamp(nInputs, 0, 2);
    nOutputs = std::clamp(nOutputs, 0, 2);
    for (int s = 0; s < frames; ++s) {
      // Read both inputs before writing, including the in-place mono-to-stereo case.
      const double left = nInputs > 0 ? inputs[0][s] : 0;
      const double right = nInputs > 1 ? inputs[1][s] : left;
      const double outL = channels[0].Process(left);
      const double outR = channels[1].Process(right);
      if (nOutputs > 0) outputs[0][s] = static_cast<T>(outL);
      if (nOutputs > 1) outputs[1][s] = static_cast<T>(outR);
    }
  }
private:
  std::array<AmpChannel, 2> channels;
};
} // namespace crunchy
