#pragma once

#include <algorithm>
#include <cmath>

namespace crunchy::controls {

// Graphic-EQ policy: a fresh instance uses the classic V contour requested for
// this build, while the GUI double-click gesture always resets an individual
// band to electrical unity (0 dB).
inline constexpr double GraphicEqStartupDb[5] = {6.0, 0.0, -6.0, 0.0, 6.0};
inline constexpr double GraphicEqDoubleClickResetDb = 0.0;

inline double Switch(double value)
{
  return std::clamp(value, 0.0, 1.0);
}

// Pass-9 / schematic-faithful LDR1 logic. The TREBLE SHIFT pull supplies the
// LDR1 LED, but its return shares the LEAD switching bus with LDR2/LDR3/LDR4.
// Therefore the 750pF / 10M Treble Shift network can only become active while
// LEAD is selected. The parameter value is preserved in Rhythm so the user's
// setting returns unchanged when switching back to Lead.
inline double TrebleShift(double trebleShift, double leadMode)
{
  return Switch(trebleShift) * Switch(leadMode);
}

// Legacy 0.10.2 mapping retained for old tests/state documentation.  The actual
// 0.10.3 panel uses one three-position parameter, matching the physical control.
inline double EqEngagement(double eqAuto, double eqIn, double leadMode)
{
  const double automatic = Switch(eqAuto);
  const double alwaysIn = Switch(eqIn);
  const double leadOnly = Switch(leadMode);
  return alwaysIn + automatic * (leadOnly - alwaysIn);
}

// New single three-position GUI:
//   0 = EQ AUTO / LEAD
//   1 = EQ OUT
//   2 = EQ IN / ALL
// Keep the function exact at the enum positions.  The caller smooths the final
// engagement amount rather than interpolating the enum through the middle state.
inline double EqModeEngagement(double mode, double leadMode)
{
  const int position = static_cast<int>(std::clamp(std::lround(mode), 0L, 2L));
  if (position == 0) return Switch(leadMode);
  if (position == 1) return 0.0;
  return 1.0;
}

// Pull switches short a return resistor around a cathode-bypass capacitor.
// Bass Shift and Deep use 15 kOhm in the handwritten sheet; Lead Bright uses
// 22 kOhm. One ohm is used instead of zero for numerical conditioning.
inline double SwitchedReturn(double enabled, double offOhms)
{
  return std::max(1.0, offOhms * (1.0 - Switch(enabled)));
}
inline double Switched15kReturn(double enabled)
{
  return SwitchedReturn(enabled, 15000.0);
}

} // namespace crunchy::controls
