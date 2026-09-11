#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "CircuitFilters.h"
#include "CircuitValues.h"

namespace crunchy {

// The electrical reverb driver/return values come from the Pass-9 handwritten
// decode. The mechanical tank constants below are deliberately named
// PROVISIONAL: Pass 9 does not identify the tank's delay/dispersion/decay data.
// They provide a stable spring-like load between the decoded tube electronics
// instead of pretending those missing mechanical parameters were on the sheet.
class SpringTankModel {
  class DampedComb {
  public:
    void Prepare(double fs, double delaySeconds, double feedback, double dampingHz) {
      const int samples = std::max(2, static_cast<int>(std::lround(fs * delaySeconds)));
      buffer.assign(static_cast<size_t>(samples), 0.0);
      index = 0;
      fb = std::clamp(feedback, 0.0, 0.985);
      dampingA = std::exp(-2.0 * 3.14159265358979323846 * dampingHz / fs);
      dampState = 0.0;
    }
    void Reset() {
      std::fill(buffer.begin(), buffer.end(), 0.0);
      index = 0;
      dampState = 0.0;
    }
    double Process(double x) {
      if (buffer.empty()) return 0.0;
      const double delayed = buffer[index];
      dampState = delayed + dampingA * (dampState - delayed);
      buffer[index] = std::clamp(x + fb * dampState, -8.0, 8.0);
      if (++index >= buffer.size()) index = 0;
      return delayed;
    }
  private:
    std::vector<double> buffer;
    size_t index = 0;
    double fb = 0.0, dampingA = 0.0, dampState = 0.0;
  };

  class Allpass {
  public:
    void Prepare(double fs, double delaySeconds, double gain) {
      const int samples = std::max(1, static_cast<int>(std::lround(fs * delaySeconds)));
      buffer.assign(static_cast<size_t>(samples), 0.0);
      index = 0;
      g = std::clamp(gain, -0.8, 0.8);
    }
    void Reset() { std::fill(buffer.begin(), buffer.end(), 0.0); index = 0; }
    double Process(double x) {
      if (buffer.empty()) return x;
      const double delayed = buffer[index];
      const double y = -g * x + delayed;
      buffer[index] = x + g * y;
      if (++index >= buffer.size()) index = 0;
      return y;
    }
  private:
    std::vector<double> buffer;
    size_t index = 0;
    double g = 0.0;
  };

  class Pole {
  public:
    void Set(double hz, double fs) { a = std::exp(-2.0 * 3.14159265358979323846 * hz / fs); }
    void Reset() { z = 0.0; }
    double Low(double x) { z = x + a * (z - x); return z; }
    double High(double x) { return x - Low(x); }
  private:
    double a = 0.0, z = 0.0;
  };

public:
  void Prepare(double fs) {
    // Provisional mechanical tank: four mutually-prime-ish paths and short
    // dispersive allpasses. These numbers are NOT schematic claims.
    constexpr double delays[] = {0.0297, 0.0371, 0.0411, 0.0437};
    constexpr double damping[] = {5200.0, 4800.0, 4300.0, 3900.0};
    constexpr double decaySeconds = 1.85;
    for (int i = 0; i < 4; ++i) {
      const double feedback = std::pow(10.0, -3.0 * delays[i] / decaySeconds);
      combs[i].Prepare(fs, delays[i], feedback, damping[i]);
    }
    allpasses[0].Prepare(fs, 0.0047, 0.55);
    allpasses[1].Prepare(fs, 0.0019, 0.48);
    inputLow.Set(6500.0, fs);
    inputDC.Set(145.0, fs);
    outputLow.Set(7200.0, fs);
    Reset();
  }

  void Reset() {
    for (auto& c : combs) c.Reset();
    for (auto& a : allpasses) a.Reset();
    inputLow.Reset(); inputDC.Reset(); outputLow.Reset();
  }

