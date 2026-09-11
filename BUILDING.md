# Building

The complete clean-PC procedure is in [README.md](README.md#complete-setup-on-a-clean-pc).

For a computer that already has Git, Visual Studio with C++, a Windows SDK, CMake, and PowerShell:

```powershell
git clone https://github.com/DiegoThunderfrosty/Der-Tondehr-Crunchy.git
cd Der-Tondehr-Crunchy
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup_dependencies.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

Expected output:

```text
build\windows\out\DerTondehrCrunchy.vst3\
build\windows\out\DerTondehrCrunchy.exe
```

The compatible framework and SDK revisions are pinned in `scripts/setup_dependencies.ps1`. Do not update either dependency without building both products and running the complete test suite.
