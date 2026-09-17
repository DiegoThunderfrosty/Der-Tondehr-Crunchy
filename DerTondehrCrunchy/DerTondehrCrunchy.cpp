#include "DerTondehrCrunchy.h"
#include "IPlug_include_in_plug_src.h"
#include "IControls.h"
#include "ui/EditorScale.h"
#include "preset/CrunchyPresetFormat.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace crunchy;

namespace {

std::string CrunchyLowerASCII(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

std::filesystem::path CrunchyPathFromUTF8(const std::string& text) {
  return std::filesystem::u8path(text);
}

std::string CrunchyPathToUTF8(const std::filesystem::path& path) {
#if __cplusplus >= 202002L
  const auto value = path.u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
  return path.u8string();
#endif
}

std::filesystem::path CrunchyUserConfigDirectory() {
  namespace fs = std::filesystem;
  fs::path root;
#ifdef _WIN32
  if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA"))
    if (*local) root = fs::path(local);
  if (root.empty())
    if (const wchar_t* roaming = _wgetenv(L"APPDATA"))
      if (*roaming) root = fs::path(roaming);
  if (root.empty())
    if (const wchar_t* profile = _wgetenv(L"USERPROFILE"))
      if (*profile) root = fs::path(profile);
#else
  if (const char* home = std::getenv("HOME"))
    if (*home) root = fs::u8path(home) / ".config";
#endif
  if (root.empty()) {
    std::error_code ec;
    root = fs::current_path(ec);
    if (ec) root = fs::path(".");
  }
  const fs::path directory = root / "Der Tondehr" / "DerTondehrCrunchy";
  std::error_code ec;
  fs::create_directories(directory, ec);
  return directory;
}

bool CrunchyPathEntryIsLinkOrReparse(const std::filesystem::path& path) {
  namespace fs = std::filesystem;
#if defined(_WIN32)
  const DWORD attributes = ::GetFileAttributesW(path.wstring().c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) return false;
  return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(path, ec);
  return !ec && fs::is_symlink(status);
#endif
}

bool CrunchyPathHasAnyLinkOrReparse(const std::filesystem::path& inputPath) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path path = fs::absolute(inputPath, ec);
  if (ec) { ec.clear(); path = inputPath; }
  path = path.lexically_normal();
  fs::path walk;
  for (const auto& component : path) {
    walk /= component;
    if (CrunchyPathEntryIsLinkOrReparse(walk)) return true;
  }
  return false;
}

std::filesystem::path CrunchyCanonicalForBoundary(const std::filesystem::path& path) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path resolved = fs::weakly_canonical(path, ec);
  if (ec) {
    ec.clear();
    resolved = fs::absolute(path, ec);
    if (ec) resolved = path;
  }
  return resolved.lexically_normal();
}

std::string CrunchyComparablePathComponent(const std::filesystem::path& component) {
  std::string value = CrunchyPathToUTF8(component);
#ifdef _WIN32
  value = CrunchyLowerASCII(std::move(value));
#endif
  return value;
}

bool CrunchyLogicalPathIsSameOrBelow(const std::filesystem::path& candidatePath,
                                     const std::filesystem::path& rootPath) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path candidate = fs::absolute(candidatePath, ec);
  if (ec) { ec.clear(); candidate = candidatePath; }
  fs::path root = fs::absolute(rootPath, ec);
  if (ec) root = rootPath;
  candidate = candidate.lexically_normal();
  root = root.lexically_normal();
  auto ci = candidate.begin();
  for (auto ri = root.begin(); ri != root.end(); ++ri, ++ci) {
    if (ci == candidate.end()) return false;
    if (CrunchyComparablePathComponent(*ci) != CrunchyComparablePathComponent(*ri)) return false;
  }
  return true;
}

bool CrunchyPathHasLinkOrReparseBelowRoot(const std::filesystem::path& candidatePath,
                                          const std::filesystem::path& rootPath) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path root = fs::absolute(rootPath, ec);
  if (ec) { ec.clear(); root = rootPath; }
  fs::path candidate = fs::absolute(candidatePath, ec);
  if (ec) { ec.clear(); candidate = candidatePath; }
  root = root.lexically_normal();
  candidate = candidate.lexically_normal();
  if (CrunchyPathEntryIsLinkOrReparse(root)) return true;
  auto ci = candidate.begin();
  for (auto ri = root.begin(); ri != root.end(); ++ri, ++ci) {
    if (ci == candidate.end()
        || CrunchyComparablePathComponent(*ri) != CrunchyComparablePathComponent(*ci))
      return true;
  }
  fs::path walk = root;
  for (; ci != candidate.end(); ++ci) {
    walk /= *ci;
    if (CrunchyPathEntryIsLinkOrReparse(walk)) return true;
    ec.clear();
    if (!fs::exists(walk, ec) || ec) break;
  }
  return false;
}

bool CrunchyPathIsSameOrBelow(const std::filesystem::path& candidatePath,
                              const std::filesystem::path& rootPath) {
  const auto candidate = CrunchyCanonicalForBoundary(candidatePath);
  const auto root = CrunchyCanonicalForBoundary(rootPath);
  auto ci = candidate.begin();
  for (auto ri = root.begin(); ri != root.end(); ++ri, ++ci) {
    if (ci == candidate.end()) return false;
    if (CrunchyComparablePathComponent(*ci) != CrunchyComparablePathComponent(*ri)) return false;
  }
  return true;
}

bool CrunchyStablePathIsSameOrBelow(const std::filesystem::path& candidatePath,
                                    const std::filesystem::path& rootPath) {
  if (!CrunchyLogicalPathIsSameOrBelow(candidatePath, rootPath)) return false;
  if (CrunchyPathHasAnyLinkOrReparse(rootPath)
      || CrunchyPathHasLinkOrReparseBelowRoot(candidatePath, rootPath)) return false;
  return CrunchyPathIsSameOrBelow(candidatePath, rootPath);
}

bool CrunchyPathsReferToSameLocation(const std::filesystem::path& a,
                                     const std::filesystem::path& b) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const bool equivalent = fs::equivalent(a, b, ec);
  if (!ec) return equivalent;
  const auto ca = CrunchyCanonicalForBoundary(a);
  const auto cb = CrunchyCanonicalForBoundary(b);
  auto ai = ca.begin(), bi = cb.begin();
  for (; ai != ca.end() && bi != cb.end(); ++ai, ++bi)
    if (CrunchyComparablePathComponent(*ai) != CrunchyComparablePathComponent(*bi)) return false;
  return ai == ca.end() && bi == cb.end();
}

bool CrunchyDirectoryWritable(const std::filesystem::path& directory) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::exists(directory, ec) || ec || !fs::is_directory(directory, ec) || ec) return false;
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path probe = directory / (".crunchy_write_test_" + std::to_string(stamp) + ".tmp");
  {
    std::ofstream out(probe, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.put('\0');
    out.flush();
    if (!out) { out.close(); fs::remove(probe, ec); return false; }
  }
  fs::remove(probe, ec);
  return !ec;
}

bool CrunchyWriteBinaryAtomically(const std::filesystem::path& destination,
                                  const std::vector<std::uint8_t>& bytes) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path parent = destination.parent_path();
  if (!parent.empty() && (!fs::exists(parent, ec) || ec || !fs::is_directory(parent, ec) || ec))
    return false;
  ec.clear();
  const bool destinationExists = fs::exists(destination, ec) && !ec;
  if (destinationExists) {
    ec.clear();
    if (!fs::is_regular_file(destination, ec) || ec || CrunchyPathEntryIsLinkOrReparse(destination))
      return false;
  }
  const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  fs::path temp = destination;
  temp += ".tmp." + std::to_string(stamp) + "." + std::to_string(reinterpret_cast<std::uintptr_t>(&bytes));
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) { out.close(); fs::remove(temp, ec); return false; }
  }
#if defined(_WIN32)
  HANDLE tempHandle = ::CreateFileW(temp.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
    FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (tempHandle == INVALID_HANDLE_VALUE || !::FlushFileBuffers(tempHandle)) {
    if (tempHandle != INVALID_HANDLE_VALUE) ::CloseHandle(tempHandle);
    fs::remove(temp, ec); return false;
  }
  ::CloseHandle(tempHandle);
  BOOL ok = destinationExists
    ? ::ReplaceFileW(destination.wstring().c_str(), temp.wstring().c_str(), nullptr,
                     REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)
    : ::MoveFileExW(temp.wstring().c_str(), destination.wstring().c_str(), MOVEFILE_WRITE_THROUGH);
  if (!ok) { fs::remove(temp, ec); return false; }
#else
  fs::rename(temp, destination, ec);
  if (ec) { fs::remove(temp, ec); return false; }
#endif
  return true;
}

struct CrunchyPresetParamSpec {
  std::uint32_t stableId;
  int paramIdx;
  crunchy::preset::ValueType type;
  double minValue;
  double maxValue;
};

