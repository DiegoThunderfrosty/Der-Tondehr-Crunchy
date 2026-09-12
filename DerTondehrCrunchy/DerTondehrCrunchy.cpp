#include "DerTondehrCrunchy.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"
#include "ui/EditorScale.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#if defined(APP_API)
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace crunchy;

#if IPLUG_EDITOR
namespace {

#if defined(_WIN32)
static float CrunchyPrimarySystemDPIScale() {
  float scale = 1.0f;
  if (HDC dc = ::GetDC(nullptr)) {
    const int dpi = ::GetDeviceCaps(dc, LOGPIXELSX);
    ::ReleaseDC(nullptr, dc);
    if (dpi > 0)
      scale = static_cast<float>(dpi) / 96.0f;
  }
  return std::max(0.5f, scale);
}
#endif

// Monitor-aware work-area policy. Windows
// uses the monitor that actually owns the editor and excludes the taskbar. A
// primary-monitor estimate is used before the native window exists.
static bool CrunchyGetEditorMonitorWorkArea(IGraphics* graphics,
                                            float& logicalWidth,
                                            float& logicalHeight) {
#if defined(_WIN32)
  HWND window = graphics ? reinterpret_cast<HWND>(graphics->GetWindow()) : nullptr;
  HMONITOR monitor = window
    ? ::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST)
    : ::MonitorFromPoint(POINT {0, 0}, MONITOR_DEFAULTTOPRIMARY);

  MONITORINFO info {};
  info.cbSize = sizeof(info);
  if (monitor && ::GetMonitorInfo(monitor, &info)) {
    const float platformScale = graphics
      ? std::max(0.5f, graphics->GetPlatformWindowScale())
      : CrunchyPrimarySystemDPIScale();
    const float physicalWidth = static_cast<float>(info.rcWork.right - info.rcWork.left);
    const float physicalHeight = static_cast<float>(info.rcWork.bottom - info.rcWork.top);
    if (physicalWidth > 0.0f && physicalHeight > 0.0f) {
      logicalWidth = physicalWidth / platformScale;
      logicalHeight = physicalHeight / platformScale;
      return true;
    }
  }
#endif

  logicalWidth = 1920.0f;
  logicalHeight = 1080.0f;
  return false;
}

static crunchy::editor::ScaleProfile CrunchyBuildEditorScaleProfile(IGraphics* graphics) {
  float workWidth = 1920.0f;
  float workHeight = 1080.0f;
  CrunchyGetEditorMonitorWorkArea(graphics, workWidth, workHeight);
  return crunchy::editor::BuildScaleProfile(workWidth, workHeight,
                                             static_cast<float>(PLUG_WIDTH),
                                             static_cast<float>(PLUG_HEIGHT));
}

#if defined(_WIN32) && defined(APP_API)
// In the Windows APP wrapper the IGraphics render HWND is a child of the
// standalone dialog. A free Win32 border drag can therefore resize the parent
// client area without changing the child at all. If the selected draw scale is
// unchanged, IGraphicsWin::PlatformResize() sees dw/dh == 0 and has no reason to
// touch the parent, leaving exactly the white strip visible in 0.10.17.
//
// Resolve that at the ownership boundary: make the top-level window's CLIENT
// area exactly match the selected Crunchy phase, then resize only the IGraphics
// child. Computing the non-client delta from the live HWND automatically
// includes title bar, menu, borders and current Windows DPI/theme metrics.
static HWND CrunchyStandaloneRootWindow(IGraphics* graphics) {
  if (!graphics)
    return nullptr;

  HWND child = reinterpret_cast<HWND>(graphics->GetWindow());
  if (!child)
    return nullptr;

  HWND root = ::GetAncestor(child, GA_ROOT);
  return root ? root : ::GetParent(child);
}

