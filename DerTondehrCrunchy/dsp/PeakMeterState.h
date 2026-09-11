#pragma once

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crunchy {

// Exact digital sample-peak result. This is deliberately sample peak, not RMS
// and not an envelope estimate. "clipped" follows the DAW-style floating-point
// threshold |sample| >= 1.0 (0 dBFS).
struct SamplePeakMeasurement {
  float peak = 0.f;
  bool clipped = false;
};

template<typename T>
inline SamplePeakMeasurement MeasureSamplePeak(T** buffers, int nFrames, int nChans) noexcept {
  SamplePeakMeasurement result;
  if (!buffers || nFrames <= 0 || nChans <= 0)
    return result;

  for (int c = 0; c < nChans; ++c) {
    const T* channel = buffers[c];
    if (!channel) continue;
    for (int s = 0; s < nFrames; ++s) {
      const double value = static_cast<double>(channel[s]);
      if (!std::isfinite(value)) continue;
      const float magnitude = static_cast<float>(std::abs(value));
      result.peak = std::max(result.peak, magnitude);
      result.clipped = result.clipped || magnitude >= 1.f;
    }
  }
  return result;
}

// Apply a user-visible digital gain to a previously measured sample peak.
// This is used by the Input meter so its 0 dB position reports the raw host
// peak, while moving the visible INPUT trim moves the meter by exactly the same
// number of dB. The amplifier's hidden +12 dB working calibration is
// deliberately excluded from this meter path.
inline SamplePeakMeasurement ApplySamplePeakGain(SamplePeakMeasurement m, double gain) noexcept {
  if (!std::isfinite(gain) || gain < 0.0)
    gain = 0.0;
  const double scaled = static_cast<double>(m.peak) * gain;
  m.peak = std::isfinite(scaled) ? static_cast<float>(scaled) : 0.f;
  m.clipped = std::isfinite(scaled) && scaled >= 1.0;
  return m;
}

// Realtime-safe sample-peak mailbox shared by the processor and its IGraphics
// control. 0.10.14 packs peak/data/clip into ONE 64-bit atomic word. That makes
// consuming a GUI frame an indivisible exchange: a peak arriving concurrently
// with a UI refresh cannot be split from its clip flag or accidentally cleared.
//
// This class contains no host/API assumptions. APP and the normal single-
// component VST3 target use the exact same instance and exact same code path.
class PeakMeterState {
public:
  struct Snapshot {
    float peak = 0.f;
    bool clipped = false;
    bool hasData = false;
  };

  PeakMeterState() noexcept = default;
  PeakMeterState(const PeakMeterState&) = delete;
  PeakMeterState& operator=(const PeakMeterState&) = delete;

  void Reset() noexcept {
    mPacked.store(0, std::memory_order_release);
  }

  void Accumulate(float peak, bool clipped) noexcept {
    const float safePeak = (std::isfinite(peak) && peak > 0.f) ? peak : 0.f;
    uint64_t current = mPacked.load(std::memory_order_relaxed);

    for (;;) {
      const float currentPeak = BitsToFloat(static_cast<uint32_t>(current & kPeakMask));
      const float mergedPeak = std::max(currentPeak, safePeak);
      const uint64_t flags = (current & kClipMask) |
                             kHasDataMask |
                             (clipped ? kClipMask : 0ull);
      const uint64_t desired = static_cast<uint64_t>(FloatToBits(mergedPeak)) | flags;

      if (mPacked.compare_exchange_weak(current, desired,
                                        std::memory_order_release,
                                        std::memory_order_relaxed))
        return;
    }
  }

  Snapshot Consume() noexcept {
    return Decode(mPacked.exchange(0, std::memory_order_acq_rel));
  }

  Snapshot Peek() const noexcept {
    return Decode(mPacked.load(std::memory_order_acquire));
  }

private:
  static constexpr uint64_t kPeakMask = 0x00000000FFFFFFFFull;
  static constexpr uint64_t kClipMask = 0x0000000100000000ull;
  static constexpr uint64_t kHasDataMask = 0x0000000200000000ull;

  static uint32_t FloatToBits(float value) noexcept {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  }

  static float BitsToFloat(uint32_t bits) noexcept {
    float value = 0.f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  static Snapshot Decode(uint64_t packed) noexcept {
    Snapshot s;
    s.hasData = (packed & kHasDataMask) != 0;
    s.clipped = (packed & kClipMask) != 0;
    s.peak = BitsToFloat(static_cast<uint32_t>(packed & kPeakMask));
    if (!std::isfinite(s.peak) || s.peak < 0.f)
      s.peak = 0.f;
    return s;
  }

  static_assert(std::atomic<uint64_t>::is_always_lock_free,
                "Crunchy meters require lock-free 64-bit atomics on the audio thread.");
  std::atomic<uint64_t> mPacked {0};
};

} // namespace crunchy