// Stable preset IDs are deliberately independent from the iPlug2 parameter enum.
// EqAuto/EqIn are legacy host-state slots and are intentionally excluded: the
// visible EqMode control is the canonical preset representation.
static constexpr std::array<CrunchyPresetParamSpec, 30> kCrunchyPresetParams {{
  {0x1001u, Volume,           crunchy::preset::ValueType::Continuous, 0.0, 10.0},
  {0x1002u, Treble,           crunchy::preset::ValueType::Continuous, 0.0, 10.0},
  {0x1003u, Bass,             crunchy::preset::ValueType::Continuous, 0.0, 10.0},
  {0x1004u, Middle,           crunchy::preset::ValueType::Continuous, 0.0, 10.0},
  {0x1005u, Master,           crunchy::preset::ValueType::Continuous, 0.0, 10.0},
  {0x1006u, LeadDrive,        crunchy::preset::ValueType::Continuous, 0.0, 10.0},
  {0x1007u, LeadMaster,       crunchy::preset::ValueType::Continuous, 0.0, 10.0},
  {0x1010u, Eq80,             crunchy::preset::ValueType::Continuous,-12.0, 12.0},
  {0x1011u, Eq240,            crunchy::preset::ValueType::Continuous,-12.0, 12.0},
  {0x1012u, Eq750,            crunchy::preset::ValueType::Continuous,-12.0, 12.0},
  {0x1013u, Eq2200,           crunchy::preset::ValueType::Continuous,-12.0, 12.0},
  {0x1014u, Eq6600,           crunchy::preset::ValueType::Continuous,-12.0, 12.0},
  {0x1020u, InputCalibration, crunchy::preset::ValueType::Continuous,-24.0, 24.0},
  {0x1021u, EqGain,           crunchy::preset::ValueType::Continuous,-12.0, 12.0},
  {0x1022u, OutputGain,       crunchy::preset::ValueType::Continuous,-24.0, 24.0},
  {0x1030u, RhythmBright,     crunchy::preset::ValueType::Toggle,      0.0,  1.0},
  {0x1031u, TrebleShift,      crunchy::preset::ValueType::Toggle,      0.0,  1.0},
  {0x1032u, BassShift,        crunchy::preset::ValueType::Toggle,      0.0,  1.0},
  {0x1033u, Lead,             crunchy::preset::ValueType::Toggle,      0.0,  1.0},
  {0x1034u, LeadBright,       crunchy::preset::ValueType::Toggle,      0.0,  1.0},
  {0x1035u, Deep,             crunchy::preset::ValueType::Toggle,      0.0,  1.0},
  {0x1038u, Bypass,           crunchy::preset::ValueType::Toggle,      0.0,  1.0},
  {0x1040u, Presence,         crunchy::preset::ValueType::Continuous, 0.0, 10.0},
  {0x1041u, PowerMode,        crunchy::preset::ValueType::Enumeration,0.0,  1.0},
  {0x1042u, Reverb,           crunchy::preset::ValueType::Continuous, 0.0, 10.0},
  {0x1043u, ReverbOn,         crunchy::preset::ValueType::Toggle,      0.0,  1.0},
  {0x1044u, LaterSimul20p,    crunchy::preset::ValueType::Toggle,      0.0,  1.0},
  {0x1045u, ExportBias,       crunchy::preset::ValueType::Toggle,      0.0,  1.0},
  {0x1050u, EqMode,           crunchy::preset::ValueType::Enumeration,0.0,  2.0},
  {0x1051u, Oversampling,     crunchy::preset::ValueType::Enumeration,0.0,  3.0}
}};
static_assert(kCrunchyPresetParams.size() == static_cast<std::size_t>(NumParams - 2),
              "Crunchy .dtcpreset map must contain every user-facing parameter except legacy EqAuto/EqIn");

const CrunchyPresetParamSpec* CrunchyPresetSpec(std::uint32_t stableId) {
  for (const auto& spec : kCrunchyPresetParams)
    if (spec.stableId == stableId) return &spec;
  return nullptr;
}

bool CrunchyPresetValueValid(const CrunchyPresetParamSpec& spec, double value) {
  if (!std::isfinite(value) || value < spec.minValue - 1e-9 || value > spec.maxValue + 1e-9)
    return false;
  if (spec.type == crunchy::preset::ValueType::Toggle)
    return std::abs(value - 0.0) < 1e-9 || std::abs(value - 1.0) < 1e-9;
  if (spec.type == crunchy::preset::ValueType::Enumeration)
    return std::abs(value - std::round(value)) < 1e-9;
  return true;
}

bool CrunchyIsPresetPath(const std::string& path) {
  std::string ext = CrunchyPathToUTF8(CrunchyPathFromUTF8(path).extension());
  ext = CrunchyLowerASCII(std::move(ext));
  return ext == crunchy::preset::kExtension;
}

} // namespace

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


// Pure-vector "hardware chassis" treatment.  No bitmap assets are required:
// the depth is built from stacked dark shadows, warm top highlights, inset
// seams and a few recessed bays.  This keeps Crunchy fully scalable in iPlug2
// while making the flat front panel read much more like a physical amplifier.
class Crunchy3DChassisControl final : public IControl {
public:
  explicit Crunchy3DChassisControl(const IRECT& bounds) : IControl(bounds) {
    SetIgnoreMouse(true);
  }

  void Draw(IGraphics& g) override {
    const IColor shadowHard(150, 0, 0, 0);
    const IColor shadowSoft(72, 0, 0, 0);
    const IColor shadowDeep(115, 4, 5, 4);
    const IColor edgeDark(255, 13, 15, 13);
    const IColor edgeMid(255, 53, 57, 51);
    const IColor edgeLight(210, 111, 112, 91);
    const IColor warmLight(145, 203, 190, 117);
    const IColor panelTop(255, 48, 51, 47);
    const IColor panelBottom(255, 34, 37, 34);
    const IColor recess(255, 23, 25, 23);
    const IColor recessInner(255, 31, 34, 31);

    // Outer chassis: several offset contours fake a soft cast shadow and a
    // rounded metal lip without relying on gradients or image resources.
    IRECT outer = mRECT.GetPadded(-7.f);
    g.FillRoundRect(shadowSoft, outer.GetTranslated(0.f, 5.f), 12.f);
    g.FillRoundRect(shadowHard, outer.GetTranslated(0.f, 2.5f), 12.f);
    g.FillRoundRect(edgeDark, outer, 12.f);
    IRECT shell = outer.GetPadded(-1.2f);
    g.FillRoundRect(panelBottom, shell, 10.8f);
    g.DrawRoundRect(edgeMid, shell, 10.8f, nullptr, 1.1f);
    g.DrawLine(warmLight, shell.L + 11.f, shell.T + 1.2f,
               shell.R - 11.f, shell.T + 1.2f, nullptr, 1.0f);
    g.DrawLine(shadowDeep, shell.L + 11.f, shell.B - 1.1f,
               shell.R - 11.f, shell.B - 1.1f, nullptr, 1.2f);

    // Raised section frames stay deliberately hollow.  The earlier 3D pass
    // filled these bays with a second panel colour, which created broad dark
    // rectangles underneath/behind some controls.  Keeping only the bevel and
    // cast shadow preserves the hardware depth without visually masking the
    // controls that iPlug2 draws on top.
    DrawRaisedBay(g, IRECT(18, 11, 1163, 87), 7.f, panelTop, panelBottom,
                  edgeLight, edgeDark, warmLight, shadowHard);

    // Main preamp / EQ shelf.
    DrawRaisedBay(g, IRECT(16, 91, 1007, 263), 8.f, panelTop, panelBottom,
                  edgeLight, edgeDark, warmLight, shadowHard);

    // I/O + quality column is deliberately deeper/recessed to visually separate
    // digital utility controls from the analogue-style amp faceplate.
    DrawRecessedBay(g, IRECT(1014, 91, 1164, 365), 7.f, recess, recessInner,
                    edgeDark, edgeMid, warmLight);

    // Switching strip and output section.
    DrawRaisedBay(g, IRECT(17, 278, 1003, 356), 7.f, panelTop, panelBottom,
                  edgeLight, edgeDark, warmLight, shadowHard);
    DrawRaisedBay(g, IRECT(17, 359, 1163, 499), 9.f, panelTop, panelBottom,
                  edgeLight, edgeDark, warmLight, shadowHard);

    // The Graphic EQ now belongs visually to the same main preamp shelf as
    // the other front-panel controls, so do not draw a separate sub-frame here.

    // Keep hardware fasteners only at the four chassis corners.  The previous
    // internal screws read as stray dots beside controls and section labels.
    const std::array<std::pair<float, float>, 4> screws {{
      {16.f, 16.f}, {1164.f, 16.f}, {16.f, 504.f}, {1164.f, 504.f}
    }};
    for (const auto& screw : screws)
      DrawScrew(g, screw.first, screw.second, edgeLight, edgeDark, warmLight);
  }

private:
  static void DrawRaisedBay(IGraphics& g, const IRECT& r, float radius,
                            IColor /*top*/, IColor /*bottom*/, IColor edgeLight,
                            IColor edgeDark, IColor highlight, IColor shadow) {
    // Frame-only treatment: no interior fill.  This avoids the large rectangular
    // overlays that were competing with the controls while retaining a clear
    // raised edge, top specular highlight and bottom contact shadow.
    g.DrawRoundRect(shadow, r.GetTranslated(0.f, 3.f), radius, nullptr, 2.6f);
    g.DrawRoundRect(edgeDark, r, radius, nullptr, 2.2f);
    const IRECT face = r.GetPadded(-1.15f);
    g.DrawRoundRect(edgeLight, face, std::max(0.f, radius - 1.f), nullptr, 0.75f);
    g.DrawLine(highlight, face.L + radius, face.T + 1.f,
               face.R - radius, face.T + 1.f, nullptr, 0.8f);
    g.DrawLine(IColor(120, 0, 0, 0), face.L + radius, face.B - 1.f,
               face.R - radius, face.B - 1.f, nullptr, 1.0f);
  }

  static void DrawRecessedBay(IGraphics& g, const IRECT& r, float radius,
                              IColor outerDark, IColor inner, IColor edgeDark,
                              IColor edgeMid, IColor highlight) {
    g.FillRoundRect(edgeDark, r, radius);
    IRECT trench = r.GetPadded(-1.4f);
    g.FillRoundRect(outerDark, trench, std::max(0.f, radius - 1.f));
    g.DrawLine(IColor(185, 0, 0, 0), trench.L + radius, trench.T + 1.f,
               trench.R - radius, trench.T + 1.f, nullptr, 1.2f);
    IRECT well = trench.GetPadded(-2.2f);
    g.FillRoundRect(inner, well, std::max(0.f, radius - 2.f));
    g.DrawRoundRect(edgeMid, well, std::max(0.f, radius - 2.f), nullptr, 0.7f);
    g.DrawLine(highlight, well.L + radius, well.B - 1.f,
               well.R - radius, well.B - 1.f, nullptr, 0.55f);
  }

