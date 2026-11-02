# build_windows.ps1 - builds AEQ Restore as a VST3 (and a standalone) on Windows.
#
#   .\build_windows.ps1              build + run the acceptance gates
#   .\build_windows.ps1 -Install     ... and copy the plugin into the VST3 folder
#   .\build_windows.ps1 -Clean       wipe the build folder first
#
# Needs: Visual Studio 2022 (Desktop development with C++), CMake 3.22+, Git.
# The first run downloads JUCE 8.0.4 (~250 MB) from github.com - that is the only
# step that needs the network.  Pass -JucePath <dir> to use a JUCE checkout
# instead and build offline.
param(
    [switch]$Install,
    [switch]$Clean,
    [string]$JucePath = ""
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

function Fail($msg) { Write-Host ""; Write-Host "FAILED: $msg" -ForegroundColor Red; exit 1 }

# ---------------------------------------------------------------- prerequisites
Write-Host "== checking prerequisites ==" -ForegroundColor Cyan

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) { Fail "cmake not found. Install CMake 3.22+ (winget install Kitware.CMake) and reopen the terminal." }
$cmakeVersion = (& cmake --version | Select-Object -First 1)
if ($cmakeVersion -notmatch "(\d+)\.(\d+)") { Fail "cannot read the cmake version" }
if ([int]$Matches[1] -lt 3 -or ([int]$Matches[1] -eq 3 -and [int]$Matches[2] -lt 22)) {
    Fail "cmake 3.22+ required, found $cmakeVersion"
}
Write-Host "   $cmakeVersion"

if (-not $JucePath) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Fail "git not found - JUCE is fetched with git. Install Git, or pass -JucePath <a JUCE 8 checkout>."
    }
    Write-Host "   git ok (JUCE will be fetched on the first configure)"
} else {
    if (-not (Test-Path (Join-Path $JucePath "CMakeLists.txt"))) { Fail "-JucePath does not point at a JUCE checkout" }
    Write-Host "   using JUCE at $JucePath"
}

# vswhere ships with every Visual Studio installer / Build Tools.  The generator
# name has to match the installed Visual Studio, and "17 2022" is not a safe
# assumption any more: a machine with 2026 installed refuses to configure.
$vsGenerator = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName
    if (-not $vs) { Fail "Visual Studio found but not the C++ workload. Re-run the installer and tick 'Desktop development with C++'." }
    $vsVersion = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
    Write-Host "   $vs ($vsVersion)"
    $major = 0
    if ($vsVersion -match '^(\d+)\.') { $major = [int]$Matches[1] }
    switch ($major) {
        16 { $vsGenerator = "Visual Studio 16 2019" }
        17 { $vsGenerator = "Visual Studio 17 2022" }
        18 { $vsGenerator = "Visual Studio 18 2026" }
    }
    if (-not $vsGenerator) {
        Write-Host "   unknown Visual Studio major version - leaving the generator to CMake" -ForegroundColor Yellow
    }
} else {
    Write-Host "   vswhere not found - letting CMake pick the default generator" -ForegroundColor Yellow
}

# --------------------------------------------------------------------- configure
$buildDir = Join-Path $root "build"
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "== cleaning $buildDir ==" -ForegroundColor Cyan
    Remove-Item -Recurse -Force $buildDir
}

Write-Host "== configure ==" -ForegroundColor Cyan
if ($vsGenerator) {
    $cfgArgs = @("-B", $buildDir, "-S", $root, "-G", $vsGenerator, "-A", "x64")
} else {
    $cfgArgs = @("-B", $buildDir, "-S", $root)
}
if ($JucePath) { $cfgArgs += "-DJUCE_PATH=$JucePath" }
& cmake @cfgArgs
if ($LASTEXITCODE -ne 0) {
    Fail ("configure failed (scroll up: the usual causes are no network for the JUCE fetch, " +
          "a missing C++ workload, or a build folder made by a different generator - " +
          "in that last case re-run with -Clean)")
}

# ------------------------------------------------------------------------- build
Write-Host "== build (Release) ==" -ForegroundColor Cyan
& cmake --build $buildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { Fail "build failed" }