static bool CrunchyResizeStandaloneClientExactly(IGraphics* graphics,
                                                  int targetClientWidth,
                                                  int targetClientHeight) {
  HWND root = CrunchyStandaloneRootWindow(graphics);
  if (!root || targetClientWidth <= 0 || targetClientHeight <= 0)
    return false;

  RECT client {};
  RECT window {};
  if (!::GetClientRect(root, &client) || !::GetWindowRect(root, &window))
    return false;

  const int clientWidth = client.right - client.left;
  const int clientHeight = client.bottom - client.top;
  if (std::abs(clientWidth - targetClientWidth) <= 1 &&
      std::abs(clientHeight - targetClientHeight) <= 1)
    return true;

  const int outerWidth = window.right - window.left;
  const int outerHeight = window.bottom - window.top;
  const int nonClientWidth = std::max(0, outerWidth - clientWidth);
  const int nonClientHeight = std::max(0, outerHeight - clientHeight);

  const int targetOuterWidth = targetClientWidth + nonClientWidth;
  const int targetOuterHeight = targetClientHeight + nonClientHeight;
  return ::SetWindowPos(root, nullptr, 0, 0, targetOuterWidth, targetOuterHeight,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
}
#endif

// VST3/DAW hosts own the outer editor rectangle. Snap the host-requested size
// to one of the same six screen-aware phases before it is applied, rather than
// trying to resize the host again from OnParentWindowResize().
static bool CrunchyConstrainHostResizeToPhase(IGraphics* graphics,
                                               int& width, int& height) {
  if (width <= 0 || height <= 0)
    return false;

  const auto profile = CrunchyBuildEditorScaleProfile(graphics);
#if defined(_WIN32)
  const float platformScale = graphics
    ? std::max(0.5f, graphics->GetPlatformWindowScale())
    : CrunchyPrimarySystemDPIScale();
#else
  const float platformScale = graphics ? std::max(0.5f, graphics->GetPlatformWindowScale()) : 1.0f;
#endif

  int bestWidth = width;
  int bestHeight = height;
  double bestDistance = std::numeric_limits<double>::max();

  for (const float drawScale : profile.scales) {
    const int candidateWidth = std::max(1, static_cast<int>(std::lround(
      static_cast<double>(PLUG_WIDTH) * drawScale * platformScale)));
    const int candidateHeight = std::max(1, static_cast<int>(std::lround(
      static_cast<double>(PLUG_HEIGHT) * drawScale * platformScale)));

    const double dw = static_cast<double>(width - candidateWidth) / static_cast<double>(candidateWidth);
    const double dh = static_cast<double>(height - candidateHeight) / static_cast<double>(candidateHeight);
    const double distance = dw * dw + dh * dh;
    if (distance < bestDistance) {
      bestDistance = distance;
      bestWidth = candidateWidth;
      bestHeight = candidateHeight;
    }
  }

  const bool alreadyValid = std::abs(width - bestWidth) <= 1 && std::abs(height - bestHeight) <= 1;
  width = bestWidth;
  height = bestHeight;
  return alreadyValid;
}

class CrunchySteppedEditorResizeControl final : public IControl {
public:
  explicit CrunchySteppedEditorResizeControl(const IRECT& graphicsBounds)
  : IControl(graphicsBounds.GetFromBRHC(40.0f, 30.0f)) {
    SetTooltip("Resize: 6 screen-aware sizes / double-click = default");
  }

  void Draw(IGraphics& g) override {
    const IRECT b = mRECT.GetPadded(-3.0f);
    const auto profile = CrunchyBuildEditorScaleProfile(GetUI());
    const int current = mDragging
      ? mTargetIndex
      : crunchy::editor::NearestScaleIndex(GetUI()->GetDrawScale(), profile);

    const float dotY = b.T + 5.0f;
    const float dotLeft = b.L + 2.5f;
    const float dotRight = b.R - 2.5f;
    const float spacing = (dotRight - dotLeft) / 5.0f;
    for (int i = 0; i < 6; ++i) {
      const float x = dotLeft + spacing * static_cast<float>(i);
      const bool active = i == current;
      if (active)
        g.FillCircle(IColor(80, 203, 190, 117), x, dotY, 2.7f);
      g.FillCircle(active ? IColor(255, 238, 234, 218) : IColor(205, 160, 166, 152),
                   x, dotY, active ? 1.55f : 1.15f);
    }

    const float tri = 12.0f;
    const float l = b.R - tri;
    const float t = b.B - tri;
    g.FillTriangle(IColor(110, 0, 0, 0), l + 1.0f, b.B, b.R, t + 1.0f, b.R, b.B);
    g.FillTriangle(IColor(210, 67, 71, 63), l, b.B, b.R, t, b.R, b.B);
    g.DrawLine(IColor(235, 203, 190, 117), l + 1.0f, b.B - 1.0f,
               b.R - 1.0f, t + 1.0f, nullptr, 1.0f);
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override {
    mStartX = x;
    mStartY = y;
    mDragProfile = CrunchyBuildEditorScaleProfile(GetUI());
    mStartIndex = crunchy::editor::NearestScaleIndex(GetUI()->GetDrawScale(), mDragProfile);
    mTargetIndex = mStartIndex;
    mDragging = true;
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float, float, const IMouseMod&) override {
    if (!mDragging)
      return;

    const float projected = 0.5f * ((x - mStartX) + (y - mStartY));
    constexpr float kPixelsPerStep = 34.0f;
    int delta = 0;
    if (projected >= 0.0f)
      delta = static_cast<int>(std::floor((projected + 0.5f * kPixelsPerStep) / kPixelsPerStep));
    else
      delta = static_cast<int>(std::ceil((projected - 0.5f * kPixelsPerStep) / kPixelsPerStep));

    const int next = std::clamp(mStartIndex + delta, 0, 5);
    if (next != mTargetIndex) {
      mTargetIndex = next;
      SetDirty(false);
    }
  }

  void OnMouseUp(float, float, const IMouseMod&) override {
    if (!mDragging)
      return;

    mDragging = false;
    const auto currentProfile = CrunchyBuildEditorScaleProfile(GetUI());
    const int current = crunchy::editor::NearestScaleIndex(GetUI()->GetDrawScale(), currentProfile);
    const int target = std::clamp(mTargetIndex, 0, 5);
    if (target != current) {
      GetUI()->Resize(PLUG_WIDTH, PLUG_HEIGHT,
                      mDragProfile.scales[static_cast<std::size_t>(target)]);
    }
    SetDirty(false);
  }

  void OnMouseDblClick(float, float, const IMouseMod&) override {
    mDragging = false;
    const auto profile = CrunchyBuildEditorScaleProfile(GetUI());
    mTargetIndex = crunchy::editor::kNormalPhaseIndex;
    GetUI()->Resize(PLUG_WIDTH, PLUG_HEIGHT,
                    profile.scales[crunchy::editor::kNormalPhaseIndex]);
    SetDirty(false);
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override {
    if (!mMouseOver)
      mPreviousCursor = GetUI()->SetMouseCursor(ECursor::SIZENWSE);
    mMouseOver = true;
    IControl::OnMouseOver(x, y, mod);
  }

  void OnMouseOut() override {
    if (mMouseOver)
      GetUI()->SetMouseCursor(mPreviousCursor);
    mMouseOver = false;
    IControl::OnMouseOut();
  }

private:
  float mStartX = 0.0f;
  float mStartY = 0.0f;
  int mStartIndex = crunchy::editor::kNormalPhaseIndex;
  int mTargetIndex = crunchy::editor::kNormalPhaseIndex;
  bool mDragging = false;
  bool mMouseOver = false;
  ECursor mPreviousCursor = ECursor::ARROW;
  crunchy::editor::ScaleProfile mDragProfile {};
};

} // namespace

// The five graphic-EQ faders intentionally have a shaped startup contour,
// but a double-click must always return the touched band to electrical 0 dB.
// Keeping that policy in the control avoids coupling the gesture to the
// startup preset/default contour.
class IZeroResetEqSlider final : public IVSliderControl {
public:
  IZeroResetEqSlider(const IRECT& bounds, int paramIdx, const char* label,
                     const IVStyle& style, bool valueIsEditable = true)
  : IVSliderControl(bounds, paramIdx, label, style, valueIsEditable) {}

  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override {
    (void) x; (void) y; (void) mod;
    const IParam* p = GetParam();
    if (!p) return;
    SetValueFromUserInput(p->ToNormalized(controls::GraphicEqDoubleClickResetDb));
  }
};

// Compact horizontal dBFS sample-peak meter used beside the Input/Output trims.
//
// 0.10.15 removes the final host-dependent handoff from the meter path. The
// IGraphics control itself polls the realtime-safe PeakMeterState at every
// display refresh through IControl's animation callback. iPlug2 calls Animate()
// on every control each display tick, and a control with an AnimationFunc is
// always considered dirty, so this works identically in APP and normal VST3
// without OnIdle(), SendControlMsgFromDelegate(), ISender, or tag lookup.
//
// The audio thread still examines every sample. The GUI only consumes the
// already-accumulated maximum, so repaint rate cannot make a clip disappear.
class IClipPeakMeter final : public IControl {
public:
  IClipPeakMeter(const IRECT& bounds, PeakMeterState* state,
                 IColor track, IColor fill, IColor clip)
  : IControl(bounds), mState(state), mTrack(track), mFill(fill), mClip(clip) {
    SetIgnoreMouse(true);

    // A duration-less animation function is intentionally permanent. iPlug2
    // invokes it once per display refresh and IsDirty() stays true while it is
    // installed. This gives the meter its own UI clock rather than depending on
    // API/host idle callbacks.
    SetAnimation([this](IControl*) {
      PollRealtimeState();
    });
  }

  void Draw(IGraphics& g) override {
    const float cubeW = std::min(8.f, mRECT.W() * 0.08f);
    const IRECT bar = IRECT(mRECT.L, mRECT.T, mRECT.R - cubeW - 2.f, mRECT.B);
    const IRECT clipCube = IRECT(mRECT.R - cubeW, mRECT.T, mRECT.R, mRECT.B);
    g.FillRect(mTrack, bar);
    g.FillRect(mTrack, clipCube);

    const double safePeak = std::max(1.0e-12, static_cast<double>(mDisplayedPeak));
    const double db = 20.0 * std::log10(safePeak);
    const double norm = std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
    if (norm > 0.0) {
      IRECT fill = bar;
      fill.R = fill.L + static_cast<float>(norm) * fill.W();
      g.FillRect(mFill, fill);
    }

    if (mClipHoldFrames > 0)
      g.FillRect(mClip, clipCube);
  }

  void ClearMeasurement() {
    mDisplayedPeak = 0.f;
    mClipHoldFrames = 0;
    mNoDataFrames = 0;
    if (mState) mState->Reset();
    SetDirty(false);
  }

private:
  void PollRealtimeState() {
    if (!mState) return;

    const auto snapshot = mState->Consume();
    if (snapshot.hasData) {
      mDisplayedPeak = (std::isfinite(snapshot.peak) && snapshot.peak > 0.f)
                         ? snapshot.peak : 0.f;
      mNoDataFrames = 0;
      if (snapshot.clipped)
        mClipHoldFrames = 36; // ~600 ms at PLUG_FPS=60.
    }
    else {
      // When the host stops calling ProcessBlock(), do not leave the last peak
      // frozen forever. Six GUI frames is long enough not to flicker even with
      // very large audio buffers, but still clears quickly when transport stops.
      if (++mNoDataFrames >= 6) {
        mDisplayedPeak = 0.f;
        mNoDataFrames = 6;
      }
    }

    if (!snapshot.clipped && mClipHoldFrames > 0)
      --mClipHoldFrames;
  }

  PeakMeterState* mState = nullptr;
  IColor mTrack, mFill, mClip;
  float mDisplayedPeak = 0.f;
  int mClipHoldFrames = 0;
  int mNoDataFrames = 0;
};

#if defined(APP_API)
class StandaloneMonoInputControl final : public IControl {
public:
  StandaloneMonoInputControl(const IRECT& bounds, DerTondehrCrunchy& plugin)
  : IControl(bounds), mPlugin(plugin) {
    SetTooltip("Standalone input source inside the stereo hardware pair selected in Preferences");
  }

  void Draw(IGraphics& g) override {
    const IRECT b = mRECT.GetPadded(-0.5f);
    const IRECT label = b.GetFromTop(13.f);
    const IRECT buttons = b.GetReducedFromTop(14.f);
    const IColor panel(255, 41, 44, 41);
    const IColor border(210, 160, 166, 152);
    const IColor ink(255, 238, 234, 218);
    const IColor muted(255, 160, 166, 152);
    const IColor accent(255, 73, 92, 49);

    g.DrawText(IText(10.0f, muted, "Arial", EAlign::Center), "MONO IN", label);
    g.FillRoundRect(panel, buttons, 2.5f);
    g.DrawRoundRect(border, buttons, 2.5f, nullptr, 0.8f);

    const int selected = mPlugin.StandaloneMonoInput();
    const float mid = buttons.MW();
    for (int i = 0; i < 2; ++i) {
      const IRECT r(i == 0 ? buttons.L : mid, buttons.T,
                    i == 0 ? mid : buttons.R, buttons.B);
      if (selected == i)
        g.FillRoundRect(accent, r.GetPadded(-2.f), 1.7f);
      if (i == 1)
        g.DrawLine(IColor(150, 100, 106, 96), mid, buttons.T + 3.f, mid, buttons.B - 3.f, nullptr, 0.7f);
      g.DrawText(IText(11.5f, selected == i ? ink : muted, "Arial", EAlign::Center),
                 i == 0 ? "1" : "2", r);
    }
  }

  void OnMouseDown(float x, float, const IMouseMod&) override {
    mPlugin.SetStandaloneMonoInput(x < mRECT.MW() ? 0 : 1);
    SetDirty(false);
  }

private:
  DerTondehrCrunchy& mPlugin;
};
#endif

// Keeps the panel faithful to the hardware channel routing. The four controls
// below are electrically disconnected in RHYTHM, so the UI dims and disables
// them without overwriting their stored values. Switching back to LEAD restores
// the controls exactly where the user left them. Polling the parameter on the
// IGraphics display tick also follows host automation without API-specific code.
class IChannelModeUIState final : public IControl {
public:
  explicit IChannelModeUIState(const IRECT& bounds, const IParam* leadParam)
  : IControl(bounds), mLeadParam(leadParam) {
    SetIgnoreMouse(true);
    SetAnimation([this](IControl*) { Sync(false); });
  }

  void Draw(IGraphics&) override {}

  void OnAttached() override {
    Sync(true);
  }

private:
  void Sync(bool force) {
    if (!mLeadParam || !GetUI()) return;
    const bool lead = mLeadParam->Value() >= 0.5;
    if (!force && lead == mLeadMode) return;
    mLeadMode = lead;

    constexpr int kLeadOnlyTags[] = {
      kCtrlTagTrebleShift, kCtrlTagLeadDrive,
      kCtrlTagLeadMaster, kCtrlTagLeadBright
    };
    for (const int tag : kLeadOnlyTags) {
      if (auto* control = GetUI()->GetControlWithTag(tag))
        control->SetDisabled(!lead);
    }
  }

  const IParam* mLeadParam = nullptr;
  bool mLeadMode = false;
};
#endif

DerTondehrCrunchy::DerTondehrCrunchy(const InstanceInfo& info)
: Plugin(info, MakeConfig(NumParams, 1)) {
#if defined(APP_API)
  constexpr bool kStandaloneBuild = true;
#else
  constexpr bool kStandaloneBuild = false;
#endif
  const auto defaults = StartupDefaults(kStandaloneBuild);

  const char* knobs[] = {"Volume 1", "Treble", "Bass", "Middle", "Master 1", "Lead Drive", "Lead Master"};
  for (int i = Volume; i <= LeadMaster; ++i)
    GetParam(i)->InitDouble(knobs[i], defaults[i], 0, 10, 0.01);
  const char* bands[] = {"EQ 60 Hz", "EQ 240 Hz", "EQ 750 Hz", "EQ 2200 Hz", "EQ 6600 Hz"};
  for (int i = 0; i < 5; ++i)
    GetParam(Eq80 + i)->InitDouble(bands[i], defaults[Eq80 + i], -12, 12, 0.1, "dB");
  GetParam(InputCalibration)->InitDouble("Input", defaults[InputCalibration], -24, 24, 0.1, "dB");
  GetParam(EqGain)->InitDouble("EQ Gain", defaults[EqGain], -12, 12, 0.1, "dB");
  GetParam(OutputGain)->InitDouble("Output", defaults[OutputGain], -24, 24, 0.1, "dB");
  const char* switches[] = {"Rhythm Bright", "Treble Shift", "Bass Shift", "Lead",
                           "Lead Bright", "Deep", "EQ Auto", "EQ In", "Bypass"};
  for (int i = RhythmBright; i <= Bypass; ++i)
    GetParam(i)->InitBool(switches[i - RhythmBright], defaults[i] >= 0.5);
  GetParam(Presence)->InitDouble("Presence", defaults[Presence], 0, 10, 0.01);
  GetParam(PowerMode)->InitEnum("Power", static_cast<int>(defaults[PowerMode]), 2, "", 0, "", "CLASS A 25 W", "SIMUL-CLASS 75 W");
  GetParam(Reverb)->InitDouble("Reverb", defaults[Reverb], 0, 10, 0.01);
  GetParam(ReverbOn)->InitBool("Reverb Footswitch", defaults[ReverbOn] >= 0.5);
  GetParam(LaterSimul20p)->InitBool("Later Simul 20pF", defaults[LaterSimul20p] >= 0.5);
  GetParam(ExportBias)->InitBool("Export Bias", defaults[ExportBias] >= 0.5);
  // 0.10.3 replaces the two visible EQ booleans with one physical-style
  // three-position switch. The old EqAuto/EqIn parameter IDs remain allocated
  // for host-state compatibility but are no longer exposed on the panel.
  GetParam(EqMode)->InitEnum("Graphic EQ Mode", static_cast<int>(defaults[EqMode]), 3, "", 0, "",
                             "EQ AUTO / LEAD", "EQ OUT", "EQ IN / ALL");
  GetParam(Oversampling)->InitEnum("Oversampling", static_cast<int>(defaults[Oversampling]), 4, "", 0, "",
                                   "1x", "2x", "4x", "8x");
  MakeDefaultPreset("Crunchy - Starting Point");
#if defined(APP_API)
  LoadStandaloneAudioPreferences();
#endif

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    // Before a native HWND exists, use the primary monitor to choose the same
    // adaptive NORMAL phase. On a screen with enough room
    // this is exactly Crunchy's existing 1180x520 / 1.0x editor.
    const auto profile = CrunchyBuildEditorScaleProfile(nullptr);
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS,
                        profile.scales[crunchy::editor::kNormalPhaseIndex]);
  };
  mLayoutFunc = [&](IGraphics* g) {
    const IColor background(255, 26, 28, 27), panel(255, 41, 44, 41);
    const IColor ink(255, 238, 234, 218), muted(255, 160, 166, 152), accent(255, 203, 190, 117);
    g->AttachPanelBackground(background);
    g->EnableMouseOver(true);
    g->EnableTooltips(true);
    // Keep Crunchy's logical design canvas fixed at 1180x520. Resizing changes
    // only draw scale, so every control preserves its current geometry/aspect.
    g->SetLayoutOnResize(false);
    g->SetScaleConstraints(crunchy::editor::kAbsoluteMinimumDrawScale,
                           crunchy::editor::kNativeSafeMaximum);
    // Keep APP/VST3 repaint behavior aligned at scaled edges.
    g->SetStrictDrawing(true);

    // IMPORTANT: IText stores a font ID, not an operating-system font name.
    // Register Arial explicitly as a *system font* so the VST3 does not depend
    // on an embedded .ttf resource. Without this LoadFont() call NanoVG draws
    // the vector controls but all labels/value readouts are blank in REAPER.
    // The 3-argument overload asks the platform for the installed Arial face.
    g->LoadFont("Arial", "Arial", ETextStyle::Normal);

    g->AttachTextEntryControl();
    g->AttachPopupMenuControl();
    g->AttachControl(new ITextControl(IRECT(28, 16, 830, 56), "DER TONDEHR  /  CRUNCHY",
      IText(30, ink, "Arial", EAlign::Near)));
    g->AttachControl(new ITextControl(IRECT(30, 57, 990, 80), "GUITAR AMPLIFIER    /    6L6 SIMUL-CLASS + CLASS A    /    PASS 9 0.10.25",
      IText(13, muted, "Arial", EAlign::Near)));
    auto style = DEFAULT_STYLE.WithColor(kBG, panel).WithColor(kFG, IColor(255, 67, 71, 63))
      .WithColor(kPR, accent).WithColor(kFR, muted).WithColor(kHL, ink)
      .WithLabelText(IText(12, ink, "Arial"))
      .WithValueText(IText(13, accent, "Arial", EAlign::Center, EVAlign::Bottom))
      .WithRoundness(0.15f).WithDrawShadows(false);
    const auto switchStyle = style.WithValueText(IText(13, ink, "Arial"))
      .WithColor(kPR, IColor(255, 73, 92, 49)).WithColor(kHL, IColor(255, 89, 107, 65));
    const char* labels[] = {"VOLUME 1", "TREBLE", "BASS", "MIDDLE", "MASTER 1", "LEAD DRIVE", "LEAD MASTER"};
    for (int i = 0; i < 7; ++i) {
      const float x = 22.f + i * 104.f;
      int tag = kNoTag;
      if (i == LeadDrive) tag = kCtrlTagLeadDrive;
      else if (i == LeadMaster) tag = kCtrlTagLeadMaster;
      g->AttachControl(new IVKnobControl(IRECT(x, 104, x + 94, 249), i, labels[i], style, true), tag);
    }
    const char* bandLabels[] = {"60", "240", "750", "2200", "6600"};
    for (int i = 0; i < 5; ++i) {
      const float x = 764.f + i * 47.f;
      g->AttachControl(new IZeroResetEqSlider(IRECT(x, 106, x + 43, 252), Eq80 + i, bandLabels[i], style, true));
    }
    g->AttachControl(new ITextControl(IRECT(760, 84, 1000, 102), "GRAPHIC EQ  /  Hz", IText(12, muted, "Arial")));
    const int trims[] = {InputCalibration, EqGain, OutputGain};
    const char* trimLabels[] = {"INPUT / dB", "EQ GAIN / dB", "OUTPUT / dB"};
    for (int i = 0; i < 3; ++i) {
      const float y = 102.f + i * 67.f;
      g->AttachControl(new IVSliderControl(IRECT(1024, y, 1156, y + 62), trims[i], trimLabels[i], style, true, EDirection::Horizontal));
    }
    // Thin meters run parallel to the Input/Output slider tracks, inside the
    // same visual blocks. Their right-most square is the clip indicator.
    // A sample reaching +/-1.0 (0 dBFS) lights it red, using the same digital
    // clipping threshold as a conventional DAW sample-peak meter. The Input
    // meter taps the host samples AFTER the user-visible INPUT trim but BEFORE
    // the hidden +12 dB amplifier working calibration. Therefore INPUT 0.0 dB
    // equals the raw host peak, +/-N dB moves the meter by +/-N dB, and only the
    // user-visible digital trim can make this meter reach 0 dBFS.
    const IColor meterTrack(255, 18, 20, 18);
    const IColor meterFill(255, 203, 190, 117);
    const IColor meterClip(255, 226, 54, 46);
    g->AttachControl(new IClipPeakMeter(IRECT(1032, 145, 1148, 151), &mInputMeterState, meterTrack, meterFill, meterClip), kCtrlTagInputMeter);
    g->AttachControl(new IClipPeakMeter(IRECT(1032, 279, 1148, 285), &mOutputMeterState, meterTrack, meterFill, meterClip), kCtrlTagOutputMeter);
    // Four explicit quality modes. 1x is the startup/default mode; higher
    // settings are opt-in because nonlinear oversampling scales CPU almost
    // proportionally with the factor.
    g->AttachControl(new IVTabSwitchControl(IRECT(1024, 303, 1156, 360), Oversampling,
      {"1x", "2x", "4x", "8x"}, "OVERSAMPLING", switchStyle,
      EVShape::Rectangle, EDirection::Horizontal));
    const int toggles[] = {RhythmBright, TrebleShift, BassShift, Lead, LeadBright, Deep};
    const char* toggleLabels[] = {"VOLUME 1 BRIGHT", "TREBLE SHIFT", "BASS SHIFT", "CHANNEL", "LEAD BRIGHT", "DEEP"};
    for (int i = 0; i < 6; ++i) {
      const float x = 26.f + i * 143.f;
      auto* toggle = new IVToggleControl(IRECT(x, 289, x + 128, 347), toggles[i], toggleLabels[i], switchStyle,
        i == 3 ? "RHYTHM" : "OFF", i == 3 ? "LEAD" : "ON");
      if (toggles[i] == TrebleShift)
        toggle->SetTooltip("Pass-9 750pF / 10M Treble Shift via LDR1; electrically available only in LEAD.");
      else if (toggles[i] == BassShift)
        toggle->SetTooltip("Pass-9 V1A cathode Bass Shift: .47uF fixed + 15uF / 15K switched branch.");
      else if (toggles[i] == LeadBright)
        toggle->SetTooltip("Pass-9 V4A .22uF / 22K Lead Bright branch; audible only in the LEAD signal path.");
      else if (toggles[i] == Deep)
        toggle->SetTooltip("Pass-9 post-loop V2 Deep cathode branch, located before MASTER 1.");

      int tag = kNoTag;
      if (toggles[i] == TrebleShift) tag = kCtrlTagTrebleShift;
      else if (toggles[i] == LeadBright) tag = kCtrlTagLeadBright;
      g->AttachControl(toggle, tag);
    }

    // Compact three-position Graphic EQ selector.  Use a tab switch instead of
    // a separate slide-switch body plus floating labels: the complete clickable
    // control now occupies the same compact column seen in the intended layout.
    // top=AUTO/LEAD, centre=OUT, bottom=IN/ALL.
    const auto eqModeStyle = switchStyle
      .WithLabelText(IText(10.5f, muted, "Arial", EAlign::Center, EVAlign::Middle))
      .WithValueText(IText(10.0f, ink, "Arial", EAlign::Near, EVAlign::Middle))
      .WithRoundness(0.0f)
      .WithDrawShadows(false);
    g->AttachControl(new ITextControl(IRECT(888, 258, 970, 275), "GRAPHIC EQ",
      IText(10.5f, muted, "Arial", EAlign::Near, EVAlign::Middle)));
    g->AttachControl(new IVTabSwitchControl(IRECT(888, 277, 970, 350), EqMode,
      {"EQ AUTO / LEAD", "EQ OUT", "EQ IN / ALL"}, "", eqModeStyle,
      EVShape::Rectangle, EDirection::Vertical));
#if defined(APP_API)
    // Standalone-only input-source selector.
    // Preferences selects a contiguous hardware pair (1/2, 3/4, ...); this
    // selector chooses which member of that pair feeds the mono guitar amp.
    g->AttachControl(new StandaloneMonoInputControl(IRECT(900, 24, 990, 76), *this),
                     kCtrlTagStandaloneMonoInput);
#endif
    g->AttachControl(new IVToggleControl(IRECT(1024, 23, 1156, 77), Bypass, "BYPASS", switchStyle));
    g->AttachControl(new IVSliderControl(IRECT(30, 369, 250, 426), Presence, "PRESENCE", style, true, EDirection::Horizontal));
    g->AttachControl(new IVSlideSwitchControl(IRECT(270, 369, 490, 426), PowerMode, "POWER MODE", switchStyle));
    g->AttachControl(new IVKnobControl(IRECT(510, 352, 605, 497), Reverb, "REVERB", style, true));
    g->AttachControl(new IVToggleControl(IRECT(625, 369, 750, 426), ReverbOn, "REVERB F/S", switchStyle, "OFF", "ON"));
    g->AttachControl(new IVToggleControl(IRECT(770, 369, 920, 426), LaterSimul20p, "SIMUL HF CAP", switchStyle, "10pF", "20pF"));
    g->AttachControl(new IVToggleControl(IRECT(940, 369, 1080, 426), ExportBias, "BIAS MODE", switchStyle, "-67V", "-55V"));
    g->AttachControl(new ITextControl(IRECT(625, 447, 1152, 489),
      "POWER: CLASS A 25 W / SIMUL-CLASS 75 W    |    POST-DISTORTION SPRING REVERB    |    CAB: EXTERNAL IR",
      IText(11, muted, "Arial", EAlign::Near)));

    // RHYTHM removes LDR2/LDR3/LDR4 from the signal path and the LDR1 Treble
    // Shift LED shares that Lead return bus. Dim those four controls only; all
    // pre-split and post-return controls remain active in both channels.
    g->AttachControl(new IChannelModeUIState(IRECT(0, 0, 1, 1), GetParam(Lead)));

    // Six-state resize handle interaction:
    // drag selects a phase, resize happens once on mouse-up, and double-click
    // returns to NORMAL. The compact handle lives in the otherwise empty corner.
    g->AttachControl(new CrunchySteppedEditorResizeControl(g->GetBounds()));

    // Use the actual parameter names as hover tooltips as an additional UI
    // contract: every automatable control is identifiable even at small scales.
    g->AssignParamNameToolTips();
  };
#endif
}

