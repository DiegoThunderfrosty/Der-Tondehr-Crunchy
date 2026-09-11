#pragma once
#include "IPlug_include_in_plug_hdr.h"
#include "dsp/CrunchyAmp.h"
#include "dsp/PeakMeterState.h"

#include <atomic>

using namespace iplug;
using namespace igraphics;

enum EControlTags {
  kCtrlTagInputMeter = 1000,
  kCtrlTagOutputMeter,
  kCtrlTagTrebleShift,
  kCtrlTagLeadDrive,
  kCtrlTagLeadMaster,
  kCtrlTagLeadBright,
  // Standalone-only non-parameter selector. Appended to keep existing tags stable.
  kCtrlTagStandaloneMonoInput
};

class DerTondehrCrunchy final : public Plugin {
public:
  explicit DerTondehrCrunchy(const InstanceInfo& info);
#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void OnReset() override;
#endif
#if defined(APP_API)
  void SetStandaloneMonoInput(int inputIndex);
  int StandaloneMonoInput() const;
#endif
#if IPLUG_EDITOR
  bool OnHostRequestingSupportedViewConfiguration(int width, int height) override;
  bool ConstrainEditorResize(int& width, int& height) const override;
  void OnParentWindowResize(int width, int height) override;
  void OnUIOpen() override;
#endif
private:
#if IPLUG_DSP
  crunchy::Parameters ReadParameters();
  crunchy::Amp mAmp;
  // Host/API-neutral meter mailboxes. The audio thread only accumulates exact
  // sample peaks; each IGraphics meter control consumes its own mailbox directly
  // on the display tick. No APP/VST3 idle/message bridge is involved.
  crunchy::PeakMeterState mInputMeterState;
  crunchy::PeakMeterState mOutputMeterState;
#if defined(APP_API)
  // Standalone opens a contiguous two-channel hardware pair. This selects which
  // member is the guitar/DI source and crossfades over ~8 ms to avoid clicks.
  std::atomic<int> mStandaloneMonoInputTarget {0};
  double mStandaloneMonoInputBlend = 0.0;
#endif
#endif

#if defined(APP_API)
  void LoadStandaloneAudioPreferences();
  void SaveStandaloneAudioPreferences() const;
#endif
};
