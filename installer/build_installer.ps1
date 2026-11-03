# build_installer.ps1 - builds the plugin and wraps it in a normal Windows
# installer: dist\AEQ_Restore_Setup_<version>.exe
#
#   .\build_installer.ps1                    build the plugin, then the installer
#   .\build_installer.ps1 -SkipBuild         reuse the existing plugin build
#   .\build_installer.ps1 -Version 1.0.1     stamp a different version
#   .\build_installer.ps1 -Sign              also sign, if a certificate is set up
#
# Needs: everything build_windows.ps1 needs, plus Inno Setup 6.3+
#        winget install JRSoftware.InnoSetup

param(
    [string]$Version = "1.2.4",
    [switch]$SkipBuild,
    [switch]$Sign
)
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$root = Split-Path $here -Parent
$staging = Join-Path $here "staging"
$dist = Join-Path $root "dist"

function Fail($msg) { Write-Host ""; Write-Host "FAILED: $msg" -ForegroundColor Red; exit 1 }
function Step($msg) { Write-Host ""; Write-Host "== $msg ==" -ForegroundColor Cyan }

# ---------------------------------------------------------------- 1. the plugin
if ($Version -notmatch '^\d+\.\d+\.\d+$') { Fail "version must look like 1.0.0 (got '$Version')" }

if (-not $SkipBuild) {
    Step "building the plugin"
    & (Join-Path $root "plugin\build_windows.ps1")
    if ($LASTEXITCODE -ne 0) { Fail "the plugin build failed - fixing that comes before any installer" }
} else {
    Write-Host "== reusing the existing plugin build ==" -ForegroundColor Cyan
}

$build = Join-Path $root "plugin\build"
$vst3 = Get-ChildItem -Path $build -Recurse -Directory -Filter "AEQ Restore.vst3" -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName "Contents\x86_64-win\AEQ Restore.vst3") } |
        Select-Object -First 1
if (-not $vst3) { $vst3 = Get-ChildItem -Path $build -Recurse -Directory -Filter "AEQ Restore.vst3" -ErrorAction SilentlyContinue | Select-Object -First 1 }
if (-not $vst3) { Fail "no AEQ Restore.vst3 in $build - build the plugin first (drop -SkipBuild)" }

$exe = Get-ChildItem -Path $build -Recurse -File -Filter "AEQ Restore.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $exe) { Fail "no AEQ Restore.exe (standalone) in $build - the same CMake target builds it, so this means the build is incomplete" }

Write-Host "   vst3      : $($vst3.FullName)"
Write-Host "   standalone: $($exe.FullName)"

# ------------------------------------------------------------------ 2. staging
Step "staging"
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force -Path (Join-Path $staging "vst3") | Out-Null
Copy-Item -Recurse -Force $vst3.FullName (Join-Path $staging "vst3")
New-Item -ItemType Directory -Force -Path (Join-Path $staging "standalone") | Out-Null
Copy-Item -Force $exe.FullName (Join-Path $staging "standalone")
$stagedFiles = Get-ChildItem -Path $staging -Recurse -File
Write-Host "   $($stagedFiles.Count) files, $([math]::Round(($stagedFiles | Measure-Object Length -Sum).Sum / 1MB, 1)) MB"

$innerDll = Join-Path $staging "vst3\AEQ Restore.vst3\Contents\x86_64-win\AEQ Restore.vst3"
if (-not (Test-Path $innerDll)) {
    Write-Host "   warning: the bundle does not look like a Windows VST3 (no Contents\x86_64-win\AEQ Restore.vst3)" -ForegroundColor Yellow
}

# ------------------------------------------------------------------ 3. if any
# change to the plugin means this must be re-signed, say so once rather than
# failing silently on SmartScreen later
if (-not $Sign) {
    Write-Host "   unsigned installer: Windows SmartScreen will ask 'More info > Run anyway' on other machines" -ForegroundColor Yellow
}

# ------------------------------------------------------------------ 4. Inno
Step "finding Inno Setup"
$iscc = $null
foreach ($c in @(
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"))) {
    if ($c -and (Test-Path $c)) { $iscc = $c; break }
}
if (-not $iscc) {
    $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($cmd) { $iscc = $cmd.Source }
}
if (-not $iscc) {
    Fail "Inno Setup not found. Install it with:  winget install JRSoftware.InnoSetup   then reopen the terminal."
}
# ISCC does not always carry a usable file version (the Chocolatey build reports
# 0.0.0.0), so try the registry as well and treat "unknown" as "let ISCC speak".
$isccVersion = ""
try { $isccVersion = (Get-Item $iscc).VersionInfo.FileVersion } catch { }
if (-not $isccVersion -or $isccVersion -like "0.0.0*") {
    foreach ($k in @(
            "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1",
            "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1")) {
        try {
            $v = (Get-ItemProperty -Path $k -ErrorAction Stop).DisplayVersion
            if ($v) { $isccVersion = $v; break }
        } catch { }
    }
}
if (-not $isccVersion -or $isccVersion -like "0.0.0*") {
    Write-Host "   $iscc  (version not readable - continuing; 6.3+ is required)" -ForegroundColor Yellow
} else {
    Write-Host "   $iscc  (v$isccVersion)"
    $verParts = $isccVersion -split '\.'
    $majorOk = $false
    if ($verParts.Count -ge 2) {
        $mj = 0; $mn = 0
        [int]::TryParse($verParts[0], [ref]$mj) | Out-Null
        [int]::TryParse($verParts[1], [ref]$mn) | Out-Null
        $majorOk = ($mj -gt 6) -or ($mj -eq 6 -and $mn -ge 3)
    }
    if (-not $majorOk) {
        Fail "Inno Setup 6.3+ required (this one is $isccVersion) - the script uses ArchitecturesAllowed=x64compatible"
    }
}

# ------------------------------------------------------------------ 5. compile
Step "compiling the installer"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$iss = Join-Path $here "AEQ_Restore.iss"
& $iscc "/DAppVersion=$Version" $iss
if ($LASTEXITCODE -ne 0) { Fail "ISCC failed - the error above names the line in AEQ_Restore.iss" }

$setup = Join-Path $dist "AEQ_Restore_Setup_$Version.exe"
if (-not (Test-Path $setup)) { Fail "ISCC reported success but $setup is missing" }

if ($Sign) {
    Step "signing"
    $signtool = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if (-not $signtool) { Fail "-Sign was given but signtool.exe is not on PATH (install the Windows SDK)" }
    & $signtool.Source sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $setup
    if ($LASTEXITCODE -ne 0) { Fail "signing failed" }
}

# ------------------------------------------------------------------ 6. report
$hash = (Get-FileHash -Algorithm SHA256 $setup).Hash
$size = [math]::Round((Get-Item $setup).Length / 1MB, 1)

Step "done"
Write-Host "installer : $setup" -ForegroundColor Green
Write-Host "version   : $Version"
Write-Host "size      : $size MB"
Write-Host "sha256    : $hash"
Set-Content -Path (Join-Path $dist "AEQ_Restore_Setup_$Version.sha256") -Value "$hash  AEQ_Restore_Setup_$Version.exe"
Write-Host ""
Write-Host "Double-click it to test the whole flow on this machine. Uninstall from" -ForegroundColor Yellow
Write-Host "Settings > Apps, then reinstall - an upgrade over an existing install must" -ForegroundColor Yellow
Write-Host "replace it, not stack a second copy." -ForegroundColor Yellow