#if IPLUG_EDITOR
bool DerTondehrCrunchy::OnHostRequestingSupportedViewConfiguration(int width, int height) {
  if ((width + height) == 0)
    return true;

  const auto profile = CrunchyBuildEditorScaleProfile(GetUI());
  for (const float scale : profile.scales) {
    const int supportedWidth = static_cast<int>(std::lround(static_cast<double>(PLUG_WIDTH) * scale));
    const int supportedHeight = static_cast<int>(std::lround(static_cast<double>(PLUG_HEIGHT) * scale));
    if (std::abs(width - supportedWidth) <= 1 && std::abs(height - supportedHeight) <= 1)
      return true;
  }
  return false;
}

bool DerTondehrCrunchy::ConstrainEditorResize(int& width, int& height) const {
#if defined(APP_API)
  // Standalone owns its Windows top-level window. IGraphics::Resize() passes the
  // fixed logical 1180x520 canvas through here before drawScale is applied, so
  // do not snap these logical dimensions to a host rectangle. Note that the
  // compile-time PLUG_MAX_* values are deliberately generous since 0.10.19: iPlug2
  // also feeds them to the APP top-level WM_GETMINMAXINFO path, where they refer
  // to the OUTER window (including menu/title/frame and DPI), not this logical
  // IGraphics canvas. The six-phase profile below remains the real size ceiling.
  const bool alreadyValid =
    width >= PLUG_MIN_WIDTH && width <= PLUG_MAX_WIDTH &&
    height >= PLUG_MIN_HEIGHT && height <= PLUG_MAX_HEIGHT;
  width = std::clamp(width, PLUG_MIN_WIDTH, PLUG_MAX_WIDTH);
  height = std::clamp(height, PLUG_MIN_HEIGHT, PLUG_MAX_HEIGHT);
  return alreadyValid;
#else
  // VST3: the DAW owns the editor rectangle. Snap its border request to the
  // nearest legal screen-aware phase before REAPER/another host applies it.
  auto* graphics = const_cast<DerTondehrCrunchy*>(this)->GetUI();
  return CrunchyConstrainHostResizeToPhase(graphics, width, height);
#endif
}

