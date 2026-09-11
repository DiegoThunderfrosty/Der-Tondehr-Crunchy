#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace crunchy::editor {

// Preserve the established six-phase editor ladder. The original
// ratios were defined relative to the monitor-safe maximum, with phase 4
// (index 3) as NORMAL.  Crunchy anchors that NORMAL phase to its current native
// 1180x520 design whenever the monitor can safely accommodate it.
inline constexpr std::array<float, 6> kPhaseRatios {
  0.64f, 0.72f, 0.80f, 0.87f, 0.94f, 1.00f
};
inline constexpr int kNormalPhaseIndex = 3;
inline constexpr float kNormalPhaseRatio = kPhaseRatios[kNormalPhaseIndex];

// If the screen can accommodate this largest phase, NORMAL is exactly 1.0x.
// The two larger phases then become ~1.08046x and ~1.14943x, while the three
// smaller phases retain the established spacing below 1.0x.
inline constexpr float kNativeSafeMaximum = 1.0f / kNormalPhaseRatio;

// Screen fitting uses the monitor work area
// (taskbar excluded), leave breathing room around the editor, and never make a
// normal large-screen profile bigger than the six-phase ladder above.
inline constexpr float kWorkAreaWidthFraction = 0.82f;
inline constexpr float kWorkAreaHeightFraction = 0.76f;

// A very small fallback floor for unusually small/remote desktops.  Normal
// laptop/desktop profiles stay well above this value.
inline constexpr float kAbsoluteMinimumDrawScale = 0.35f;
inline constexpr float kScaleGrid = 1024.0f;

struct ScaleProfile {
  std::array<float, 6> scales {};
  float workAreaWidth = 1920.0f;
  float workAreaHeight = 1080.0f;
  float screenSafeMaximum = kNativeSafeMaximum;
};

inline float QuantizeScaleDown(float scale) {
  return std::floor(std::max(scale, 1.0f / kScaleGrid) * kScaleGrid + 1.0e-6f) / kScaleGrid;
}

inline ScaleProfile BuildScaleProfile(float workAreaWidth, float workAreaHeight,
                                      float designWidth, float designHeight) {
  ScaleProfile profile;
  profile.workAreaWidth = std::max(1.0f, workAreaWidth);
  profile.workAreaHeight = std::max(1.0f, workAreaHeight);

  const float widthLimit = (profile.workAreaWidth * kWorkAreaWidthFraction) / std::max(1.0f, designWidth);
  const float heightLimit = (profile.workAreaHeight * kWorkAreaHeightFraction) / std::max(1.0f, designHeight);
  const float rawSafeMaximum = std::min({kNativeSafeMaximum, widthLimit, heightLimit});
  profile.screenSafeMaximum = std::max(kAbsoluteMinimumDrawScale, rawSafeMaximum);

  const bool canUseNativeProfile = profile.screenSafeMaximum >= (kNativeSafeMaximum - 1.0e-5f);

  float previous = 0.0f;
  for (std::size_t i = 0; i < profile.scales.size(); ++i) {
    float scale = 0.0f;
    if (canUseNativeProfile) {
      // Exact native anchoring: index 3 is precisely 1.0x when the monitor has
      // enough work area. This is the current Crunchy size and the requested
      // default scale.
      scale = kPhaseRatios[i] / kNormalPhaseRatio;
      if (static_cast<int>(i) == kNormalPhaseIndex)
        scale = 1.0f;
    }
    else {
      // On smaller/DPI-scaled displays compress the complete six-state ladder
      // underneath the monitor-safe maximum.
      scale = QuantizeScaleDown(profile.screenSafeMaximum * kPhaseRatios[i]);
    }

    scale = std::max(kAbsoluteMinimumDrawScale, scale);

    // Retain distinct ordered states whenever the available range allows it.
    if (i > 0 && scale <= previous) {
      const float next = previous + (1.0f / kScaleGrid);
      scale = std::min(profile.screenSafeMaximum, next);
    }

    profile.scales[i] = scale;
    previous = scale;
  }

  if (canUseNativeProfile) {
    profile.scales[kNormalPhaseIndex] = 1.0f;
    profile.scales.back() = kNativeSafeMaximum;
    profile.screenSafeMaximum = kNativeSafeMaximum;
  }
  else {
    profile.scales.back() = QuantizeScaleDown(profile.screenSafeMaximum);
  }

  return profile;
}

inline int NearestScaleIndex(float scale, const ScaleProfile& profile) {
  int best = 0;
  float bestDistance = std::abs(scale - profile.scales[0]);
  for (int i = 1; i < static_cast<int>(profile.scales.size()); ++i) {
    const float distance = std::abs(scale - profile.scales[static_cast<std::size_t>(i)]);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }
  return best;
}

inline int SelectStandaloneResizePhase(float requestedLogicalWidth, float requestedLogicalHeight,
                                      float designWidth, float designHeight,
                                      float currentScale, const ScaleProfile& profile) {
  const int currentIndex = NearestScaleIndex(currentScale, profile);
  const float currentPhaseScale = profile.scales[static_cast<std::size_t>(currentIndex)];
  const float widthScale = requestedLogicalWidth / std::max(1.0f, designWidth);
  const float heightScale = requestedLogicalHeight / std::max(1.0f, designHeight);

  // A Windows standalone border can be dragged on just one axis. Using the
  // smaller axis (the hosted/VST3 policy) interprets a right-edge expansion as
  // "no growth" because the unchanged height remains the limiting axis. The
  // parent then becomes wider than the IGraphics child and Windows exposes a
  // blank strip. For APP resizing, follow the axis the user actually moved most.
  const float widthDelta = widthScale - currentPhaseScale;
  const float heightDelta = heightScale - currentPhaseScale;
  const bool widthDominant = std::abs(widthDelta) >= std::abs(heightDelta);
  const float dominantDelta = widthDominant ? widthDelta : heightDelta;
  const float requestedScale = widthDominant ? widthScale : heightScale;

  int targetIndex = NearestScaleIndex(requestedScale, profile);

  // Never jump in the opposite direction to the user's dominant border motion.
  // This specifically prevents the standalone from shrinking while a side is
  // being dragged outward (and vice versa).
  if (dominantDelta > 1.0e-5f)
    targetIndex = std::max(targetIndex, currentIndex);
  else if (dominantDelta < -1.0e-5f)
    targetIndex = std::min(targetIndex, currentIndex);
  else
    targetIndex = currentIndex;

  return std::clamp(targetIndex, 0, static_cast<int>(profile.scales.size()) - 1);
}

} // namespace crunchy::editor