# ------------------------------------------------------------------- gates first
Write-Host "== acceptance gates ==" -ForegroundColor Cyan
$tests = Get-ChildItem -Path $buildDir -Recurse -Filter "aeq_tests.exe" | Select-Object -First 1
if (-not $tests) { Fail "aeq_tests.exe was not built" }
# Tee, not a plain call: if the gate binary ever dies mid-run the buffered output is
# still shown, instead of "the gates did not pass" with no evidence (that happened).
& $tests.FullName 2>&1 | Tee-Object -Variable gateOut | Write-Host
$code = $LASTEXITCODE
if ($code -ne 0) {
    $last = ($gateOut | Select-Object -Last 12) -join "`n"
    Fail "the DSP gates did not pass (exit $code) - do not load this build in a DAW.`nLast lines:`n$last"
}

# --------------------------------------------------------------- UI screenshot
# The claim "the interface renders" has to come from the interface, not from a
# drawing of it.  aeq_shot builds the real editor, feeds it a synthetic take and
# snapshots the component tree, using the same Windows font stack that broke in
# 1.2.0.  A missing shot is not a build failure: the exe is the deliverable.
Write-Host "== UI screenshot ==" -ForegroundColor Cyan
$shot = Get-ChildItem -Path $buildDir -Recurse -Filter "aeq_shot.exe" | Select-Object -First 1
if ($shot) {
    $shotDir = Join-Path $root "dist\ui"
    New-Item -ItemType Directory -Force -Path $shotDir | Out-Null
    & $shot.FullName $shotDir 2>&1 | Write-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Host "   aeq_shot exited $LASTEXITCODE - the screenshot is missing" -ForegroundColor Yellow
    }
} else {
    Write-Host "   aeq_shot.exe was not built - the screenshot is missing" -ForegroundColor Yellow
}

# ----------------------------------------------------------- thread contract
# TSAN is a GCC/Clang feature; on MSVC this binary is a plain run.  It still
# catches the class of bug that crashed FL Studio (a lifetime violation across
# threads) when it is deterministic.
Write-Host "== thread contract ==" -ForegroundColor Cyan
$thr = Get-ChildItem -Path $buildDir -Recurse -Filter "aeq_threads.exe" | Select-Object -First 1
if ($thr) {
    & $thr.FullName 2>&1 | Write-Host
    if ($LASTEXITCODE -ne 0) { Fail "the audio/editor thread contract failed" }
} else {
    Write-Host "   aeq_threads.exe was not built" -ForegroundColor Yellow
}

# ----------------------------------------------------------------------- output
$vst3 = Get-ChildItem -Path $buildDir -Recurse -Filter "AEQ Restore.vst3" | Select-Object -First 1
$exe  = Get-ChildItem -Path $buildDir -Recurse -Filter "AEQ Restore.exe" | Select-Object -First 1

Write-Host ""
if ($vst3) {
    Write-Host "VST3      : $($vst3.FullName)" -ForegroundColor Green
} else {
    Fail "no .vst3 was produced - check the build log for the VST3 target"
}
if ($exe) { Write-Host "Standalone: $($exe.FullName)" -ForegroundColor Green }

if ($Install) {
    $dest = Join-Path $env:ProgramFiles "Common Files\VST3"
    if (-not (Test-Path $dest)) { Fail "VST3 folder not found: $dest" }
    Write-Host "== installing to $dest ==" -ForegroundColor Cyan
    Copy-Item -Recurse -Force $vst3.FullName $dest
    Write-Host "installed: $(Join-Path $dest 'AEQ Restore.vst3')" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "To install, either re-run with -Install, or copy that .vst3 folder to:" -ForegroundColor Yellow
    Write-Host "  C:\Program Files\Common Files\VST3"
}

Write-Host ""
Write-Host "Next: validate it with pluginval (strictness 5, then 10) before mixing:" -ForegroundColor Yellow
Write-Host '  pluginval.exe --strictness-level 5 --validate "C:\Program Files\Common Files\VST3\AEQ Restore.vst3"'
