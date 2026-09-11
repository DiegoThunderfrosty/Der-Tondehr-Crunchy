// Real APP wrapper and real plugin constructor, without a GUI or audio device.
#include "DerTondehrCrunchy.h"
#include "resource.h"
#include "IPlugPaths.h"
#include "IGraphics.h"
#include <cstdio>
#include <cstdlib>
#include <crtdbg.h>
#include <memory>

extern HINSTANCE gHINSTANCE;
int main(int argc, char** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _set_error_mode(_OUT_TO_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  gHINSTANCE = GetModuleHandle(nullptr);
  if (argc == 3 && std::string(argv[1]) == "--vst3-resource") {
    HMODULE module = LoadLibraryExA(argv[2], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) return 6;
    const bool hasDialog = FindResourceA(module, MAKEINTRESOURCEA(IDD_DIALOG_MAIN), RT_DIALOG) != nullptr;
    FreeLibrary(module);
    if (!hasDialog) {
      std::fprintf(stderr, "FAIL: expected Windows dialog resource is missing from the VST3 binary\n"); return 7;
    }
    std::puts("PASS: VST3 Windows resources are present");
    return 0;
  }
  std::puts("Construct APP plugin"); std::fflush(stdout);
  auto plugin = std::make_unique<DerTondehrCrunchy>(iplug::InstanceInfo{nullptr});
  // This target is compiled through the APP wrapper, so verify the format-
  // specific fresh-instance defaults as well as the shared 5.00 reset points.
  for (int i = crunchy::Volume; i <= crunchy::LeadMaster; ++i) {
    if (std::abs(plugin->GetParam(i)->Value() - 5.0) > 1e-12 ||
        std::abs(plugin->GetParam(i)->GetDefault() - 5.0) > 1e-12) return 9;
  }
  if (plugin->GetParam(crunchy::ReverbOn)->Value() >= 0.5) return 10;
  if (plugin->GetParam(crunchy::Bypass)->Value() < 0.5) return 11;
  std::puts("Constructor OK; startup defaults OK; check resources"); std::fflush(stdout);
  if (!FindResourceA(gHINSTANCE, MAKEINTRESOURCEA(IDD_DIALOG_MAIN), RT_DIALOG)) return 2;
  plugin->OnReset();
  double input[128] {}, outputL[128] {}, outputR[128] {};
  double* ins[] = {input, input}; double* outs[] = {outputL, outputR};
  plugin->ProcessBlock(ins, outs, 128);
  for (double x : outputL) if (!std::isfinite(x) || x != 0) return 3;
  if (argc > 1 && std::string(argv[1]) == "--graphics") {
    // This test never shows or enables a top-level window, never opens audio,
    // and cannot take keyboard focus. It only exercises our own editor code.
    std::puts("Create disabled, hidden test parent"); std::fflush(stdout);
    HWND parent = CreateWindowExA(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, "STATIC", "Crunchy internal test",
      WS_POPUP | WS_DISABLED, 0, 0, PLUG_WIDTH, PLUG_HEIGHT, nullptr, nullptr, gHINSTANCE, nullptr);
    if (!parent) return 4;
    for (int i = 0; i < 3; ++i) {
      std::puts("Open editor on hidden parent"); std::fflush(stdout);
      HWND editor = static_cast<HWND>(plugin->OpenWindow(parent));
      if (!editor || !plugin->GetUI()) return 5;
      iplug::igraphics::IRECT textBounds(0,0,500,50);
      plugin->GetUI()->MeasureText(iplug::igraphics::IText(16, iplug::igraphics::COLOR_WHITE, "Arial"),
        "Der Tondehr Crunchy", textBounds);
      if (textBounds.W() <= 0) return 8;
      std::printf("Editor layout OK: %d controls\n", plugin->GetUI()->NControls()); std::fflush(stdout);
      plugin->CloseWindow();
    }
    DestroyWindow(parent);
    std::puts("PASS: editor opened and closed three times on hidden disabled parent.");
  }
  std::puts("Process OK; destroy plugin"); std::fflush(stdout);
  plugin.reset();
  std::puts("PASS: APP constructor, resources, reset, audio, destructor; no visible window or audio device opened.");
}
