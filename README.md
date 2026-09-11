# Der Tondehr Crunchy

Der Tondehr Crunchy is an independent, open-source guitar amplifier plugin inspired by aspects of the circuit architecture and control layout of the **MESA/Boogie Mark IIC+**. It is an unofficial technical and creative project. It is not affiliated with, sponsored by, endorsed by, or approved by MESA/Boogie or its owner. MESA/Boogie and Mark IIC+ are used here only to identify the amplifier that inspired the project; they are not part of the plugin name or branding.

The project is written in C++17 with iPlug2 and currently builds two Windows x64 products:

- `DerTondehrCrunchy.vst3`, for use in a compatible audio plugin host.
- `DerTondehrCrunchy.exe`, a standalone application with DirectSound, ASIO, and WASAPI device selection.

The plugin models the amplifier section only. It does not contain a speaker cabinet or an impulse response. A typical signal chain is:

```text
Guitar or DI -> Der Tondehr Crunchy -> cabinet impulse-response loader -> output
```

## Main features

- Rhythm and Lead channels.
- Volume, tone, Lead gain, and Lead level controls.
- Bright, Shift, and Deep switching options.
- Five-band graphic equalizer with Auto, Out, and In modes.
- Presence and two power-stage configurations.
- Reverb level and footswitch.
- Input and output trims with peak meters and clipping indicators.
- Selectable 1x, 2x, 4x, and 8x oversampling.
- Six editor sizes with monitor-aware scaling.
- Mono and stereo processing.
- Standalone input/output device, channel, sample-rate, and buffer selection.

## Current status

This package contains version `0.10.24`. The complete plugin and standalone build currently targets Windows x64. Portable DSP tests can be compiled on other operating systems, but plugin and standalone binaries for those platforms are not currently documented or supported by this repository.

## Repository contents

```text
Der-Tondehr-Crunchy/
|-- DerTondehrCrunchy/       Plugin, DSP, UI, and resource source files
|-- tests/                   Control, DSP, meter, and editor-scale tests
|-- scripts/                 Dependency setup and build scripts
|-- docs/                    Third-party license copies
|-- .github/                 Issue, pull-request, and automation files
|-- .gitattributes           Text-file normalization rules
|-- .gitignore               Generated/private-file exclusions
|-- BUILDING.md              Short build reference
|-- CHANGELOG.md             Public release history
|-- CONTRIBUTING.md          Contribution requirements
|-- LICENSE                  MIT License for original project code
|-- SECURITY.md              Security-reporting policy
|-- THIRD_PARTY.md           Dependency attribution
|-- CMakeLists.txt           Main CMake configuration
`-- README.md                This document
```

Development references, manuals, circuit drawings, recording sessions, audio files, impulse responses, local builds, and downloaded dependencies are intentionally excluded. They are not needed to compile the source package.

## Build requirements

Before starting, you need all of the following:

1. A 64-bit installation of Windows 10 or Windows 11.
2. An Internet connection for the initial dependency download.
3. Git for Windows.
4. Visual Studio with the **Desktop development with C++** workload.
5. The MSVC x64/x86 compiler tools.
6. A recent Windows SDK.
7. CMake tools for Windows.
8. PowerShell 5.1 or later.
9. Free disk space for the source, dependencies, and build output.

The known development configuration uses Visual Studio 2026 and CMake 3.25 or later. Other versions may work, but they are outside the documented configuration.

## Complete setup on a clean PC

### Step 1: Install Git

1. Download Git for Windows from its official website.
2. Run the installer.
3. The default installer options are sufficient for this project.
4. Close every open PowerShell window after installation.
5. Open a new PowerShell window.
6. Verify the installation:

```powershell
git --version
```

A version number must be displayed. If PowerShell reports that `git` is not recognized, restart the terminal or Windows and try again.

### Step 2: Install Visual Studio and the C++ tools

1. Open Visual Studio Installer.
2. Install Visual Studio Community or a higher edition.
3. Open the **Workloads** tab.
4. Select **Desktop development with C++**.
5. In the workload details, confirm that these components are selected:
   - MSVC build tools for x64/x86.
   - Windows 10 SDK or Windows 11 SDK.
   - C++ CMake tools for Windows.
6. Start the installation.
7. Wait until every selected component has finished installing.
8. Restart Windows if the installer asks you to do so.

### Step 3: Download the project

Open PowerShell and choose a working directory:

```powershell
cd C:\Users\Public\Documents
git clone https://github.com/DiegoThunderfrosty/Der-Tondehr-Crunchy.git
cd Der-Tondehr-Crunchy
```

If you used GitHub's **Code -> Download ZIP** option instead:

1. Locate the downloaded ZIP file.
2. Extract the complete ZIP to a normal folder.
3. Do not run the scripts from inside the compressed ZIP view.
4. Open PowerShell in the extracted `Der-Tondehr-Crunchy` folder.
5. Continue with Step 4.

Using Git is recommended because it makes later updates easier.

### Step 4: Download the exact dependencies

Run this command from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup_dependencies.ps1
```

The script performs these operations:

1. Creates `external\iPlug2`.
2. Clones the official iPlug2 repository if it is missing.
3. Selects the exact framework revision required by this project.
4. Clones the required plugin-format SDK inside iPlug2.
5. Selects the exact SDK revision required by this project.
6. Initializes the SDK submodules.
7. Checks that the required files exist.

When it finishes, these files must exist:

```text
external\iPlug2\iPlug2.cmake
external\iPlug2\Dependencies\IPlug\VST3_SDK\CMakeLists.txt
```

Do not rename or move directories inside `external\iPlug2`. The `external` directory is excluded from Git because it contains separately licensed dependencies and their own repository data.

### Step 5: Build Release and run the tests

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

The script will:

1. Locate the local iPlug2 checkout.
2. Locate CMake from the system or Visual Studio installation.
3. Create `build\windows` if needed.
4. Configure a 64-bit CMake project.
5. Build the plugin and standalone application in Release mode.
6. Build the test executables.
7. Run the configured tests.
8. Stop and display an error if configuration, compilation, or testing fails.

The first build may take longer because CMake can prepare auxiliary dependencies.

To compile without building or running tests:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -SkipTests
```

To choose another build directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -BuildDirectory build\another-directory
```

Do not manually edit files under `build`; CMake can regenerate them.

### Step 6: Locate the build output

After a successful default build, the expected output is:

```text
build\windows\out\DerTondehrCrunchy.vst3\
build\windows\out\DerTondehrCrunchy.exe
```

`DerTondehrCrunchy.vst3` is a complete directory bundle even if Windows displays it as one item. Do not copy only the binary found inside its `Contents` directory.

### Step 7: Install the plugin

1. Close any audio host that may have an earlier build loaded.
2. Open File Explorer.
3. Open `build\windows\out`.
4. Copy the complete `DerTondehrCrunchy.vst3` folder.
5. Open:

```text
C:\Program Files\Common Files\VST3
```

6. Paste the complete folder there.
7. Windows may request administrator permission. Approve it only if you trust the build you created.
8. Open your audio host.
9. Run its plugin rescan or refresh command.
10. Search for `Der Tondehr Crunchy` in the effects list.
11. Insert it on a guitar or DI track.
12. Add a cabinet impulse-response loader after it if cabinet simulation is required.

If multiple builds share the same plugin identifier, a host may list only one or select one unpredictably. Keep backup builds outside every directory scanned by the host.

### Step 8: Run the standalone application

Open:

```text
build\windows\out\DerTondehrCrunchy.exe
```

Then:

1. Open `File -> Preferences`.
2. Select DirectSound, ASIO, or WASAPI.
3. Select an input device.
4. Select an output device.
5. Select the input channel pair.
6. Select the output channel pair.
7. Select the sample rate.
8. Select the buffer size.
9. Press `Apply` to try the settings.
10. Press `OK` to save them.
11. Use `MONO IN 1/2` to choose which channel from the selected input pair feeds the guitar path.
12. Begin with a low monitoring level and Bypass enabled.

A smaller buffer normally reduces monitoring latency but increases CPU pressure. Increase the buffer size if audio breaks up or drops out.

## Updating an existing checkout

From the repository root:

```powershell
git pull
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup_dependencies.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

The dependency script checks pinned revisions and stops when an existing directory is not a valid checkout. It does not silently replace unrelated local files.

## Building portable tests without iPlug2

The following commands configure only the tests that do not require the plugin framework:

```powershell
cmake -S . -B build-tests -DCRUNCHY_BUILD_PLUGIN=OFF -DCRUNCHY_BUILD_TESTS=ON
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

## Troubleshooting

### Git is not found

Reinstall Git for Windows, allow the installer to add Git to `PATH`, close the terminal, and open a new PowerShell window.

### CMake or the compiler is not found

Open Visual Studio Installer, select **Modify**, and confirm that the C++ desktop workload, MSVC, Windows SDK, and CMake tools are installed.

### iPlug2 or the SDK is missing

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup_dependencies.ps1
```

Do not create an empty directory with the expected name. The setup script checks real files and revisions.

### The plugin is not listed by the host

Confirm that the complete bundle was copied to `C:\Program Files\Common Files\VST3`. Confirm that the host scans that location, clear any failed-plugin entry, and run a complete rescan.

### The standalone application has no input

Open Preferences and verify the selected driver, input device, input pair, and `MONO IN 1/2` selection. Confirm that Windows has granted microphone/input permission to desktop applications.

### Compilation fails after changing dependency versions

The standalone integration patches selected wrapper source text during CMake configuration. It therefore requires the pinned iPlug2 revision. Use a fresh dependency directory, run `setup_dependencies.ps1`, remove only the affected generated build directory, and build again.

## Contributions

Read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting a change. Reports should include Windows version, compiler, sample rate, buffer size, target format, and complete reproduction steps. Do not attach audio, impulse responses, manuals, circuit drawings, or any other file that you are not permitted to redistribute.

## License

Original Der Tondehr Crunchy code is released under the [MIT License](LICENSE): Copyright (c) 2026 Diego Rodriguez.

Dependencies retain their own licenses and notices. See [THIRD_PARTY.md](THIRD_PARTY.md). The project license does not grant rights over names, marks, documentation, audio, or code owned by third parties.