void DerTondehrCrunchy::OnParentWindowResize(int width, int height) {
  auto* graphics = GetUI();
  if (!graphics || width <= 0 || height <= 0)
    return;

  const float platformScale = std::max(0.01f, graphics->GetPlatformWindowScale());
  const float logicalWidth = static_cast<float>(width) / platformScale;
  const float logicalHeight = static_cast<float>(height) / platformScale;
  const auto profile = CrunchyBuildEditorScaleProfile(graphics);

#if defined(APP_API)
  // Standalone is different from a hosted editor: Windows lets the user drag a
  // single outer border, so one axis may change while the other stays fixed.
  // Following min(widthScale,heightScale), as VST3 does, caused right/bottom
  // expansion to keep (or occasionally choose a smaller) draw scale. The parent
  // stayed larger than the child and the uncovered client area appeared white.
  const int targetIndex = crunchy::editor::SelectStandaloneResizePhase(
    logicalWidth, logicalHeight,
    static_cast<float>(PLUG_WIDTH), static_cast<float>(PLUG_HEIGHT),
    graphics->GetDrawScale(), profile);
  const float targetScale = profile.scales[static_cast<std::size_t>(targetIndex)];

#if defined(_WIN32)
  const int targetClientWidth = std::max(1, static_cast<int>(std::lround(
    static_cast<double>(PLUG_WIDTH) * targetScale * platformScale)));
  const int targetClientHeight = std::max(1, static_cast<int>(std::lround(
    static_cast<double>(PLUG_HEIGHT) * targetScale * platformScale)));

  // Resize the APP-owned top-level client rectangle even when targetScale did
  // not change. That detail is what removes the stale white right/bottom strip
  // after a one-axis border drag. Then tell IGraphics the parent is already at
  // the accepted size, so only its render child is synchronized.
  const bool parentMatched = CrunchyResizeStandaloneClientExactly(
    graphics, targetClientWidth, targetClientHeight);
  graphics->Resize(PLUG_WIDTH, PLUG_HEIGHT, targetScale, !parentMatched);
#else
  graphics->Resize(PLUG_WIDTH, PLUG_HEIGHT, targetScale, true);
#endif

#else
  // VST3/DAW path stays exactly as in 0.10.17. The host already owns and has
  // constrained the outer rectangle, so use the smaller axis only to fit the
  // fixed 1180x520 canvas and never request another host resize here.
  const float requestedScale = std::min(logicalWidth / static_cast<float>(PLUG_WIDTH),
                                        logicalHeight / static_cast<float>(PLUG_HEIGHT));
  const int targetIndex = crunchy::editor::NearestScaleIndex(requestedScale, profile);
  const float targetScale = profile.scales[static_cast<std::size_t>(targetIndex)];
  graphics->Resize(PLUG_WIDTH, PLUG_HEIGHT, targetScale, false);
#endif
}

