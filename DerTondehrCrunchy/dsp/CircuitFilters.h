#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "CircuitValues.h"

namespace crunchy {
// Bilinear transform of an analog rational function in ascending powers of s.
// Coefficients are changed at the smoothed control rate; state stays per channel.
class CircuitFilter {
public:
  void Reset() { z1=z2=0; }
  void SetAnalog(double n0,double n1,double n2,double d0,double d1,double d2,double fs) {
    const double k=2*fs;
    if (n2==0 && d2==0) {
      const double a=d0+d1*k;
      b0=(n0+n1*k)/a; b1=(n0-n1*k)/a; b2=0;
      a1=(d0-d1*k)/a; a2=0; z2=0;
    } else {
      const double a=d0+d1*k+d2*k*k;
      b0=(n0+n1*k+n2*k*k)/a; b1=(2*n0-2*n2*k*k)/a; b2=(n0-n1*k+n2*k*k)/a;
      a1=(2*d0-2*d2*k*k)/a; a2=(d0-d1*k+d2*k*k)/a;
    }
  }
  double Process(double x) {
    const double y=b0*x+z1;
    z1=b1*x-a1*y+z2; z2=b2*x-a2*y;
    return y;
  }
  void Coupling(double cap,double sourceOhms,double loadOhms,double fs) {
    SetAnalog(0,cap*loadOhms,0,1,cap*(sourceOhms+loadOhms),0,fs);
  }
  void LowPass(double resistance,double cap,double fs) {
    SetAnalog(1,0,0,1,resistance*cap,0,fs);
  }
  void CoupledDivider(double cap,double sourceOhms,double loadOhms,double shuntCap,double fs) {
    SetAnalog(0,loadOhms*cap,0,1,cap*(sourceOhms+loadOhms)+loadOhms*shuntCap,
      sourceOhms*loadOhms*cap*shuntCap,fs);
  }
  // Source -> (Rseries parallel Cbright) -> (Rload parallel Cshunt).
  void Divider(double sourceOhms,double seriesOhms,double brightCap,
               double loadOhms,double shuntCap,double fs) {
    const double t=seriesOhms*brightCap, u=loadOhms*shuntCap;
    SetAnalog(loadOhms,loadOhms*t,0,
      sourceOhms+seriesOhms+loadOhms,
      sourceOhms*(t+u)+seriesOhms*u+loadOhms*t,sourceOhms*t*u,fs);
  }
private:
  double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
};

class TriodeStage {
public:
  void Prepare(double supply,double plateOhms,double cathodeOhms, double measuredCathodeVolts = 0.0) {
    supplyV = std::max(50.0, supply);
    plateR = std::max(1000.0, plateOhms);
    cathodeR = std::max(100.0, cathodeOhms);
    gridBiasCorrection = 0.0;

    // Prefer an original handwritten DC operating point when Pass 9 supplies
    // one.  This keeps the nonlinear table tied to the actual resistor load
    // line instead of forcing the generic Koren parameters to choose the bias.
    // A tiny effective-grid correction makes the Koren current equation pass
    // through that measured Q point; it is a model calibration, not an extra
    // circuit component.
    const double requestedVk = std::max(0.0, measuredCathodeVolts);
    const double requestedCurrent = requestedVk / cathodeR;
    const double requestedPlate = supplyV - requestedCurrent * plateR;
    if (requestedVk > 0.0 && requestedCurrent > 0.0 &&
        requestedPlate > requestedVk + 5.0) {
      idleCurrent = requestedCurrent;
      idleCathode = requestedVk;
      idlePlate = requestedPlate;

      double loBias = -4.0, hiBias = 4.0;
      const double vpk = std::max(0.05, idlePlate - idleCathode);
      for (int n = 0; n < 56; ++n) {
        const double correction = 0.5 * (loBias + hiBias);
        const double tube = PlateCurrent(vpk, -idleCathode + correction);
        if (tube < idleCurrent) loBias = correction; else hiBias = correction;
      }
      gridBiasCorrection = 0.5 * (loBias + hiBias);
    } else {
      // No measured point: solve the quiescent point from the generic 12AX7
      // equation and the actual plate/cathode resistors.
      double lo = 0.0;
      double hi = supplyV / (plateR + cathodeR);
      for (int n = 0; n < 48; ++n) {
        const double current = 0.5 * (lo + hi);
        const double vp = supplyV - current * plateR;
        const double vk = current * cathodeR;
        const double tube = PlateCurrent(std::max(0.05, vp - vk), -vk);
        if (current > tube) hi = current; else lo = current;
      }
      idleCurrent = 0.5 * (lo + hi);
      idleCathode = idleCurrent * cathodeR;
      idlePlate = supplyV - idleCurrent * plateR;
    }

    // A small precomputed load-line transfer is much smoother and more
    // asymmetric than the old tanh approximation, while remaining cheap in the
    // 8x-oversampled audio loop.  -8 V is already deep into cutoff for these
    // stages; the positive side is limited by grid conduction before lookup.
    for (int i = 0; i < kTableSize; ++i) {
      const double t = static_cast<double>(i) / static_cast<double>(kTableSize - 1);
      const double grid = kGridMin + (kGridMax - kGridMin) * t;
      table[i] = SolvePlate(grid) - idlePlate;
    }
    positiveGridThreshold = std::max(0.25, idleCathode - 0.25);
    tablePositionScale = static_cast<double>(kTableSize - 1) / (kGridMax - kGridMin);
  }