  static void DrawScrew(IGraphics& g, float x, float y, IColor metal,
                        IColor dark, IColor highlight) {
    g.FillCircle(IColor(115, 0, 0, 0), x + 1.2f, y + 1.4f, 3.2f);
    g.FillCircle(dark, x, y, 3.0f);
    g.FillCircle(metal, x - 0.35f, y - 0.4f, 2.15f);
    g.FillCircle(highlight, x - 0.9f, y - 1.0f, 0.65f);
    g.DrawLine(IColor(210, 27, 29, 26), x - 1.25f, y + 0.95f,
               x + 1.25f, y - 0.95f, nullptr, 0.65f);
  }
};


class CrunchyKnob3DOverlayControl final : public IControl {
public:
  explicit CrunchyKnob3DOverlayControl(const IRECT& bounds) : IControl(bounds) {
    SetIgnoreMouse(true);
  }

  void Draw(IGraphics& g) override {
    // Overlay subtle bevel, gloss and contact shadow directly on top of the
    // knob faces. This keeps the rest of the UI unchanged while giving the
    // rotary controls a noticeably more physical/3D appearance.
    for (int i = 0; i < 7; ++i) {
      const float x = 22.f + i * 104.f;
      const IRECT r(x, 104.f, x + 94.f, 249.f);
      DrawKnobOverlay(g, r, i >= 5 ? 0.28f : 0.0f);
    }

    // Reverb knob
    DrawKnobOverlay(g, IRECT(510.f, 352.f, 605.f, 497.f), 0.0f);
  }

private:
  static void DrawKnobOverlay(IGraphics& g, const IRECT& r, float darkBias) {
    const float cx = static_cast<float>(r.MW());
    const float cy = static_cast<float>(r.MH()) - 2.0f;
    const float radius = std::min(static_cast<float>(r.W()), static_cast<float>(r.H())) * 0.395f;

    // Soft contact shadow around the base.
    g.FillCircle(IColor(45, 0, 0, 0), cx, cy + radius * 0.18f, radius + 3.0f);

    // Stronger metal-like bevel rings.
    g.DrawCircle(IColor(155, 242, 238, 220), cx, cy, radius + 0.7f, nullptr, 1.1f);
    g.DrawCircle(IColor(85, 0, 0, 0), cx, cy, radius - 0.2f, nullptr, 1.4f);
    g.DrawCircle(IColor(85, 255, 252, 240), cx - 0.2f, cy - 0.35f, radius - 2.2f, nullptr, 0.7f);

    // Top-left gloss and bottom-right shade inside the knob face.
    g.FillCircle(IColor(static_cast<int>(38 - darkBias * 15), 255, 255, 255),
                 cx - radius * 0.24f, cy - radius * 0.28f, radius * 0.62f);
    g.FillCircle(IColor(static_cast<int>(26 + darkBias * 20), 0, 0, 0),
                 cx + radius * 0.18f, cy + radius * 0.25f, radius * 0.74f);

    // Very small specular line near the lit top edge.
    g.DrawLine(IColor(88, 250, 247, 232),
               cx - radius * 0.42f, cy - radius * 0.52f,
               cx + radius * 0.18f, cy - radius * 0.52f,
               nullptr, 0.9f);
  }
};

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
    g.FillRoundRect(IColor(110, 0, 0, 0), buttons.GetTranslated(0.f, 2.f), 3.2f);
    g.FillRoundRect(IColor(255, 18, 20, 18), buttons, 3.2f);
    g.FillRoundRect(panel, buttons.GetPadded(-1.2f), 2.3f);
    g.DrawRoundRect(border, buttons.GetPadded(-0.6f), 2.7f, nullptr, 0.8f);
    g.DrawLine(IColor(135, 203, 190, 117), buttons.L + 4.f, buttons.T + 1.f,
               buttons.R - 4.f, buttons.T + 1.f, nullptr, 0.65f);

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


class CrunchyPresetManagerControl final : public IControl {
public:
  CrunchyPresetManagerControl(const IRECT& bounds, DerTondehrCrunchy& plugin)
  : IControl(bounds), mPlugin(plugin) {
    mPlugin.mPresetBrowserOpen = false;
    mPlugin.ClearPresetDeleteIdentity();
    SetAnimation([this](IControl*) { mPlugin.PollPresetDirectoryChanges(); });
  }

  bool IsHit(float x, float y) const override {
    if (mOpen) return mRECT.Contains(x, y);
    return HeaderBar().Contains(x, y);
  }