void DerTondehrCrunchy::OnUIOpen() {
  SendCurrentParamValuesFromDelegate();

  // Now that the real window exists, recompute against the monitor containing
  // it. NORMAL is native 1180x520 whenever that screen/DPI can safely fit it;
  // smaller displays automatically receive the compressed NORMAL phase.
  if (auto* graphics = GetUI()) {
    const auto profile = CrunchyBuildEditorScaleProfile(graphics);
    const float normalScale = profile.scales[crunchy::editor::kNormalPhaseIndex];
    if (std::abs(graphics->GetDrawScale() - normalScale) > (0.5f / crunchy::editor::kScaleGrid))
      graphics->Resize(PLUG_WIDTH, PLUG_HEIGHT, normalScale);
  }

#if IPLUG_DSP
  mInputMeterState.Reset();
  mOutputMeterState.Reset();
#endif
}
#endif

#if IPLUG_DSP
Parameters DerTondehrCrunchy::ReadParameters() {
  Parameters values;
  for (int i = 0; i < NumParams; ++i) values[i] = GetParam(i)->Value();
  return values;
}

#if defined(APP_API)
namespace {
std::filesystem::path CrunchyStandaloneAudioPrefsPath() {
  namespace fs = std::filesystem;
  const char* base = std::getenv("LOCALAPPDATA");
  if (!base || !*base)
    base = std::getenv("APPDATA");

  std::error_code ec;
  fs::path root = (base && *base) ? fs::u8path(base) : fs::temp_directory_path(ec);
  if (root.empty()) root = fs::path(".");
  fs::path directory = root / "Der Tondehr" / "DerTondehrCrunchy";
  fs::create_directories(directory, ec);
  return directory / "standalone_audio.txt";
}
}

