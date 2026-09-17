#pragma once
#include "IPlug_include_in_plug_hdr.h"
#include "dsp/CrunchyAmp.h"
#include "dsp/PeakMeterState.h"

#include <atomic>
#include <cstdint>
#include <string>

class CrunchyPresetManagerControl;

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
  kCtrlTagStandaloneMonoInput,
  kCtrlTagPresetManager
};

class DerTondehrCrunchy final : public Plugin {
  friend class CrunchyPresetManagerControl;
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
  static constexpr const char* kPresetExtension = ".dtcpreset";
  static constexpr int kPresetMaxEntriesPerFolder = 4096;

  bool SavePresetFile(const std::string& path);
  bool LoadPresetFile(const std::string& path);
  bool DeleteSelectedPresetFile();
  bool PresetRootAvailable() const;
  bool PresetPathIsInsideRoot(const std::string& path) const;
  std::string PresetPreferencesPath() const;
  void LoadPresetDirectoryPreference();
  bool SavePresetDirectoryPreferenceValue(const std::string& root) const;
  bool ComputePresetFileIdentity(const std::string& path, std::uint64_t& identity) const;
  void NotifyHostCustomPresetStateChanged();
#if IPLUG_EDITOR
  void PromptSetPresetDirectory();
  void RemovePresetDirectory();
  void PromptSavePreset();
  void MarkPresetUIChanged();
  void PollPresetDirectoryChanges();
  std::uint64_t ComputeCurrentPresetDirectoryFingerprint() const;
  bool ArmSelectedPresetDelete();
  void ClearPresetDeleteIdentity();
#endif

  std::string mPresetRootDirectory;
  std::string mPresetCurrentDirectory;
  std::string mPresetSelectedPath;
  std::string mPresetStatusMessage;
  bool mPresetStatusIsError = false;
  std::atomic<unsigned int> mPresetUIRevision {0};
  std::uint64_t mPresetDirectoryFingerprint = 0u;
  bool mPresetDirectoryFingerprintValid = false;
  std::int64_t mPresetLastDirectoryPollMs = 0;
  bool mPresetBrowserOpen = false;
  std::string mPresetPendingOverwritePath;
  std::uint64_t mPresetPendingOverwriteIdentity = 0u;
  bool mPresetPendingOverwriteValid = false;
  std::uint64_t mPresetDeleteIdentity = 0u;
  bool mPresetDeleteIdentityValid = false;
  std::atomic<bool> mPresetRecallInProgress {false};
  std::atomic<int> mPresetAudioBlocksInFlight {0};

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