  double Process(double gridSignal) const {
    if (!std::isfinite(gridSignal)) return 0.0;
    // Keep mathematical silence exactly silent. The precomputed load-line grid
    // does not necessarily contain 0V as an exact table point, so interpolation
    // can otherwise create a sub-microvolt DC residue that later high-pass
    // stages turn into a startup transient.
    if (gridSignal == 0.0) return 0.0;

    // Once Vg approaches Vk the real 12AX7 starts drawing grid current.  A
    // static soft clamp is intentionally conservative: it removes the brittle,
    // square-wave-like positive-grid overdrive of the old model without adding
    // an invented compressor or changing the schematic gain controls.
    constexpr double knee = 0.80;
    if (gridSignal > positiveGridThreshold)
      gridSignal = positiveGridThreshold + knee * std::tanh((gridSignal - positiveGridThreshold) / knee);

    if (gridSignal < kGridMin) gridSignal = kGridMin;
    else if (gridSignal > kGridMax) gridSignal = kGridMax;
    const double pos = (gridSignal - kGridMin) * tablePositionScale;
    const int i = std::min(kTableSize - 2, std::max(0, static_cast<int>(pos)));
    const double frac = pos - static_cast<double>(i);
    return table[i] + (table[i + 1] - table[i]) * frac;
  }

  double IdleCathodeVolts() const { return idleCathode; }
  double IdlePlateVolts() const { return idlePlate; }
  double GridBiasCorrectionVolts() const { return gridBiasCorrection; }

private:
  static double SoftPlus(double x) {
    if (x > 50.0) return x;
    if (x < -50.0) return std::exp(x);
    return std::log1p(std::exp(x));
  }

  static double PlateCurrent(double vpk, double vgk) {
    vpk = std::max(vpk, 1.0e-6);
    const double root = std::sqrt(circuit::TriodeKvb + vpk * vpk);
    const double arg = circuit::TriodeKp * (1.0 / circuit::TriodeMu + vgk / root);
    const double e1 = (vpk / circuit::TriodeKp) * SoftPlus(arg);
    if (!(e1 > 0.0)) return 0.0;
    return 2.0 * std::pow(e1, circuit::TriodeEx) / circuit::TriodeKg1;
  }