  void Draw(IGraphics& g) override {
    if (mDeleteArmed && !mPlugin.mPresetDeleteIdentityValid) mDeleteArmed = false;
    const unsigned int revision = mPlugin.mPresetUIRevision.load(std::memory_order_relaxed);
    if (revision != mLastRevision) {
      mLastRevision = revision;
      RefreshEntries();
    }
    DrawHeader(g);
    if (!mOpen) return;

    const IRECT panel = PanelRect();
    g.FillRect(IColor(190, 4, 5, 4), mRECT);
    g.FillRoundRect(IColor(255, 24, 27, 24), panel, 7.f);
    g.DrawRoundRect(IColor(255, 105, 108, 91), panel, 7.f, nullptr, 1.f);
    g.DrawLine(IColor(150, 203, 190, 117), panel.L + 8.f, panel.T + 1.f,
               panel.R - 8.f, panel.T + 1.f, nullptr, 0.8f);

    g.DrawText(IText(15.f, IColor(255, 238, 234, 218), "Arial", EAlign::Near),
               "DER TONDEHR CRUNCHY PRESETS", IRECT(panel.L + 12.f, panel.T + 8.f, panel.R - 50.f, panel.T + 35.f));
    DrawButton(g, CloseRect(panel), "X", false, false);

    const IRECT crumb = CrumbRect(panel);
    g.FillRoundRect(IColor(255, 18, 20, 18), crumb, 3.f);
    std::string crumbText = CurrentCrumb();
    if (mClipped) crumbText += "  |  Limited to 4096 entries";
    if (mListingIncomplete) crumbText += "  |  Listing incomplete";
    g.DrawText(IText(10.f, IColor(220, 160, 166, 152), "Arial", EAlign::Near),
               FitLabel(crumbText, crumb.W() - 12.f, 10.f).c_str(), crumb.GetPadded(-6.f));

    const IRECT list = ListRect(panel);
    g.FillRoundRect(IColor(255, 15, 17, 15), list, 3.f);
    g.DrawRoundRect(IColor(180, 67, 71, 63), list, 3.f, nullptr, 0.8f);
    constexpr float rowH = 27.f;
    const int visibleRows = std::max(1, static_cast<int>(list.H() / rowH));
    mScroll = std::clamp(mScroll, 0, std::max(0, static_cast<int>(mEntries.size()) - visibleRows));
    for (int row = 0; row < visibleRows; ++row) {
      const int idx = mScroll + row;
      if (idx >= static_cast<int>(mEntries.size())) break;
      const Entry& entry = mEntries[static_cast<std::size_t>(idx)];
      const IRECT rr(list.L, list.T + row * rowH, list.R, list.T + (row + 1) * rowH);
      const bool selected = !entry.isDirectory && !mPlugin.mPresetSelectedPath.empty()
        && CrunchyPathsReferToSameLocation(CrunchyPathFromUTF8(entry.path), CrunchyPathFromUTF8(mPlugin.mPresetSelectedPath));
      if (selected) g.FillRect(IColor(255, 54, 58, 50), rr);
      const std::string prefix = entry.isDirectory ? ">  " : "   ";
      g.DrawText(IText(11.f, entry.isDirectory ? IColor(240, 203, 190, 117) : IColor(235, 220, 218, 205),
                       "Arial", EAlign::Near),
                 FitLabel(prefix + entry.name, rr.W() - 18.f, 11.f).c_str(), rr.GetPadded(-7.f));
      g.DrawLine(IColor(65, 95, 98, 88), rr.L + 4.f, rr.B, rr.R - 4.f, rr.B, nullptr, 0.6f);
    }

    if (static_cast<int>(mEntries.size()) > visibleRows) {
      const IRECT track = ScrollTrackRect(list);
      g.FillRoundRect(IColor(255, 30, 33, 29), track, 2.f);
      g.FillRoundRect(IColor(255, 105, 108, 91), ScrollThumbRect(list, visibleRows), 2.f);
    }

    if (mEntries.empty()) {
      const char* message = mPlugin.PresetRootAvailable()
        ? "No .dtcpreset files in this folder" : "Preset directory is unavailable";
      g.DrawText(IText(12.f, IColor(180, 160, 166, 152), "Arial", EAlign::Center), message, list);
    }

    std::string status = mDeleteArmed
      ? "Delete selected preset? Click DELETE again to confirm."
      : mPlugin.mPresetStatusMessage;
    if (status.empty()) status = "Single-click selects. Double-click recalls. Folders can be opened.";
    const bool error = mDeleteArmed || mPlugin.mPresetStatusIsError;
    g.DrawText(IText(10.5f, error ? IColor(245, 238, 128, 90) : IColor(230, 203, 190, 117),
                     "Arial", EAlign::Center),
               FitLabel(status, StatusRect(panel).W() - 10.f, 10.5f).c_str(), StatusRect(panel));
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override {
    const IRECT header = HeaderBar();
    if (header.Contains(x, y)) {
      if (PlusRect(header).Contains(x, y)) {
        mDeleteArmed = false; mPlugin.ClearPresetDeleteIdentity();
        if (mPlugin.mPresetRootDirectory.empty()) mPlugin.PromptSetPresetDirectory();
        return;
      }
      if (MinusRect(header).Contains(x, y)) {
        mDeleteArmed = false; mPlugin.ClearPresetDeleteIdentity();
        if (!mPlugin.mPresetRootDirectory.empty()) {
          mOpen = false; mPlugin.mPresetBrowserOpen = false; mPlugin.RemovePresetDirectory();
        }
        return;
      }
      if (SaveRect(header).Contains(x, y)) {
        mDeleteArmed = false; mPlugin.ClearPresetDeleteIdentity();
        if (mPlugin.PresetRootAvailable()) mPlugin.PromptSavePreset();
        return;
      }
      if (DeleteRect(header).Contains(x, y)) {
        if (!mPlugin.PresetRootAvailable() || mPlugin.mPresetSelectedPath.empty()) return;
        if (mDeleteArmed) {
          mDeleteArmed = false; mPlugin.DeleteSelectedPresetFile(); RefreshEntries();
        } else if (mPlugin.ArmSelectedPresetDelete()) {
          mDeleteArmed = true; SetDirty(false);
        }
        return;
      }
      if (FieldRect(header).Contains(x, y) && mPlugin.PresetRootAvailable()) {
        mDeleteArmed = false; mPlugin.ClearPresetDeleteIdentity();
        mOpen = !mOpen; mPlugin.mPresetBrowserOpen = mOpen;
        mPlugin.mPresetDirectoryFingerprintValid = false;
        if (mOpen) RefreshEntries();
        SetDirty(false);
      }
      return;
    }

    if (!mOpen) return;
    const IRECT panel = PanelRect();
    if (CloseRect(panel).Contains(x, y) || !panel.Contains(x, y)) {
      mOpen = false; mPlugin.mPresetBrowserOpen = false; mDeleteArmed = false;
      mPlugin.ClearPresetDeleteIdentity(); SetDirty(false); return;
    }
    const IRECT list = ListRect(panel);
    const int visibleRows = std::max(1, static_cast<int>(list.H() / 27.f));
    if (static_cast<int>(mEntries.size()) > visibleRows && ScrollTrackRect(list).Contains(x, y)) {
      mDraggingScrollbar = true; SetScrollFromY(y, list, visibleRows); SetDirty(false); return;
    }
    const int idx = EntryAt(y, panel);
    if (idx < 0 || idx >= static_cast<int>(mEntries.size())) return;
    const Entry entry = mEntries[static_cast<std::size_t>(idx)];
    mDeleteArmed = false; mPlugin.ClearPresetDeleteIdentity();
    if (entry.isDirectory) {
      if (mPlugin.PresetPathIsInsideRoot(entry.path)) {
        mPlugin.mPresetCurrentDirectory = entry.path;
        mPlugin.mPresetSelectedPath.clear();
        mPlugin.mPresetStatusMessage.clear();
        mPlugin.mPresetStatusIsError = false;
        mPlugin.mPresetDirectoryFingerprintValid = false;
        mScroll = 0; RefreshEntries(); SetDirty(false);
      }
      return;
    }
    mPlugin.mPresetSelectedPath = entry.path;
    mPlugin.mPresetStatusMessage = "Selected: " + entry.name + "  |  Double-click to recall";
    mPlugin.mPresetStatusIsError = false;
    RefreshEntries(); SetDirty(false);
  }

  void OnMouseDblClick(float x, float y, const IMouseMod&) override {
    if (!mOpen) return;
    const IRECT panel = PanelRect();
    if (!panel.Contains(x, y) || !ListRect(panel).Contains(x, y)) return;
    const int idx = EntryAt(y, panel);
    if (idx < 0 || idx >= static_cast<int>(mEntries.size())) return;
    const Entry entry = mEntries[static_cast<std::size_t>(idx)];
    if (entry.isDirectory || mPlugin.mPresetSelectedPath.empty()) return;
    if (!CrunchyPathsReferToSameLocation(CrunchyPathFromUTF8(entry.path), CrunchyPathFromUTF8(mPlugin.mPresetSelectedPath)))
      return;
    mDeleteArmed = false;
    if (mPlugin.LoadPresetFile(entry.path)) {
      mOpen = false; mPlugin.mPresetBrowserOpen = false;
    }
    RefreshEntries(); SetDirty(false);
  }

  void OnMouseDrag(float, float y, float, float, const IMouseMod&) override {
    if (!mOpen || !mDraggingScrollbar) return;
    const IRECT list = ListRect(PanelRect());
    SetScrollFromY(y, list, std::max(1, static_cast<int>(list.H() / 27.f)));
    SetDirty(false);
  }

  void OnMouseUp(float, float, const IMouseMod&) override { mDraggingScrollbar = false; }

  void OnMouseWheel(float, float, const IMouseMod&, float d) override {
    if (!mOpen) return;
    mScroll = std::max(0, mScroll + (d > 0.f ? -3 : 3));
    SetDirty(false);
  }

private:
  struct Entry { std::string path; std::string name; bool isDirectory = false; };

  IRECT HeaderBar() const { return IRECT(500.f, 24.f, 895.f, 53.f); }
  static IRECT FieldRect(const IRECT& b)  { return IRECT(b.L + 2.f, b.T + 2.f, b.L + 180.f, b.B - 2.f); }
  static IRECT PlusRect(const IRECT& b)   { return IRECT(b.L + 184.f, b.T + 2.f, b.L + 214.f, b.B - 2.f); }
  static IRECT MinusRect(const IRECT& b)  { return IRECT(b.L + 218.f, b.T + 2.f, b.L + 248.f, b.B - 2.f); }
  static IRECT SaveRect(const IRECT& b)   { return IRECT(b.L + 252.f, b.T + 2.f, b.L + 316.f, b.B - 2.f); }
  static IRECT DeleteRect(const IRECT& b) { return IRECT(b.L + 320.f, b.T + 2.f, b.R - 2.f, b.B - 2.f); }
  IRECT PanelRect() const { return IRECT(110.f, 94.f, 1070.f, 486.f); }
  static IRECT CloseRect(const IRECT& p) { return IRECT(p.R - 39.f, p.T + 8.f, p.R - 10.f, p.T + 35.f); }
  static IRECT CrumbRect(const IRECT& p) { return IRECT(p.L + 12.f, p.T + 43.f, p.R - 12.f, p.T + 69.f); }
  static IRECT StatusRect(const IRECT& p) { return IRECT(p.L + 12.f, p.B - 34.f, p.R - 12.f, p.B - 8.f); }
  static IRECT ListRect(const IRECT& p) { return IRECT(p.L + 12.f, p.T + 77.f, p.R - 12.f, p.B - 42.f); }
  static IRECT ScrollTrackRect(const IRECT& list) { return IRECT(list.R - 10.f, list.T + 3.f, list.R - 3.f, list.B - 3.f); }

  IRECT ScrollThumbRect(const IRECT& list, int visibleRows) const {
    const IRECT track = ScrollTrackRect(list);
    const int total = static_cast<int>(mEntries.size());
    if (total <= visibleRows) return track;
    const float h = std::max(28.f, track.H() * static_cast<float>(visibleRows) / static_cast<float>(total));
    const int maxScroll = std::max(1, total - visibleRows);
    const float t = static_cast<float>(std::clamp(mScroll, 0, maxScroll)) / static_cast<float>(maxScroll);
    const float top = track.T + (track.H() - h) * t;
    return IRECT(track.L, top, track.R, top + h);
  }

  void SetScrollFromY(float y, const IRECT& list, int visibleRows) {
    const int total = static_cast<int>(mEntries.size());
    const int maxScroll = std::max(0, total - visibleRows);
    if (maxScroll <= 0) { mScroll = 0; return; }
    const IRECT track = ScrollTrackRect(list);
    const float h = std::max(28.f, track.H() * static_cast<float>(visibleRows) / static_cast<float>(total));
    const float travel = std::max(1.f, track.H() - h);
    const float t = std::clamp((y - track.T - h * 0.5f) / travel, 0.f, 1.f);
    mScroll = std::clamp(static_cast<int>(std::lround(t * static_cast<float>(maxScroll))), 0, maxScroll);
  }

  static std::string FitLabel(const std::string& text, float widthPx, float fontPx) {
    const int maxChars = std::max(1, static_cast<int>(std::floor(std::max(1.f, widthPx) / std::max(4.2f, fontPx * 0.56f))));
    if (static_cast<int>(text.size()) <= maxChars) return text;
    if (maxChars <= 3) return std::string(static_cast<std::size_t>(maxChars), '.');
    return text.substr(0, static_cast<std::size_t>(maxChars - 3)) + "...";
  }

  static void DrawButton(IGraphics& g, const IRECT& r, const char* text, bool armed, bool disabled) {
    const IColor fill = disabled ? IColor(255, 27, 29, 26) : armed ? IColor(255, 105, 66, 44) : IColor(255, 49, 52, 47);
    const IColor frame = disabled ? IColor(255, 58, 60, 54) : armed ? IColor(255, 203, 190, 117) : IColor(220, 111, 112, 91);
    const IColor ink = disabled ? IColor(125, 160, 166, 152) : IColor(245, 238, 234, 218);
    g.FillRoundRect(fill, r, 2.5f);
    g.DrawRoundRect(frame, r, 2.5f, nullptr, 0.8f);
    g.DrawText(IText(9.3f, ink, "Arial", EAlign::Center), text, r);
  }

  void DrawHeader(IGraphics& g) {
    const IRECT bar = HeaderBar();
    const bool hasRoot = !mPlugin.mPresetRootDirectory.empty();
    const bool available = mPlugin.PresetRootAvailable();
    const bool selected = available && !mPlugin.mPresetSelectedPath.empty();
    g.FillRoundRect(IColor(255, 20, 22, 20), bar, 4.f);
    g.DrawRoundRect(IColor(220, 90, 94, 82), bar, 4.f, nullptr, 0.8f);
    std::string label;
    IColor color(230, 190, 190, 175);
    if (!hasRoot) label = "PRESETS: choose folder (+)";
    else if (!available) { label = "Preset folder unavailable"; color = IColor(240, 230, 108, 108); }
    else if (!mPlugin.mPresetStatusMessage.empty()) {
      label = mPlugin.mPresetStatusMessage;
      color = mPlugin.mPresetStatusIsError ? IColor(240, 230, 108, 108) : IColor(235, 203, 190, 117);
    } else if (selected) label = CrunchyPathToUTF8(CrunchyPathFromUTF8(mPlugin.mPresetSelectedPath).stem());
    else label = "Select preset...";
    const IRECT field = FieldRect(bar);
    g.FillRoundRect(IColor(255, 13, 15, 13), field, 2.5f);
    g.DrawRoundRect(IColor(115, 76, 80, 70), field, 2.5f, nullptr, 0.7f);
    g.DrawText(IText(9.5f, color, "Arial", EAlign::Near),
               FitLabel(label, field.W() - 10.f, 9.5f).c_str(), field.GetPadded(-5.f));
    DrawButton(g, PlusRect(bar), "+", false, hasRoot);
    DrawButton(g, MinusRect(bar), "-", false, !hasRoot);
    DrawButton(g, SaveRect(bar), "SAVE", false, !available);
    DrawButton(g, DeleteRect(bar), mDeleteArmed ? "OK" : "DEL", mDeleteArmed, !selected);
  }

  std::string CurrentCrumb() const {
    namespace fs = std::filesystem;
    if (!mPlugin.PresetRootAvailable()) return "";
    const fs::path root = CrunchyPathFromUTF8(mPlugin.mPresetRootDirectory);
    const fs::path current = CrunchyPathFromUTF8(mPlugin.mPresetCurrentDirectory.empty()
      ? mPlugin.mPresetRootDirectory : mPlugin.mPresetCurrentDirectory);
    std::error_code ec;
    const fs::path relative = fs::relative(current, root, ec);
    const std::string rootName = CrunchyPathToUTF8(root.filename());
    if (ec || relative.empty() || relative == ".") return rootName;
    return rootName + " / " + CrunchyPathToUTF8(relative);
  }

  int EntryAt(float y, const IRECT& panel) const {
    const IRECT list = ListRect(panel);
    if (y < list.T || y > list.B) return -1;
    return mScroll + static_cast<int>((y - list.T) / 27.f);
  }

  void RefreshEntries() {
    namespace fs = std::filesystem;
    mEntries.clear(); mClipped = false; mListingIncomplete = false;
    if (!mPlugin.PresetRootAvailable()) { mScroll = 0; return; }
    const fs::path root = CrunchyPathFromUTF8(mPlugin.mPresetRootDirectory);
    fs::path current = mPlugin.mPresetCurrentDirectory.empty() ? root : CrunchyPathFromUTF8(mPlugin.mPresetCurrentDirectory);
    std::error_code ec;
    if (!CrunchyStablePathIsSameOrBelow(current, root)
        || !fs::exists(current, ec) || ec || !fs::is_directory(current, ec) || ec) current = root;
    mPlugin.mPresetCurrentDirectory = CrunchyPathToUTF8(current);
    if (!CrunchyPathsReferToSameLocation(current, root)) {
      fs::path parent = current.parent_path();
      if (!CrunchyStablePathIsSameOrBelow(parent, root)) parent = root;
      mEntries.push_back({CrunchyPathToUTF8(parent), "..", true});
    }
    std::vector<Entry> dirs, presets;
    int relevant = 0;
    ec.clear();
    for (fs::directory_iterator it(current, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
      const fs::directory_entry& de = *it;
      std::error_code te;
      Entry entry;
      bool keep = false;
      if (de.is_directory(te) && !te) {
        if (!CrunchyPathEntryIsLinkOrReparse(de.path()) && CrunchyStablePathIsSameOrBelow(de.path(), root)) {
          entry = {CrunchyPathToUTF8(de.path()), CrunchyPathToUTF8(de.path().filename()), true}; keep = true;
        }
      } else {
        te.clear();
        const std::string pathText = CrunchyPathToUTF8(de.path());
        if (de.is_regular_file(te) && !te && CrunchyIsPresetPath(pathText)
            && !CrunchyPathEntryIsLinkOrReparse(de.path()) && CrunchyStablePathIsSameOrBelow(de.path(), root)) {
          entry = {pathText, CrunchyPathToUTF8(de.path().stem()), false}; keep = true;
        }
      }
      if (!keep) continue;
      if (relevant++ >= DerTondehrCrunchy::kPresetMaxEntriesPerFolder) { mClipped = true; break; }
      (entry.isDirectory ? dirs : presets).push_back(std::move(entry));
    }
    if (ec) {
      mListingIncomplete = true;
      mPlugin.mPresetStatusMessage = "Folder listing incomplete";
      mPlugin.mPresetStatusIsError = true;
    }
    const auto byName = [](const Entry& a, const Entry& b) { return CrunchyLowerASCII(a.name) < CrunchyLowerASCII(b.name); };
    std::sort(dirs.begin(), dirs.end(), byName); std::sort(presets.begin(), presets.end(), byName);
    mEntries.insert(mEntries.end(), dirs.begin(), dirs.end());
    mEntries.insert(mEntries.end(), presets.begin(), presets.end());
    mScroll = std::clamp(mScroll, 0, std::max(0, static_cast<int>(mEntries.size()) - 1));
  }

  DerTondehrCrunchy& mPlugin;
  std::vector<Entry> mEntries;
  unsigned int mLastRevision = std::numeric_limits<unsigned int>::max();
  int mScroll = 0;
  bool mOpen = false;
  bool mDeleteArmed = false;
  bool mClipped = false;
  bool mListingIncomplete = false;
  bool mDraggingScrollbar = false;
};

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
  LoadPresetDirectoryPreference();
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
    g->AttachControl(new Crunchy3DChassisControl(g->GetBounds()));
    g->AttachControl(new ITextControl(IRECT(28, 16, 830, 56), "DER TONDEHR  /  CRUNCHY",
      IText(30, ink, "Arial", EAlign::Near)));
    // Remove the large square/rectangular backdrop that IVControls draw by
    // default behind each widget. Keeping kBG fully transparent lets the
    // controls float directly on the chassis while preserving the knob/button
    // faces, shadows, labels and value readouts.
    auto style = DEFAULT_STYLE.WithColor(kBG, IColor(0, 0, 0, 0)).WithColor(kFG, IColor(255, 67, 71, 63))
      .WithColor(kPR, accent).WithColor(kFR, muted).WithColor(kHL, ink)
      .WithLabelText(IText(12, ink, "Arial"))
      .WithValueText(IText(13, accent, "Arial", EAlign::Center, EVAlign::Bottom))
      .WithRoundness(0.22f).WithDrawShadows(true);
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
    g->AttachControl(new ITextControl(IRECT(772, 95, 990, 110), "GRAPHIC EQ  /  Hz",
      IText(11.0f, muted, "Arial", EAlign::Center, EVAlign::Middle)));
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
      .WithLabelText(IText(9.8f, muted, "Arial", EAlign::Center, EVAlign::Middle))
      .WithValueText(IText(9.2f, ink, "Arial", EAlign::Center, EVAlign::Middle))
      .WithRoundness(0.08f)
      .WithDrawShadows(true);
    // Keep the complete selector inside the switching strip instead of letting
    // its caption float above the frame.  It is slightly narrower/shorter so
    // all three positions have breathing room from the surrounding bevels.
    g->AttachControl(new ITextControl(IRECT(892, 280, 966, 292), "GRAPHIC EQ",
      IText(9.8f, muted, "Arial", EAlign::Center, EVAlign::Middle)));
    g->AttachControl(new IVTabSwitchControl(IRECT(892, 293, 966, 348), EqMode,
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
    g->AttachControl(new ITextControl(IRECT(510, 360, 605, 378), "REVERB",
      IText(12, ink, "Arial", EAlign::Center, EVAlign::Middle)));
    const auto knobNoLabelStyle = style.WithLabelText(IText(12, IColor(0, 0, 0, 0), "Arial"));
    g->AttachControl(new IVKnobControl(IRECT(510, 352, 605, 497), Reverb, "", knobNoLabelStyle, true));
    g->AttachControl(new IVToggleControl(IRECT(625, 369, 750, 426), ReverbOn, "REVERB F/S", switchStyle, "OFF", "ON"));
    g->AttachControl(new IVToggleControl(IRECT(770, 369, 920, 426), LaterSimul20p, "SIMUL HF CAP", switchStyle, "10pF", "20pF"));
    g->AttachControl(new IVToggleControl(IRECT(940, 369, 1080, 426), ExportBias, "BIAS MODE", switchStyle, "-67V", "-55V"));
    // Draw the knob beautification overlay *after* the actual knob controls so
    // the added bevel/gloss sits on top of the standard iPlug2 knob faces.
    g->AttachControl(new CrunchyKnob3DOverlayControl(g->GetBounds()));

    // RHYTHM removes LDR2/LDR3/LDR4 from the signal path and the LDR1 Treble
    // Shift LED shares that Lead return bus. Dim those four controls only; all
    // pre-split and post-return controls remain active in both channels.
    g->AttachControl(new IChannelModeUIState(IRECT(0, 0, 1, 1), GetParam(Lead)));

    // Six-state resize handle interaction:
    // drag selects a phase, resize happens once on mouse-up, and double-click
    // returns to NORMAL. The compact handle lives in the otherwise empty corner.
    g->AttachControl(new CrunchySteppedEditorResizeControl(g->GetBounds()));

    // Custom .dtcpreset manager is deliberately the last interactive control so
    // its modal browser always renders/captures input above the complete amp UI.
    g->AttachControl(new CrunchyPresetManagerControl(g->GetBounds(), *this), kCtrlTagPresetManager);

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


std::string DerTondehrCrunchy::PresetPreferencesPath() const {
  return CrunchyPathToUTF8(CrunchyUserConfigDirectory() / "preset_directory.txt");
}

bool DerTondehrCrunchy::SavePresetDirectoryPreferenceValue(const std::string& root) const {
  std::vector<std::uint8_t> bytes(root.begin(), root.end());
  bytes.push_back(static_cast<std::uint8_t>('\n'));
  return CrunchyWriteBinaryAtomically(CrunchyPathFromUTF8(PresetPreferencesPath()), bytes);
}

void DerTondehrCrunchy::LoadPresetDirectoryPreference() {
  mPresetRootDirectory.clear();
  mPresetCurrentDirectory.clear();
  mPresetSelectedPath.clear();
  mPresetStatusMessage.clear();
  mPresetStatusIsError = false;

  std::ifstream file(CrunchyPathFromUTF8(PresetPreferencesPath()), std::ios::binary);
  if (!file) return;
  std::string line;
  std::getline(file, line);
  if (line.empty()) return;

  namespace fs = std::filesystem;
  fs::path p = CrunchyPathFromUTF8(line).lexically_normal();
  std::error_code ec;
  const bool available = fs::exists(p, ec) && !ec && fs::is_directory(p, ec) && !ec;
  if (CrunchyPathHasAnyLinkOrReparse(p)) {
    mPresetRootDirectory = CrunchyPathToUTF8(p);
    mPresetCurrentDirectory = mPresetRootDirectory;
    mPresetStatusMessage = "Preset directory contains a link/junction and is blocked";
    mPresetStatusIsError = true;
    return;
  }
  const fs::path stored = available ? CrunchyCanonicalForBoundary(p) : p;
  mPresetRootDirectory = CrunchyPathToUTF8(stored);
  mPresetCurrentDirectory = mPresetRootDirectory;
  if (!available) {
    mPresetStatusMessage = "Preset directory is unavailable";
    mPresetStatusIsError = true;
  }
}

bool DerTondehrCrunchy::PresetRootAvailable() const {
  if (mPresetRootDirectory.empty()) return false;
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path root = CrunchyPathFromUTF8(mPresetRootDirectory);
  return fs::exists(root, ec) && !ec && fs::is_directory(root, ec) && !ec
      && !CrunchyPathHasAnyLinkOrReparse(root);
}

bool DerTondehrCrunchy::PresetPathIsInsideRoot(const std::string& path) const {
  if (mPresetRootDirectory.empty() || path.empty()) return false;
  return CrunchyStablePathIsSameOrBelow(CrunchyPathFromUTF8(path), CrunchyPathFromUTF8(mPresetRootDirectory));
}

bool DerTondehrCrunchy::ComputePresetFileIdentity(const std::string& path, std::uint64_t& identity) const {
  namespace fs = std::filesystem;
  identity = 0u;
  const fs::path p = CrunchyPathFromUTF8(path);
  std::error_code ec;
  if (!fs::exists(p, ec) || ec || !fs::is_regular_file(p, ec) || ec || CrunchyPathEntryIsLinkOrReparse(p))
    return false;

  constexpr std::uint64_t kOffset = 1469598103934665603ull;
  constexpr std::uint64_t kPrime = 1099511628211ull;
  std::uint64_t h = kOffset;
  const auto add = [&](const void* data, std::size_t n) {
    const auto* b = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n; ++i) { h ^= static_cast<std::uint64_t>(b[i]); h *= kPrime; }
  };
  const std::string canonical = CrunchyPathToUTF8(CrunchyCanonicalForBoundary(p));
  add(canonical.data(), canonical.size());
  const auto size = fs::file_size(p, ec); if (ec) return false; add(&size, sizeof(size));
  const auto time = fs::last_write_time(p, ec); if (ec) return false;
  const auto ticks = time.time_since_epoch().count(); add(&ticks, sizeof(ticks));
#if defined(_WIN32)
  HANDLE handle = ::CreateFileW(p.wstring().c_str(), FILE_READ_ATTRIBUTES,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  BY_HANDLE_FILE_INFORMATION info {};
  const BOOL ok = ::GetFileInformationByHandle(handle, &info);
  ::CloseHandle(handle);
  if (!ok || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return false;
  add(&info.dwVolumeSerialNumber, sizeof(info.dwVolumeSerialNumber));
  add(&info.nFileIndexHigh, sizeof(info.nFileIndexHigh));
  add(&info.nFileIndexLow, sizeof(info.nFileIndexLow));
#endif
  identity = h;
  return true;
}

void DerTondehrCrunchy::NotifyHostCustomPresetStateChanged() {
  DirtyParametersFromUI();
  SendCurrentParamValuesFromDelegate();
}

bool DerTondehrCrunchy::SavePresetFile(const std::string& path) {
  namespace fs = std::filesystem;
  mPresetStatusMessage.clear();
  mPresetStatusIsError = false;
  if (!PresetRootAvailable()) {
    mPresetStatusMessage = "No preset directory configured";
    mPresetStatusIsError = true;
    return false;
  }

  fs::path destination = CrunchyPathFromUTF8(path).lexically_normal();
  if (destination.extension().empty()) destination += kPresetExtension;
  else if (!CrunchyIsPresetPath(CrunchyPathToUTF8(destination))) destination.replace_extension(kPresetExtension);
  const std::string destinationText = CrunchyPathToUTF8(destination);
  if (!PresetPathIsInsideRoot(destinationText)) {
    mPresetStatusMessage = "Preset must be saved inside the configured directory";
    mPresetStatusIsError = true;
    return false;
  }
  const fs::path parent = destination.parent_path();
  if (parent.empty() || !CrunchyStablePathIsSameOrBelow(parent, CrunchyPathFromUTF8(mPresetRootDirectory))
      || !CrunchyDirectoryWritable(parent)) {
    mPresetStatusMessage = "Preset folder is not writable or is unsafe";
    mPresetStatusIsError = true;
    return false;
  }

  std::error_code ec;
  const bool exists = fs::exists(destination, ec) && !ec;
  if (exists) {
    if (!fs::is_regular_file(destination, ec) || ec || CrunchyPathEntryIsLinkOrReparse(destination)) {
      mPresetStatusMessage = "Preset destination is not a regular file";
      mPresetStatusIsError = true;
      mPresetPendingOverwriteValid = false;
      return false;
    }
    std::uint64_t identity = 0u;
    if (!ComputePresetFileIdentity(destinationText, identity)) {
      mPresetStatusMessage = "Could not verify existing preset before overwrite";
      mPresetStatusIsError = true;
      mPresetPendingOverwriteValid = false;
      return false;
    }
    const bool same = mPresetPendingOverwriteValid
      && CrunchyPathsReferToSameLocation(CrunchyPathFromUTF8(mPresetPendingOverwritePath), destination);
    if (!same || identity != mPresetPendingOverwriteIdentity) {
      mPresetPendingOverwritePath = destinationText;
      mPresetPendingOverwriteIdentity = identity;
      mPresetPendingOverwriteValid = true;
      mPresetStatusMessage = "Preset exists - choose the same file and press SAVE again";
      mPresetStatusIsError = true;
#if IPLUG_EDITOR
      MarkPresetUIChanged();
#endif
      return false;
    }
  } else {
    mPresetPendingOverwritePath.clear();
    mPresetPendingOverwriteIdentity = 0u;
    mPresetPendingOverwriteValid = false;
  }

  std::vector<crunchy::preset::Entry> entries;
  entries.reserve(kCrunchyPresetParams.size());
  for (const auto& spec : kCrunchyPresetParams) {
    const double value = GetParam(spec.paramIdx)->Value();
    if (!CrunchyPresetValueValid(spec, value)) {
      mPresetStatusMessage = "A control contains an invalid value; preset was not saved";
      mPresetStatusIsError = true;
      return false;
    }
    entries.push_back({spec.stableId, spec.type, value});
  }
  std::vector<std::uint8_t> bytes;
  if (!crunchy::preset::BuildFile(entries, bytes)) {
    mPresetStatusMessage = "Could not build a valid .dtcpreset file";
    mPresetStatusIsError = true;
    return false;
  }

  if (!PresetPathIsInsideRoot(destinationText)
      || !CrunchyStablePathIsSameOrBelow(parent, CrunchyPathFromUTF8(mPresetRootDirectory))) {
    mPresetStatusMessage = "Preset path became unsafe before save";
    mPresetStatusIsError = true;
    return false;
  }
  if (exists) {
    std::uint64_t identity = 0u;
    if (!ComputePresetFileIdentity(destinationText, identity) || identity != mPresetPendingOverwriteIdentity) {
      mPresetStatusMessage = "Preset changed before overwrite - confirmation cancelled";
      mPresetStatusIsError = true;
      mPresetPendingOverwriteValid = false;
      return false;
    }
  }
  if (!CrunchyWriteBinaryAtomically(destination, bytes)) {
    mPresetStatusMessage = "Could not write preset";
    mPresetStatusIsError = true;
    return false;
  }

  mPresetPendingOverwritePath.clear();
  mPresetPendingOverwriteIdentity = 0u;
  mPresetPendingOverwriteValid = false;
  mPresetSelectedPath = destinationText;
  mPresetCurrentDirectory = CrunchyPathToUTF8(destination.parent_path());
  mPresetStatusMessage = "Saved: " + CrunchyPathToUTF8(destination.stem());
  mPresetStatusIsError = false;
#if IPLUG_EDITOR
  ClearPresetDeleteIdentity();
  MarkPresetUIChanged();
#endif
  return true;
}

bool DerTondehrCrunchy::LoadPresetFile(const std::string& path) {
  namespace fs = std::filesystem;
  mPresetStatusMessage.clear();
  mPresetStatusIsError = false;
  const fs::path presetPath = CrunchyPathFromUTF8(path).lexically_normal();
  const std::string presetPathText = CrunchyPathToUTF8(presetPath);
  if (!PresetRootAvailable() || !PresetPathIsInsideRoot(presetPathText) || !CrunchyIsPresetPath(presetPathText)) {
    mPresetStatusMessage = "Only valid .dtcpreset files inside the configured directory can be loaded";
    mPresetStatusIsError = true;
#if IPLUG_EDITOR
    MarkPresetUIChanged();
#endif
    return false;
  }
  std::error_code ec;
  if (!fs::exists(presetPath, ec) || ec || !fs::is_regular_file(presetPath, ec) || ec
      || CrunchyPathEntryIsLinkOrReparse(presetPath)) {
    mPresetStatusMessage = "Preset is not a stable regular file";
    mPresetStatusIsError = true;
#if IPLUG_EDITOR
    MarkPresetUIChanged();
#endif
    return false;
  }
  std::uint64_t initialIdentity = 0u;
  if (!ComputePresetFileIdentity(presetPathText, initialIdentity)) {
    mPresetStatusMessage = "Could not verify preset before reading";
    mPresetStatusIsError = true;
#if IPLUG_EDITOR
    MarkPresetUIChanged();
#endif
    return false;
  }
  const auto fileSize = fs::file_size(presetPath, ec);
  if (ec || fileSize < crunchy::preset::kHeaderBytes || fileSize > crunchy::preset::kMaxFileBytes) {
    mPresetStatusMessage = "Preset is corrupted or invalid";
    mPresetStatusIsError = true;
#if IPLUG_EDITOR
    MarkPresetUIChanged();
#endif
    return false;
  }
  std::ifstream in(presetPath, std::ios::binary);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
  if (!in || (!bytes.empty() && !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))) {
    mPresetStatusMessage = "Could not read preset";
    mPresetStatusIsError = true;
#if IPLUG_EDITOR
    MarkPresetUIChanged();
#endif
    return false;
  }
  std::uint64_t finalIdentity = 0u;
  if (!PresetPathIsInsideRoot(presetPathText) || CrunchyPathEntryIsLinkOrReparse(presetPath)
      || !ComputePresetFileIdentity(presetPathText, finalIdentity) || finalIdentity != initialIdentity) {
    mPresetStatusMessage = "Preset changed or became unsafe while reading";
    mPresetStatusIsError = true;
#if IPLUG_EDITOR
    MarkPresetUIChanged();
#endif
    return false;
  }

  std::vector<crunchy::preset::Entry> entries;
  crunchy::preset::ParseError parseError {};
  if (!crunchy::preset::ParseFile(bytes, entries, parseError)) {
    mPresetStatusMessage = crunchy::preset::ParseErrorText(parseError);
    mPresetStatusIsError = true;
#if IPLUG_EDITOR
    MarkPresetUIChanged();
#endif
    return false;
  }
  if (entries.size() != kCrunchyPresetParams.size()) {
    mPresetStatusMessage = "Preset control set does not match Der Tondehr Crunchy";
    mPresetStatusIsError = true;
#if IPLUG_EDITOR
    MarkPresetUIChanged();
#endif
    return false;
  }

  std::array<double, kCrunchyPresetParams.size()> validatedValues {};
  std::array<bool, kCrunchyPresetParams.size()> seen {};
  for (const auto& entry : entries) {
    const CrunchyPresetParamSpec* spec = CrunchyPresetSpec(entry.id);
    if (!spec || spec->type != entry.type || !CrunchyPresetValueValid(*spec, entry.value)) {
      mPresetStatusMessage = "Preset contains an unknown control, wrong control type, or invalid value";
      mPresetStatusIsError = true;
#if IPLUG_EDITOR
      MarkPresetUIChanged();
#endif
      return false;
    }
    const auto it = std::find_if(kCrunchyPresetParams.begin(), kCrunchyPresetParams.end(),
      [&](const CrunchyPresetParamSpec& candidate) { return candidate.stableId == entry.id; });
    const std::size_t index = static_cast<std::size_t>(std::distance(kCrunchyPresetParams.begin(), it));
    if (index >= seen.size() || seen[index]) {
      mPresetStatusMessage = "Preset contains duplicate or invalid control IDs";
      mPresetStatusIsError = true;
#if IPLUG_EDITOR
      MarkPresetUIChanged();
#endif
      return false;
    }
    seen[index] = true;
    validatedValues[index] = entry.value;
  }
  if (std::find(seen.begin(), seen.end(), false) != seen.end()) {
    mPresetStatusMessage = "Preset is missing one or more Crunchy controls";
    mPresetStatusIsError = true;
#if IPLUG_EDITOR
    MarkPresetUIChanged();
#endif
    return false;
  }

#if IPLUG_DSP
  mPresetRecallInProgress.store(true, std::memory_order_release);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
  while (mPresetAudioBlocksInFlight.load(std::memory_order_acquire) > 0) {
    if (std::chrono::steady_clock::now() >= deadline) {
      mPresetRecallInProgress.store(false, std::memory_order_release);
      mPresetStatusMessage = "Audio engine is busy - preset recall cancelled";
      mPresetStatusIsError = true;
#if IPLUG_EDITOR
      MarkPresetUIChanged();
#endif
      return false;
    }
    std::this_thread::yield();
  }
#endif

  for (std::size_t i = 0; i < kCrunchyPresetParams.size(); ++i)
    GetParam(kCrunchyPresetParams[i].paramIdx)->Set(validatedValues[i]);

#if IPLUG_DSP
  mAmp.Prepare(GetSampleRate(), ReadParameters());
  mInputMeterState.Reset();
  mOutputMeterState.Reset();
  mPresetRecallInProgress.store(false, std::memory_order_release);
#endif
  NotifyHostCustomPresetStateChanged();
  mPresetSelectedPath = presetPathText;
  mPresetCurrentDirectory = CrunchyPathToUTF8(presetPath.parent_path());
  mPresetStatusMessage = "Loaded: " + CrunchyPathToUTF8(presetPath.stem());
  mPresetStatusIsError = false;
#if IPLUG_EDITOR
  ClearPresetDeleteIdentity();
  MarkPresetUIChanged();
#endif
  return true;
}

bool DerTondehrCrunchy::DeleteSelectedPresetFile() {
  namespace fs = std::filesystem;
  if (!PresetRootAvailable() || mPresetSelectedPath.empty() || !PresetPathIsInsideRoot(mPresetSelectedPath)
      || !CrunchyIsPresetPath(mPresetSelectedPath) || !mPresetDeleteIdentityValid) {
    mPresetStatusMessage = "Select and confirm a preset to delete";
    mPresetStatusIsError = true;
#if IPLUG_EDITOR
    MarkPresetUIChanged();
#endif
    return false;
  }
  const fs::path target = CrunchyPathFromUTF8(mPresetSelectedPath);
  std::uint64_t identity = 0u;
  if (!ComputePresetFileIdentity(mPresetSelectedPath, identity) || identity != mPresetDeleteIdentity
      || !PresetPathIsInsideRoot(mPresetSelectedPath) || CrunchyPathEntryIsLinkOrReparse(target)) {
    mPresetStatusMessage = "Preset changed or became unsafe after confirmation";
    mPresetStatusIsError = true;
    mPresetDeleteIdentityValid = false;
#if IPLUG_EDITOR
    MarkPresetUIChanged();
#endif
    return false;
  }
  const std::string name = CrunchyPathToUTF8(target.stem());
  std::error_code ec;
  fs::remove(target, ec);
  if (ec) {
    mPresetStatusMessage = "Could not delete preset";
    mPresetStatusIsError = true;
#if IPLUG_EDITOR
    MarkPresetUIChanged();
#endif
    return false;
  }
  mPresetSelectedPath.clear();
  mPresetDeleteIdentityValid = false;
  mPresetStatusMessage = "Deleted: " + name;
  mPresetStatusIsError = false;
#if IPLUG_EDITOR
  MarkPresetUIChanged();
#endif
  return true;
}

#if IPLUG_EDITOR
bool DerTondehrCrunchy::ArmSelectedPresetDelete() {
  mPresetDeleteIdentity = 0u;
  mPresetDeleteIdentityValid = false;
  if (!PresetRootAvailable() || mPresetSelectedPath.empty()
      || !PresetPathIsInsideRoot(mPresetSelectedPath) || !CrunchyIsPresetPath(mPresetSelectedPath)) return false;
  std::uint64_t identity = 0u;
  if (!ComputePresetFileIdentity(mPresetSelectedPath, identity)) {
    mPresetStatusMessage = "Could not verify selected preset for deletion";
    mPresetStatusIsError = true;
    MarkPresetUIChanged();
    return false;
  }
  mPresetDeleteIdentity = identity;
  mPresetDeleteIdentityValid = true;
  return true;
}

void DerTondehrCrunchy::ClearPresetDeleteIdentity() {
  mPresetDeleteIdentity = 0u;
  mPresetDeleteIdentityValid = false;
}

void DerTondehrCrunchy::MarkPresetUIChanged() {
  mPresetUIRevision.fetch_add(1u, std::memory_order_relaxed);
  mPresetDirectoryFingerprintValid = false;
  if (auto* ui = GetUI())
    if (auto* control = ui->GetControlWithTag(kCtrlTagPresetManager)) control->SetDirty(false);
}

void DerTondehrCrunchy::PromptSetPresetDirectory() {
  if (!mPresetRootDirectory.empty()) return;
  auto* ui = GetUI(); if (!ui) return;
  WDL_String directory;
  ui->PromptForDirectory(directory);
  if (directory.GetLength() == 0) return;
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path chosen = fs::u8path(directory.Get());
  if (!fs::exists(chosen, ec) || ec || !fs::is_directory(chosen, ec) || ec) {
    mPresetStatusMessage = "Could not open preset directory"; mPresetStatusIsError = true; MarkPresetUIChanged(); return;
  }
  if (CrunchyPathHasAnyLinkOrReparse(chosen)) {
    mPresetStatusMessage = "Preset directory cannot contain symbolic links or junctions";
    mPresetStatusIsError = true; MarkPresetUIChanged(); return;
  }
  chosen = CrunchyCanonicalForBoundary(chosen);
  if (!CrunchyDirectoryWritable(chosen)) {
    mPresetStatusMessage = "Preset directory is not writable"; mPresetStatusIsError = true; MarkPresetUIChanged(); return;
  }
  const std::string candidate = CrunchyPathToUTF8(chosen);
  if (!SavePresetDirectoryPreferenceValue(candidate)) {
    mPresetStatusMessage = "Could not save preset directory preference";
    mPresetStatusIsError = true; MarkPresetUIChanged(); return;
  }
  mPresetRootDirectory = candidate;
  mPresetCurrentDirectory = candidate;
  mPresetSelectedPath.clear();
  mPresetPendingOverwriteValid = false;
  ClearPresetDeleteIdentity();
  mPresetStatusMessage = "Preset directory configured";
  mPresetStatusIsError = false;
  MarkPresetUIChanged();
}

void DerTondehrCrunchy::RemovePresetDirectory() {
  if (mPresetRootDirectory.empty()) return;
  if (!SavePresetDirectoryPreferenceValue(std::string{})) {
    mPresetStatusMessage = "Could not clear preset directory preference";
    mPresetStatusIsError = true; MarkPresetUIChanged(); return;
  }
  mPresetRootDirectory.clear(); mPresetCurrentDirectory.clear(); mPresetSelectedPath.clear();
  mPresetPendingOverwritePath.clear(); mPresetPendingOverwriteValid = false;
  ClearPresetDeleteIdentity(); mPresetStatusMessage.clear(); mPresetStatusIsError = false;
  MarkPresetUIChanged();
}

void DerTondehrCrunchy::PromptSavePreset() {
  if (!PresetRootAvailable()) {
    mPresetStatusMessage = mPresetRootDirectory.empty() ? "No preset directory configured" : "Preset directory is unavailable";
    mPresetStatusIsError = true; MarkPresetUIChanged(); return;
  }
  namespace fs = std::filesystem;
  const fs::path root = CrunchyPathFromUTF8(mPresetRootDirectory);
  fs::path current = mPresetCurrentDirectory.empty() ? root : CrunchyPathFromUTF8(mPresetCurrentDirectory);
  if (!CrunchyStablePathIsSameOrBelow(current, root)) current = root;
  std::error_code ec;
  if (!fs::exists(current, ec) || ec || !fs::is_directory(current, ec) || ec) current = root;
  auto* ui = GetUI(); if (!ui) return;
  WDL_String fileName; fileName.Set("Der Tondehr Crunchy Preset.dtcpreset");
  WDL_String pathText; const std::string currentUtf8 = CrunchyPathToUTF8(current); pathText.Set(currentUtf8.c_str());
  ui->PromptForFile(fileName, pathText, EFileAction::Save, "dtcpreset");
  if (fileName.GetLength() == 0) return;
  fs::path destination = fs::u8path(fileName.Get());
  if (!destination.is_absolute()) destination = fs::u8path(pathText.GetLength() > 0 ? pathText.Get() : currentUtf8.c_str()) / destination;
  if (destination.extension().empty()) destination += kPresetExtension;
  else if (!CrunchyIsPresetPath(CrunchyPathToUTF8(destination))) destination.replace_extension(kPresetExtension);
  SavePresetFile(CrunchyPathToUTF8(destination.lexically_normal()));
}

std::uint64_t DerTondehrCrunchy::ComputeCurrentPresetDirectoryFingerprint() const {
  namespace fs = std::filesystem;
  constexpr std::uint64_t kOffset = 1469598103934665603ull, kPrime = 1099511628211ull;
  std::uint64_t hash = kOffset;
  const auto add = [&](const std::string& text) {
    for (const unsigned char c : text) { hash ^= static_cast<std::uint64_t>(c); hash *= kPrime; }
    hash ^= 0xffu; hash *= kPrime;
  };
  if (!PresetRootAvailable()) return hash;
  const fs::path root = CrunchyPathFromUTF8(mPresetRootDirectory);
  fs::path current = mPresetCurrentDirectory.empty() ? root : CrunchyPathFromUTF8(mPresetCurrentDirectory);
  if (!CrunchyStablePathIsSameOrBelow(current, root)) current = root;
  add(CrunchyPathToUTF8(CrunchyCanonicalForBoundary(current)));
  std::vector<std::string> records;
  std::error_code ec;
  for (fs::directory_iterator it(current, fs::directory_options::skip_permission_denied, ec), end;
       it != end && !ec; it.increment(ec)) {
    const fs::directory_entry& de = *it;
    std::error_code te;
    const bool dir = de.is_directory(te) && !te && !CrunchyPathEntryIsLinkOrReparse(de.path())
      && CrunchyStablePathIsSameOrBelow(de.path(), root);
    te.clear();
    const std::string pathText = CrunchyPathToUTF8(de.path());
    const bool preset = de.is_regular_file(te) && !te && CrunchyIsPresetPath(pathText)
      && !CrunchyPathEntryIsLinkOrReparse(de.path()) && CrunchyStablePathIsSameOrBelow(de.path(), root);
    if (!dir && !preset) continue;
    std::string record = dir ? "D:" : "P:";
    record += CrunchyPathToUTF8(de.path().filename());
    if (preset) {
      std::error_code me;
      const auto sz = de.file_size(me); record += me ? ":size-error" : (":" + std::to_string(sz));
      me.clear(); const auto t = de.last_write_time(me); record += me ? ":time-error" : (":" + std::to_string(t.time_since_epoch().count()));
    }
    records.push_back(std::move(record));
  }
  std::sort(records.begin(), records.end());
  for (const auto& record : records) add(record);
  if (ec) add("<listing-error:" + std::to_string(ec.value()) + ">");
  add(std::to_string(records.size()));
  return hash;
}

void DerTondehrCrunchy::PollPresetDirectoryChanges() {
  if (!GetUI() || !mPresetBrowserOpen) return;
  using namespace std::chrono;
  const auto now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  if (mPresetLastDirectoryPollMs != 0 && now - mPresetLastDirectoryPollMs < 1000) return;
  mPresetLastDirectoryPollMs = now;
  if (!mPresetRootDirectory.empty() && !PresetRootAvailable()) {
    mPresetStatusMessage = CrunchyPathHasAnyLinkOrReparse(CrunchyPathFromUTF8(mPresetRootDirectory))
      ? "Preset directory contains a link/junction and is blocked" : "Preset directory is unavailable";
    mPresetStatusIsError = true; mPresetCurrentDirectory = mPresetRootDirectory; mPresetSelectedPath.clear();
    ClearPresetDeleteIdentity(); mPresetDirectoryFingerprintValid = false; mPresetUIRevision.fetch_add(1u, std::memory_order_relaxed);
    if (auto* c = GetUI()->GetControlWithTag(kCtrlTagPresetManager)) c->SetDirty(false);
    return;
  }
  if (!PresetRootAvailable()) return;
  const std::uint64_t fingerprint = ComputeCurrentPresetDirectoryFingerprint();
  if (!mPresetDirectoryFingerprintValid) {
    mPresetDirectoryFingerprint = fingerprint; mPresetDirectoryFingerprintValid = true;
  } else if (fingerprint != mPresetDirectoryFingerprint) {
    mPresetDirectoryFingerprint = fingerprint; ClearPresetDeleteIdentity(); mPresetPendingOverwriteValid = false;
    mPresetUIRevision.fetch_add(1u, std::memory_order_relaxed);
    if (auto* c = GetUI()->GetControlWithTag(kCtrlTagPresetManager)) c->SetDirty(false);
  }
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
  const int nInputs = std::clamp(NInChansConnected(), 0, 2);
  const int nOutputs = std::clamp(NOutChansConnected(), 0, 2);
  const auto silence = [&]() {
    for (int ch = 0; ch < nOutputs; ++ch)
      if (outputs[ch]) std::fill(outputs[ch], outputs[ch] + nFrames, static_cast<sample>(0));
  };

  // Preset recall is committed on the UI thread only after the complete file,
  // extension, product signature, SHA-256 digest and control map have passed.
  // Gate audio while mAmp is reset so no block can observe a half-applied preset.
  if (mPresetRecallInProgress.load(std::memory_order_acquire)) {
    silence();
    return;
  }
  mPresetAudioBlocksInFlight.fetch_add(1, std::memory_order_acq_rel);
  if (mPresetRecallInProgress.load(std::memory_order_acquire)) {
    mPresetAudioBlocksInFlight.fetch_sub(1, std::memory_order_acq_rel);
    silence();
    return;
  }

  const Parameters params = ReadParameters();
  mAmp.SetParameters(params);
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
  mPresetAudioBlocksInFlight.fetch_sub(1, std::memory_order_acq_rel);
}
#endif
