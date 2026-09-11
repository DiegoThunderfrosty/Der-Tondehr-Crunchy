param(
  [string]$IPlug2Dir,
  [string]$Configuration = 'Release',
  [switch]$TestsOnly,
  [switch]$SkipTests,
  [string]$BuildDirectory = 'build\windows'
)

$ErrorActionPreference = 'Stop'
if ($TestsOnly -and $SkipTests) { throw 'TestsOnly and SkipTests cannot be combined.' }
$projectRoot = Split-Path -Parent $PSScriptRoot

if (-not $IPlug2Dir) {
  $IPlug2Dir = Join-Path $projectRoot 'external\iPlug2'
}
if (-not (Test-Path -LiteralPath (Join-Path $IPlug2Dir 'iPlug2.cmake'))) {
  throw 'iPlug2 was not found. Run scripts\setup_dependencies.ps1 first or provide -IPlug2Dir.'
}
if (-not (Test-Path -LiteralPath (Join-Path $IPlug2Dir 'Dependencies\IPlug\VST3_SDK\CMakeLists.txt'))) {
  throw 'The required SDK was not found inside iPlug2. Run scripts\setup_dependencies.ps1.'
}
$IPlug2Dir = (Resolve-Path -LiteralPath $IPlug2Dir).Path

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
  $cmakePath = $cmakeCommand.Source
} else {
  $cmakePath = Get-ChildItem -LiteralPath 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\CMake\\bin\\cmake.exe$' } |
    Select-Object -First 1 -ExpandProperty FullName
}
if (-not $cmakePath) {
  throw 'CMake was not found. Install Visual Studio with Desktop development with C++ and CMake tools.'
}

if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
  $buildDir = $BuildDirectory
} else {
  $buildDir = Join-Path $projectRoot $BuildDirectory
}

$pluginFlag = if ($TestsOnly) { 'OFF' } else { 'ON' }
$testFlag = if ($SkipTests) { 'OFF' } else { 'ON' }

& $cmakePath -S $projectRoot -B $buildDir -A x64 `
  "-DIPLUG2_DIR=$IPlug2Dir" `
  "-DCRUNCHY_BUILD_PLUGIN=$pluginFlag" `
  "-DCRUNCHY_BUILD_TESTS=$testFlag" `
  '-DIPLUG_DEPLOY_PLUGINS=OFF'
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

& $cmakePath --build $buildDir --config $Configuration --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

if (-not $SkipTests) {
  $ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
  if (-not (Test-Path -LiteralPath $ctestPath)) {
    $ctestCommand = Get-Command ctest -ErrorAction SilentlyContinue
    if (-not $ctestCommand) { throw 'CTest was not found beside CMake or in PATH.' }
    $ctestPath = $ctestCommand.Source
  }
  & $ctestPath --test-dir $buildDir -C $Configuration --output-on-failure
  if ($LASTEXITCODE -ne 0) { throw 'One or more checks failed.' }
}

if ($TestsOnly) {
  Write-Host "Tests build is ready: $buildDir"
} else {
  Write-Host "Build is ready: $buildDir\out"
}