  double SolvePlate(double gridSignal) const {
    // Cathode-frequency feedback is handled by CathodeNetwork before this
    // transfer, so the nonlinear lookup holds the cathode at its DC bias and
    // solves the plate load line for the effective AC grid voltage.
    double lo = idleCathode + 0.05;
    double hi = supplyV;
    const double vgk = gridSignal - idleCathode + gridBiasCorrection;
    for (int n = 0; n < 36; ++n) {
      const double vp = 0.5 * (lo + hi);
      const double loadCurrent = (supplyV - vp) / plateR;
      const double tubeCurrent = PlateCurrent(vp - idleCathode, vgk);
      if (loadCurrent > tubeCurrent) lo = vp; else hi = vp;
    }
    return 0.5 * (lo + hi);
  }

  static constexpr int kTableSize = 4097;
  static constexpr double kGridMin = -8.0;
  static constexpr double kGridMax = 4.0;
  std::array<double, kTableSize> table{};
  double supplyV = 300.0, plateR = 100000.0, cathodeR = 1500.0;
  double idleCurrent = 0.001, idleCathode = 1.5, idlePlate = 200.0;
  double gridBiasCorrection = 0.0;
  double positiveGridThreshold = 1.25;
  double tablePositionScale = static_cast<double>(kTableSize - 1) / (kGridMax - kGridMin);
};

class CathodeNetwork {
public:
  void Reset() { filter.Reset(); }
  void Configure(double plateOhms,double cathodeOhms,double fixedCap,
                 double switchedCap,double returnOhms,double fs) {
    ConfigureImpl(plateOhms, cathodeOhms, fixedCap, switchedCap, returnOhms, fs, false);
  }
  void ConfigureInverse(double plateOhms,double cathodeOhms,double fixedCap,
                        double switchedCap,double returnOhms,double fs) {
    ConfigureImpl(plateOhms, cathodeOhms, fixedCap, switchedCap, returnOhms, fs, true);
  }
  double Process(double input) { return filter.Process(input); }
private:
  void ConfigureImpl(double plateOhms,double cathodeOhms,double fixedCap,
                     double switchedCap,double returnOhms,double fs,bool inverse) {
    // Small-signal gain / fully-bypassed gain = Yk / (Yk + (mu+1)/(rp+Ra)).
    // Here Ra means the AC plate load seen by the triode. Historically Crunchy
    // fed only the B+ plate resistor to this formula; 0.10.21 can additionally
    // account for the downstream load in the *switch delta* without moving the
    // calibrated switch-OFF amplifier response.
    const double g=(circuit::TriodeMuEstimate+1)/(circuit::TriodeRpEstimate+plateOhms);
    const double t=returnOhms*switchedCap;
    const double n0=1/cathodeOhms, n1=t/cathodeOhms+fixedCap+switchedCap, n2=fixedCap*t;
    const double d0=n0+g, d1=n1+g*t, d2=n2;
    if (inverse) filter.SetAnalog(d0,d1,d2,n0,n1,n2,fs);
    else filter.SetAnalog(n0,n1,n2,d0,d1,d2,fs);
  }
  CircuitFilter filter;
};

// A real pull switch jumps between two fixed circuit topologies.  Morphing the
// coefficients of one recursive filter through all intermediate resistance
// values can create zipper/step noise because its stored state belongs to the
// previous topology.  Keep both endpoint networks running continuously and
// crossfade their outputs instead.  This is also closer to the hardware: the
// 15 kOhm return is either present or bypassed by the switch/LDR.
class SwitchableCathodeNetwork {
public:
  void Prepare(double plateOhms, double cathodeOhms, double fixedCap,
               double switchedCap, double returnOhms, double fs) {
    off.Configure(plateOhms, cathodeOhms, fixedCap, switchedCap, returnOhms, fs);
    on.Configure(plateOhms, cathodeOhms, fixedCap, switchedCap, 1.0, fs);
    Reset();
  }
  void Reset() { off.Reset(); on.Reset(); region = -1; }
  double Process(double input, double amount) {
    const double a = std::clamp(amount, 0.0, 1.0);
    constexpr double endpoint = 1.0e-6;

    // At a settled switch endpoint only one topology can be heard.  The old
    // implementation evaluated BOTH recursive cathode networks on every sample,
    // even after a pull switch had been stationary for minutes.  Keep both only
    // during the short 6 ms de-click transition.
    if (a <= endpoint) {
      region = 0;
      return off.Process(input);
    }
    if (a >= 1.0 - endpoint) {
      region = 2;
      return on.Process(input);
    }

    // When a dormant endpoint is about to join a crossfade, start it from a
    // known zero-energy state.  The active endpoint continues uninterrupted and
    // the switch smoothing hides the newly-started branch.
    if (region == 0) on.Reset();
    else if (region == 2) off.Reset();
    region = 1;
    const double dry = off.Process(input);
    const double pulled = on.Process(input);
    return dry + (pulled - dry) * a;
  }
private:
  CathodeNetwork off, on;
  int region = -1; // 0=off endpoint, 1=crossfade, 2=on endpoint
};

// Pass-9 plate-loaded pull-switch model used by Bass Shift, Lead Bright and
// Deep. The existing switch-OFF transfer is deliberately kept byte-for-byte
// equivalent to the earlier calibrated model. Only the ON/OFF differential is
// corrected for the downstream AC load shown by the schematic:
//
//   OFF = H_old_off
//   ON  = H_old_off * H_loaded_on / H_loaded_off
//
// This avoids changing the established amplifier level/tone when all pull
// switches are OFF, while fixing the previous underestimation of cathode-bypass
// action caused by using the raw plate resistor as if no following network were
// connected to that plate.
class PlateLoadedCathodeSwitch {
public:
  void Prepare(double plateOhms, double loadedPlateOhms, double cathodeOhms,
               double fixedCap, double switchedCap, double returnOhms, double fs) {
    const double loaded = std::max(1.0, loadedPlateOhms);
    baseOff.Configure(plateOhms, cathodeOhms, fixedCap, switchedCap, returnOhms, fs);
    loadedOn.Configure(loaded, cathodeOhms, fixedCap, switchedCap, 1.0, fs);
    loadedOffInverse.ConfigureInverse(loaded, cathodeOhms, fixedCap, switchedCap, returnOhms, fs);
    Reset();
  }
  void Reset() {
    baseOff.Reset(); loadedOn.Reset(); loadedOffInverse.Reset(); region = -1;
  }
  double Process(double input, double amount) {
    const double a = std::clamp(amount, 0.0, 1.0);
    constexpr double endpoint = 1.0e-6;

    // Keep the baseline path warm at all times. This is the exact pre-0.10.21
    // OFF topology and therefore preserves the established sound at rest.
    const double dry = baseOff.Process(input);
    if (a <= endpoint) {
      region = 0;
      return dry;
    }

    // The correction branch can sleep while the switch is OFF. Restart it from
    // zero energy when it joins the 6 ms switch crossfade.
    if (region == 0) {
      loadedOn.Reset();
      loadedOffInverse.Reset();
    }
    const double pulled = loadedOffInverse.Process(loadedOn.Process(dry));
    if (a >= 1.0 - endpoint) {
      region = 2;
      return pulled;
    }

    region = 1;
    return dry + (pulled - dry) * a;
  }
private:
  CathodeNetwork baseOff, loadedOn, loadedOffInverse;
  int region = -1;
};


// Shared V1B / Lead-output mixing node feeding the following V2 grid.
//
// Clearer handwritten IIC+ sheet:
//
//   V4A plate -- C30 .047u --+-- (R31 220k || C31 330p) --+-- LDR3/FROM LEAD
//                             |                              |
//                             |                      R32 100k || C32 560p
//                             |                              |
//                             +------------------------------+--> ground through shunt at output node
//
// R10 3.3M || C10 10p (Rhythm) and R11 680k || C11 47p meet at the same
// recovery-grid node when LDR3 closes.  Keep a separate Rhythm-only endpoint
// and crossfade the switch transition so the LDR does not morph recursive state.
class LeadReturnMixer {
  struct Cap {
    int a = 0, b = -1;
    double g = 0.0, voltage = 0.0, current = 0.0;
  };
  using Matrix3 = std::array<std::array<double, 3>, 3>;
public:
  void Prepare(double fs, double rhythmBrightCap = circuit::RhythmMixBright) {
    rate = fs;
    rhythmMixBright = std::max(0.0, rhythmBrightCap);
    rhythm.Reset();
    rhythm.Divider(0.0, circuit::RhythmMixSeries, rhythmMixBright,
                   circuit::RecoveryGridLeak, circuit::RecoveryGridHF, fs);

    // 0 = V4A Thevenin source side of C30
    // 1 = after C30, before R31||C31
    // 2 = LEAD OUTPUT / FROM LEAD / following V2-grid node
    c30 = {0, 1, 2.0 * fs * circuit::LeadOutputCoupling, 0.0, 0.0};
    c31 = {1, 2, 2.0 * fs * circuit::LeadOutputBright, 0.0, 0.0};
    c32 = {2, -1, 2.0 * fs * circuit::LeadOutputShuntC, 0.0, 0.0};
    c11 = {2, -1, 2.0 * fs * circuit::RecoveryGridHF, 0.0, 0.0};
    c10g = 2.0 * fs * rhythmMixBright;
    c10Voltage = c10Current = 0.0;

    Matrix3 m{};
    const auto stampR = [&](int a, int b, double r) {
      const double g = 1.0 / std::max(1.0, r);
      m[a][a] += g;
      if (b >= 0) { m[b][b] += g; m[a][b] -= g; m[b][a] -= g; }
    };
    const auto stampC = [&](const Cap& c) {
      m[c.a][c.a] += c.g;
      if (c.b >= 0) { m[c.b][c.b] += c.g; m[c.a][c.b] -= c.g; m[c.b][c.a] -= c.g; }
    };

    leadSourceOhms = circuit::Parallel(circuit::LeadPlateOhms, circuit::TriodeRpEstimate);
    stampR(0, -1, leadSourceOhms);
    stampC(c30);
    stampR(1, 2, circuit::LeadOutputSeries);
    stampC(c31);

    // Rhythm source -> R10||C10 -> common node 2. Known-source terms go to RHS.
    m[2][2] += 1.0 / circuit::RhythmMixSeries + c10g;
    stampR(2, -1, circuit::RecoveryGridLeak);
    stampC(c11);

    // Handwritten Lead-output shunt is directly from the output node to ground.
    stampR(2, -1, circuit::LeadOutputShuntR);
    stampC(c32);

    inverse = Invert(m);
    Reset();
  }