void DerTondehrCrunchy::LoadStandaloneAudioPreferences() {
  int selected = 0;
  std::ifstream file(CrunchyStandaloneAudioPrefsPath(), std::ios::binary);
  if (file) {
    std::string line;
    std::getline(file, line);
    try { selected = std::clamp(std::stoi(line), 0, 1); }
    catch (...) { selected = 0; }
  }
  mStandaloneMonoInputTarget.store(selected, std::memory_order_relaxed);
  mStandaloneMonoInputBlend = static_cast<double>(selected);
}

void DerTondehrCrunchy::SaveStandaloneAudioPreferences() const {
  const int selected = StandaloneMonoInput();
  const std::filesystem::path path = CrunchyStandaloneAudioPrefsPath();
  const std::filesystem::path temp = path.string() + ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) return;
    file << selected << "\n";
    file.flush();
    if (!file) return;
  }
  std::error_code ec;
  std::filesystem::remove(path, ec);
  ec.clear();
  std::filesystem::rename(temp, path, ec);
  if (ec)
    std::filesystem::remove(temp, ec);
}

void DerTondehrCrunchy::SetStandaloneMonoInput(int inputIndex) {
  mStandaloneMonoInputTarget.store(std::clamp(inputIndex, 0, 1), std::memory_order_release);
  SaveStandaloneAudioPreferences();
}

