param(
  [string]$Destination
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Destination) {
  $Destination = Join-Path $projectRoot 'external\iPlug2'
}

$iPlugRevision = 'd54f69050f517e43b941d88c2a170f0a840b9ee4'
$vstRevision = '3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96'
$iPlugRepository = 'https://github.com/iPlug2/iPlug2.git'
$vstRepository = 'https://github.com/steinbergmedia/vst3sdk.git'

$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
  throw 'Git was not found. Install Git for Windows, open a new PowerShell window, and run this script again.'
}

$destinationParent = Split-Path -Parent $Destination
New-Item -ItemType Directory -Force -Path $destinationParent | Out-Null

if (-not (Test-Path -LiteralPath $Destination)) {
  & $git.Source clone $iPlugRepository $Destination
  if ($LASTEXITCODE -ne 0) { throw 'Could not clone iPlug2.' }
}

if (-not (Test-Path -LiteralPath (Join-Path $Destination '.git'))) {
  throw "The destination exists but is not an iPlug2 Git checkout: $Destination"
}

& $git.Source -C $Destination fetch --depth 1 origin $iPlugRevision
if ($LASTEXITCODE -ne 0) { throw 'Could not fetch the required iPlug2 revision.' }
& $git.Source -C $Destination checkout --detach $iPlugRevision
if ($LASTEXITCODE -ne 0) { throw 'Could not select the required iPlug2 revision.' }

$vstPath = Join-Path $Destination 'Dependencies\IPlug\VST3_SDK'
if (-not (Test-Path -LiteralPath $vstPath)) {
  & $git.Source clone --recursive $vstRepository $vstPath
  if ($LASTEXITCODE -ne 0) { throw 'Could not clone the VST3 SDK.' }
}

if (-not (Test-Path -LiteralPath (Join-Path $vstPath '.git'))) {
  throw "The SDK destination exists but is not a Git checkout: $vstPath"
}

& $git.Source -C $vstPath fetch --depth 1 origin $vstRevision
if ($LASTEXITCODE -ne 0) { throw 'Could not fetch the required SDK revision.' }
& $git.Source -C $vstPath checkout --detach $vstRevision
if ($LASTEXITCODE -ne 0) { throw 'Could not select the required SDK revision.' }
& $git.Source -C $vstPath submodule update --init --recursive --depth 1
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the SDK submodules.' }

$requiredFiles = @(
  (Join-Path $Destination 'iPlug2.cmake'),
  (Join-Path $vstPath 'CMakeLists.txt'),
  (Join-Path $vstPath 'pluginterfaces\base\funknown.h'),
  (Join-Path $vstPath 'public.sdk\source\vst\vstsinglecomponenteffect.cpp')
)
foreach ($file in $requiredFiles) {
  if (-not (Test-Path -LiteralPath $file)) { throw "Required dependency file is missing: $file" }
}

Write-Host "Dependencies are ready: $Destination"
Write-Host "iPlug2 revision: $iPlugRevision"
Write-Host "SDK revision: $vstRevision"