  void Reset() {
    rhythm.Reset();
    ResetLeadNetwork();
    fullPathActive = false;
  }

  double Process(double rhythmSource, double leadPlateSource, double leadAmount) {
    const double rhythmOut = rhythm.Process(rhythmSource);
    const double a = std::clamp(leadAmount, 0.0, 1.0);

    // With LDR3 fully open, the 3x3 Lead-output nodal solve is electrically
    // disconnected.  Avoid solving/updating that dormant network in Rhythm.
    // It is reinitialised when the 6 ms channel crossfade starts.
    if (a <= 1.0e-7) {
      if (fullPathActive) { ResetLeadNetwork(); fullPathActive = false; }
      return rhythmOut;
    }
    if (!fullPathActive) { ResetLeadNetwork(); fullPathActive = true; }

    std::array<double, 3> rhs{};
    rhs[0] += leadPlateSource / leadSourceOhms;
    rhs[2] += rhythmSource / circuit::RhythmMixSeries;

    AddCapHistory(c30, rhs);
    AddCapHistory(c31, rhs);
    AddCapHistory(c32, rhs);
    AddCapHistory(c11, rhs);

    const double h10 = c10g * c10Voltage + c10Current;
    rhs[2] += c10g * rhythmSource - h10;

    std::array<double, 3> node{};
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j)
        node[i] += inverse[i][j] * rhs[j];

