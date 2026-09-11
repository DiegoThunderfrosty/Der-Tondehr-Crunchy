#include "ui/EditorScale.h"
#include "config.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace crunchy::editor;

static bool Near(float a, float b, float eps = 1.0e-4f) {
  return std::abs(a - b) <= eps;
}

static void AssertOrdered(const ScaleProfile& p) {
  for (std::size_t i = 1; i < p.scales.size(); ++i)
    assert(p.scales[i] > p.scales[i - 1]);
}

int main() {
  constexpr float width = 1180.0f;
  constexpr float height = 520.0f;

  // Common 1080p-class work area: Crunchy's existing 1180x520 canvas must be
  // the NORMAL/default phase, with three smaller and two larger states.
  const auto desktop = BuildScaleProfile(1920.0f, 1040.0f, width, height);
  AssertOrdered(desktop);
  assert(Near(desktop.scales[kNormalPhaseIndex], 1.0f));
  for (int i = 0; i < kNormalPhaseIndex; ++i)
    assert(desktop.scales[static_cast<std::size_t>(i)] < 1.0f);
  for (int i = kNormalPhaseIndex + 1; i < 6; ++i)
    assert(desktop.scales[static_cast<std::size_t>(i)] > 1.0f);
  assert(Near(desktop.scales.back(), kNativeSafeMaximum));
  assert(NearestScaleIndex(1.0f, desktop) == kNormalPhaseIndex);

  // Laptop-sized work area: the complete ladder must compress to the screen
  // instead of forcing the native width off-screen.
  const auto laptop = BuildScaleProfile(1366.0f, 728.0f, width, height);
  AssertOrdered(laptop);
  assert(laptop.scales.back() <= (1366.0f * kWorkAreaWidthFraction / width) + 1.0e-3f);
  assert(laptop.scales.back() <= (728.0f * kWorkAreaHeightFraction / height) + 1.0e-3f);
  assert(laptop.scales[kNormalPhaseIndex] < 1.0f);

  // Windows 125% DPI turns a 1920x1040 physical work area into roughly
  // 1536x832 logical UI units. It should also choose a smaller NORMAL phase.
  const auto dpi125 = BuildScaleProfile(1536.0f, 832.0f, width, height);
  AssertOrdered(dpi125);
  assert(dpi125.scales[kNormalPhaseIndex] < 1.0f);
  assert(dpi125.scales.back() <= 1536.0f * kWorkAreaWidthFraction / width + 1.0e-3f);

  // iPlug2's Windows standalone uses PLUG_MAX_WIDTH/HEIGHT as the TOP-LEVEL
  // WM_GETMINMAXINFO tracking ceiling. Those limits therefore need generous
  // room for non-client chrome and DPI scaling; they must not equal the largest
  // 1180x520 client-area phase. The six-phase profile remains the real cap.
  assert(PLUG_MAX_WIDTH >= 4096);
  assert(PLUG_MAX_HEIGHT >= 2160);
  const int largestNativeClientW = static_cast<int>(std::ceil(width * kNativeSafeMaximum));
  const int largestNativeClientH = static_cast<int>(std::ceil(height * kNativeSafeMaximum));
  assert(PLUG_MAX_WIDTH > largestNativeClientW + 256);
  assert(PLUG_MAX_HEIGHT > largestNativeClientH + 256);

  // Larger monitors must not inflate startup/default beyond the current native
  // Crunchy size; only the two explicit post-default phases go larger.
  const auto large = BuildScaleProfile(2560.0f, 1400.0f, width, height);
  AssertOrdered(large);
  assert(Near(large.scales[kNormalPhaseIndex], 1.0f));
  assert(Near(large.scales.back(), kNativeSafeMaximum));

  // Windows standalone border resizing is one-axis capable. A horizontal grow
  // from NORMAL must never be interpreted as a shrink/no-op by the unchanged
  // height, and a horizontal shrink must never select a larger phase.
  {
    const float current = desktop.scales[kNormalPhaseIndex];
    const float next = desktop.scales[kNormalPhaseIndex + 1];
    const float prev = desktop.scales[kNormalPhaseIndex - 1];

    const float growWidth = width * (0.25f * current + 0.75f * next);
    const int growIndex = SelectStandaloneResizePhase(
      growWidth, height * current, width, height, current, desktop);
    assert(growIndex == kNormalPhaseIndex + 1);

    const float shrinkWidth = width * (0.25f * current + 0.75f * prev);
    const int shrinkIndex = SelectStandaloneResizePhase(
      shrinkWidth, height * current, width, height, current, desktop);
    assert(shrinkIndex == kNormalPhaseIndex - 1);

    // Small one-axis drags that have not crossed the next phase midpoint snap
    // back to the current exact client size rather than exposing blank space.
    const int smallGrow = SelectStandaloneResizePhase(
      width * (current + 0.01f), height * current, width, height, current, desktop);
    assert(smallGrow == kNormalPhaseIndex);

    // Direction lock: even strongly mismatched aspect requests cannot make an
    // outward dominant drag choose a smaller state or an inward drag a larger one.
    const int outward = SelectStandaloneResizePhase(
      width * next, height * (current - 0.01f), width, height, current, desktop);
    assert(outward >= kNormalPhaseIndex);
    const int inward = SelectStandaloneResizePhase(
      width * prev, height * (current + 0.01f), width, height, current, desktop);
    assert(inward <= kNormalPhaseIndex);
  }

  std::cout << "Crunchy adaptive six-phase editor scale + standalone direction: PASS\n";
  return 0;
}
