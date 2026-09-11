#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include "CircuitValues.h"

namespace crunchy {
// Passive network from the supplied IIC+ PDF, p.1. Linear trapezoidal nodal
// solution. R5=100k, C3=47n, C4=100n, C5=250p; pots T/B=250k, M=10k,
// Volume=1M and its 180p bright bypass. A/log laws use a 10% midpoint.
// Source resistance uses 150k || estimated triode rp; 10M shift leakage included.
class PassiveToneStack {
  static constexpr int N = 8;
  using Matrix = std::array<std::array<double, N>, N>;
  struct Capacitor { int a, b; double g = 0, voltage = 0, current = 0; };
public:
  void Prepare(double sampleRate) {
    rate = sampleRate;
    caps = {{{0,2}, {1,3}, {1,4}, {5,6}, {0,7}}};
    for (auto& c : caps) c.voltage = c.current = 0;
    Configure(5, 5, 5, 5, 0, 0);
  }
  void Configure(double treble, double bass, double middle, double volume,
                 double trebleShift, double bright) {
    Matrix g {};
    auto resistor = [&](int a, int b, double r) { Stamp(g, a, b, 1.0 / std::max(r, 1.0)); };
    // Nodes: plate, slope junction, treble top, treble bottom/bass top,
    // bass bottom/mid top, treble wiper/volume top, volume wiper.
    resistor(0, -1, kSourceOhms);
    resistor(0, 1, circuit::ToneFeedOhms);
    const double t = std::clamp(treble / 10., 0., 1.);
    resistor(2, 5, circuit::TreblePotOhms * (1 - t));
    resistor(5, 3, circuit::TreblePotOhms * t);
    resistor(3, 4, circuit::BassPotOhms * circuit::AudioTaper(bass));
    resistor(4, -1, circuit::MiddlePotOhms * std::clamp(middle / 10., 0., 1.));
    const double v = circuit::AudioTaper(volume);
    resistor(5, 6, circuit::Volume1Ohms * (1 - v)); resistor(6, -1, circuit::Volume1Ohms * v);
    // Extra 750p is connected through the 10M bleed when the LDR is off.
    resistor(7, 2, circuit::TrebleShiftResistor * (1 - std::clamp(trebleShift, 0., 1.)));
    const double capacitance[] = {circuit::TrebleCap, circuit::ToneUpperCap, circuit::ToneLowerCap,
                                  circuit::Volume1BrightCap * bright, circuit::TrebleShiftCap};
    for (int i = 0; i < 5; ++i) {
      // Reset the branch current on an exactly disconnected bright capacitor.
      if (capacitance[i] == 0) caps[i].current = 0;
      caps[i].g = 2 * rate * capacitance[i];
      Stamp(g, caps[i].a, caps[i].b, caps[i].g);
    }
    inverse = Invert(g);

    // The right-hand side has only six independent terms: the source sample
    // plus the five capacitor histories.  Pre-combine the dense 8x8 inverse
    // into exactly the five capacitor voltages and the output voltage we need.
    // This is algebraically the same nodal solve, but avoids constructing all
    // eight node voltages and doing 64 multiplies on every oversampled sample.
    std::array<std::array<double, 6>, N> basis{};
    for (int row = 0; row < N; ++row) {
      basis[row][0] = inverse[row][0] / kSourceOhms;
      basis[row][1] = inverse[row][0] - inverse[row][2];
      basis[row][2] = inverse[row][1] - inverse[row][3];
      basis[row][3] = inverse[row][1] - inverse[row][4];
      basis[row][4] = inverse[row][5] - inverse[row][6];
      basis[row][5] = inverse[row][0] - inverse[row][7];
    }
    constexpr int voltageA[6] = {0, 1, 1, 5, 0, 6};
    constexpr int voltageB[6] = {2, 3, 4, 6, 7, -1};
    for (int quantity = 0; quantity < 6; ++quantity) {
      for (int term = 0; term < 6; ++term) {
        const double a = basis[voltageA[quantity]][term];
        const double b = voltageB[quantity] >= 0 ? basis[voltageB[quantity]][term] : 0.0;
        response[quantity][term] = a - b;
      }
    }
  }
  double Process(double input) {
    const double h0 = caps[0].g * caps[0].voltage + caps[0].current;
    const double h1 = caps[1].g * caps[1].voltage + caps[1].current;
    const double h2 = caps[2].g * caps[2].voltage + caps[2].current;
    const double h3 = caps[3].g * caps[3].voltage + caps[3].current;
    const double h4 = caps[4].g * caps[4].voltage + caps[4].current;
    const double terms[6] = {input, h0, h1, h2, h3, h4};
    double v[6] = {};
    for (int quantity = 0; quantity < 6; ++quantity) {
      const auto& c = response[quantity];
      v[quantity] = c[0] * terms[0] + c[1] * terms[1] + c[2] * terms[2] +
                    c[3] * terms[3] + c[4] * terms[4] + c[5] * terms[5];
    }
    for (int i = 0; i < 5; ++i) {
      auto& c = caps[i];
      c.current = c.g * (v[i] - c.voltage) - c.current;
      c.voltage = v[i];
    }
    return v[5];
  }
private:
  static void Stamp(Matrix& m, int a, int b, double y) {
    m[a][a] += y;
    if (b >= 0) { m[b][b] += y; m[a][b] -= y; m[b][a] -= y; }
  }
  static Matrix Invert(Matrix a) {
    Matrix inv {};
    for (int i = 0; i < N; ++i) inv[i][i] = 1;
    for (int k = 0; k < N; ++k) {
      int pivot = k;
      for (int i = k + 1; i < N; ++i)
        if (std::abs(a[i][k]) > std::abs(a[pivot][k])) pivot = i;
      std::swap(a[k], a[pivot]); std::swap(inv[k], inv[pivot]);
      const double divisor = a[k][k];
      for (int j = 0; j < N; ++j) { a[k][j] /= divisor; inv[k][j] /= divisor; }
      for (int i = 0; i < N; ++i) if (i != k) {
        const double factor = a[i][k];
        for (int j = 0; j < N; ++j) {
          a[i][j] -= factor * a[k][j]; inv[i][j] -= factor * inv[k][j];
        }
      }
    }
    return inv;
  }
  static constexpr double kSourceOhms = circuit::Parallel(circuit::V1APlateOhms, circuit::TriodeRpEstimate);
  double rate = 192000;
  Matrix inverse {};
  std::array<std::array<double, 6>, 6> response {};
  std::array<Capacitor, 5> caps {};
};
}