    UpdateCap(c30, node[0] - node[1]);
    UpdateCap(c31, node[1] - node[2]);
    UpdateCap(c32, node[2]);
    UpdateCap(c11, node[2]);
    const double newC10Voltage = rhythmSource - node[2];
    c10Current = c10g * (newC10Voltage - c10Voltage) - c10Current;
    c10Voltage = newC10Voltage;

    return rhythmOut + (node[2] - rhythmOut) * a;
  }

private:
  void ResetLeadNetwork() {
    c30.voltage = c30.current = 0.0;
    c31.voltage = c31.current = 0.0;
    c32.voltage = c32.current = 0.0;
    c11.voltage = c11.current = 0.0;
    c10Voltage = c10Current = 0.0;
  }
  static Matrix3 Invert(Matrix3 a) {
    Matrix3 inv{};
    for (int i = 0; i < 3; ++i) inv[i][i] = 1.0;
    for (int k = 0; k < 3; ++k) {
      int pivot = k;
      for (int i = k + 1; i < 3; ++i)
        if (std::abs(a[i][k]) > std::abs(a[pivot][k])) pivot = i;
      std::swap(a[k], a[pivot]);
      std::swap(inv[k], inv[pivot]);
      const double d = a[k][k];
      for (int j = 0; j < 3; ++j) { a[k][j] /= d; inv[k][j] /= d; }
      for (int i = 0; i < 3; ++i) if (i != k) {
        const double f = a[i][k];
        for (int j = 0; j < 3; ++j) {
          a[i][j] -= f * a[k][j];
          inv[i][j] -= f * inv[k][j];
        }
      }
    }
    return inv;
  }