int DerTondehrCrunchy::StandaloneMonoInput() const {
  return std::clamp(mStandaloneMonoInputTarget.load(std::memory_order_acquire), 0, 1);
}
#endif

void DerTondehrCrunchy::OnReset() {
#if defined(APP_API)
  mStandaloneMonoInputBlend = static_cast<double>(StandaloneMonoInput());
#endif
  mAmp.Prepare(GetSampleRate(), ReadParameters());
  mInputMeterState.Reset();
  mOutputMeterState.Reset();
}

void DerTondehrCrunchy::ProcessBlock(sample** inputs, sample** outputs, int nFrames) {
  const Parameters params = ReadParameters();
  mAmp.SetParameters(params);

  const int nInputs = std::clamp(NInChansConnected(), 0, 2);
  const int nOutputs = std::clamp(NOutChansConnected(), 0, 2);
  // INPUT meter semantics:
  //   selected standalone source (or host inputs in VST3) -> visible INPUT trim
  //   -> meter / clip detector -> hidden +12 dB amplifier calibration.
  // The hidden analog working offset never consumes digital meter headroom.
#if defined(APP_API)
  // Standalone input model: the driver opens one contiguous
  // stereo hardware pair and MONO IN chooses one member of that pair. Crossfade
  // the source over ~8 ms, then feed the same mono sample to both amp channels.
  // APP_SIGNAL_VECTOR_SIZE is the iPlugAPP internal ProcessBlock quantum; process
  // larger calls in chunks so the audio thread never allocates memory.
  constexpr int kMonoScratchFrames = APP_SIGNAL_VECTOR_SIZE > 0 ? APP_SIGNAL_VECTOR_SIZE : 64;
  std::array<sample, kMonoScratchFrames> monoScratch {};
  double rawPeak = 0.0;
  bool rawClipped = false;
  const double sampleRate = std::max(8000.0, GetSampleRate());
  const double monoInputSlew = 1.0 - std::exp(-1.0 / (0.008 * sampleRate));
  const double monoTarget = static_cast<double>(StandaloneMonoInput());

  int offset = 0;
  while (offset < nFrames) {
    const int count = std::min(kMonoScratchFrames, nFrames - offset);
    for (int s = 0; s < count; ++s) {
      const int frame = offset + s;
      const double in0 = (nInputs > 0 && inputs[0] && std::isfinite(inputs[0][frame]))
                           ? static_cast<double>(inputs[0][frame]) : 0.0;
      const double in1 = (nInputs > 1 && inputs[1] && std::isfinite(inputs[1][frame]))
                           ? static_cast<double>(inputs[1][frame]) : in0;
      mStandaloneMonoInputBlend += (monoTarget - mStandaloneMonoInputBlend) * monoInputSlew;
      const double mono = in0 + (in1 - in0) * std::clamp(mStandaloneMonoInputBlend, 0.0, 1.0);
      monoScratch[static_cast<std::size_t>(s)] = static_cast<sample>(mono);
      const double magnitude = std::abs(mono);
      rawPeak = std::max(rawPeak, magnitude);
      rawClipped = rawClipped || magnitude >= 1.0;
    }

    sample* monoInputs[2] = {monoScratch.data(), monoScratch.data()};
    sample* chunkOutputs[2] = {
      nOutputs > 0 && outputs[0] ? outputs[0] + offset : nullptr,
      nOutputs > 1 && outputs[1] ? outputs[1] + offset : nullptr
    };
    mAmp.Process(monoInputs, chunkOutputs, count, nInputs > 0 ? 2 : 0, nOutputs);
    offset += count;
  }
  SamplePeakMeasurement inputMeter;
  inputMeter.peak = static_cast<float>(rawPeak);
  inputMeter.clipped = rawClipped;
#else
  auto inputMeter = MeasureSamplePeak(inputs, nFrames, nInputs);
  mAmp.Process(inputs, outputs, nFrames, nInputs, nOutputs);
#endif
  inputMeter = ApplySamplePeakGain(inputMeter, Db(params[InputCalibration]));

  // Output is measured after every Crunchy stage and after the Output trim:
  // these are the exact samples returned to the host/DAW.
  const auto outputMeter = MeasureSamplePeak(outputs, nFrames, nOutputs);

  // One atomic mailbox per meter accumulates the maximum until the actual
  // IGraphics control consumes it on a display refresh. There is no OnIdle or
  // message transport in this path.
  mInputMeterState.Accumulate(inputMeter.peak, inputMeter.clipped);
  mOutputMeterState.Accumulate(outputMeter.peak, outputMeter.clipped);
}
#endif