  double Process(double x) {
    // Spring tanks do not reproduce deep bass efficiently. Keep this mechanical
    // conditioning separate from the decoded tube electronics.
    x = inputLow.Low(inputDC.High(x));
    double y = 0.0;
    y += 0.34 * combs[0].Process(x);
    y += 0.28 * combs[1].Process(-x);
    y += 0.22 * combs[2].Process(x);
    y += 0.16 * combs[3].Process(-x);
    y = allpasses[0].Process(y);
    y = allpasses[1].Process(y);
    return outputLow.Low(y);
  }

private:
  std::array<DampedComb, 4> combs;
  std::array<Allpass, 2> allpasses;
  Pole inputLow, inputDC, outputLow;
};

class Pass9ReverbCircuit {
public:
  void Prepare(double fs) {
    rate = fs;

    // V4B reverb driver. The handwritten sheet gives 10K/2W in the driver
    // plate-supply branch, cathode 1K + .33uF, plate 339V and an uncertain
    // 3.8V cathode annotation. Derive the table's effective supply from the
    // measured Q point plus the decoded 10K DC drop, rather than inventing a
    // different plate resistor.
    const double driverVk = circuit::ReverbDriverCathodeVoltsUncertain;
    const double driverCurrent = driverVk / circuit::ReverbDriverCathodeOhms;
    const double driverSupply = circuit::ReverbDriverPlateVolts +
      driverCurrent * circuit::ReverbDriverPlateSupplyOhms;
    driver.Prepare(driverSupply, circuit::ReverbDriverPlateSupplyOhms,
      circuit::ReverbDriverCathodeOhms, driverVk);
    // Pass 9 resolves a 15K resistor in the reverb footswitch/cathode branch.
    // The exact switch drawing is not mechanically recoverable from the decode,
    // so the reduced model uses the only electrically defensible placement that
    // preserves all decoded parts: the .33uF cathode bypass returns through 15K
    // when the reverb switch is open and is effectively grounded when engaged.
    // The wet return is also muted when off, matching the footswitch function.
    driverCathode.Prepare(circuit::ReverbDriverPlateSupplyOhms,
      circuit::ReverbDriverCathodeOhms, 0.0, circuit::ReverbDriverCathodeBypass,
      circuit::ReverbFootswitchCathodeOhms, fs);

    // V3A reverb return: 100K plate, 3.3K + 15uF cathode, handwritten 2.6V
    // cathode point, 220K grid shunt, and .01uF output coupling.
    returnStage.Prepare(circuit::PreampSupplyC, circuit::ReverbV3APlateFeedOhms,
      circuit::ReverbV3ACathodeOhms, circuit::ReverbV3ACathodeVolts);
    returnCathode.Configure(circuit::ReverbV3APlateFeedOhms,
      circuit::ReverbV3ACathodeOhms, circuit::ReverbV3ACathodeBypass,
      0.0, 0.0, fs);
    returnCoupling.Coupling(circuit::ReverbV3AOutputCoupling,
      circuit::Parallel(circuit::ReverbV3APlateFeedOhms, circuit::TriodeRpEstimate),
      circuit::ReverbLevelOhms, fs);

    tank.Prepare(fs);
    Reset();
  }

  void Reset() {
    driverCathode.Reset(); returnCathode.Reset(); returnCoupling.Reset(); tank.Reset();
    active = false;
  }

  double Process(double toReverb, double level, double enabled) {
    // The transformer/tank primary impedance and tank transducer sensitivity are
    // absent from Pass 9. Normalize only at that boundary. Everything around it
    // (V4B cathode network, V3A return and 100K Reverb pot) follows decoded data.
    const double switchAmount = std::clamp(enabled, 0.0, 1.0);
    const double levelAmount = std::clamp(level, 0.0, 10.0);
    // Reverb is one of the most expensive blocks. When the level is at zero or
    // the footswitch is fully open there is no audible wet path, so keep it cold
    // instead of spending CPU on an inaudible spring network. Reset once on the
    // transition to avoid freezing an old spring tail and resurrecting it later.
    if (levelAmount <= 1.0e-9 || switchAmount <= 1.0e-9) {
      if (active) {
        driverCathode.Reset(); returnCathode.Reset(); returnCoupling.Reset(); tank.Reset();
        active = false;
      }
      return 0.0;
    }
    active = true;
    const double driverGrid = 0.10 * toReverb;
    const double drivenPlate = driver.Process(driverCathode.Process(driverGrid, switchAmount));
    const double mechanicalDrive = std::tanh(drivenPlate / 42.0);
    const double spring = tank.Process(mechanicalDrive);

    // The tank recovery-coil source impedance is not decoded. Give that missing
    // transducer boundary an explicitly provisional 2.5K source impedance so the
    // decoded V3A 220K grid shunt is an actual electrical load instead of dead
    // metadata. Changing this provisional number never changes the Pass-9 values.
    constexpr double provisionalTankOutputSourceOhms = 2500.0;
    constexpr double v3aGridLoad = circuit::ReverbV3AGridShuntOhms /
      (circuit::ReverbV3AGridShuntOhms + provisionalTankOutputSourceOhms);
    const double recovered = returnStage.Process(returnCathode.Process(0.42 * v3aGridLoad * spring));
    const double coupled = returnCoupling.Process(recovered);

    const double p = circuit::AudioTaper(levelAmount);
    // At Reverb=0 the rear-panel pot removes wet signal; the footswitch does the
    // same without changing the stored mix setting. R=100K is decoded, while the
    // exact wiper loading/mix resistor is not, so the pot law is used as the wet
    // gain rather than inventing another resistor.
    return coupled * p * switchAmount;
  }

private:
  double rate = 384000.0;
  TriodeStage driver, returnStage;
  SwitchableCathodeNetwork driverCathode;
  CathodeNetwork returnCathode;
  CircuitFilter returnCoupling;
  SpringTankModel tank;
  bool active = false;
};

} // namespace crunchy