  static void AddCapHistory(const Cap& c, std::array<double, 3>& rhs) {
    const double h = c.g * c.voltage + c.current;
    rhs[c.a] += h;
    if (c.b >= 0) rhs[c.b] -= h;
  }
  static void UpdateCap(Cap& c, double voltage) {
    c.current = c.g * (voltage - c.voltage) - c.current;
    c.voltage = voltage;
  }

  double rate = 384000.0, leadSourceOhms = 50000.0, rhythmMixBright = circuit::RhythmMixBright;
  CircuitFilter rhythm;
  Matrix3 inverse{};
  Cap c30, c31, c32, c11;
  double c10g = 0.0, c10Voltage = 0.0, c10Current = 0.0;
  bool fullPathActive = false;
};

class PresenceNetwork {
public:
  void Reset() { filter.Reset(); }
  void Configure(double presence,double fs) {
    // OT feedback tap -> RV14 250k rheostat -> 3k3 ->
    // (56k || .005uF) -> 22k -> PI tail.  Pass 9 also resolves BOTH lower
    // feedback capacitors, .047uF and .1uF, in parallel with the 3k3 lower
    // shunt. Build 0.9 only used .047uF. Higher Presence means less HF feedback.
    const double rs = circuit::PresenceSeries + circuit::FeedbackToPI + circuit::PresencePotOhms *
      std::clamp(presence / 10.0, 0.0, 1.0);
    const double tf = circuit::FeedbackSeries * circuit::FeedbackSeriesCap;
    const double lowerFeedbackCap = circuit::LowerFeedbackCapA + circuit::LowerFeedbackCapB;
    const double tg = circuit::TailShunt * lowerFeedbackCap;
    const double n0 = circuit::TailSeries + circuit::TailShunt;
    const double n1 = n0 * tf + circuit::TailSeries * tg;
    const double n2 = circuit::TailSeries * tg * tf;
    filter.SetAnalog(n0, n1, n2,
      rs + circuit::FeedbackSeries + n0,
      (rs + circuit::FeedbackSeries) * tg + rs * tf + n1,
      rs * tf * tg + n2, fs);
  }
  double Process(double x) { return filter.Process(x); }
private:
  CircuitFilter filter;
};
}
