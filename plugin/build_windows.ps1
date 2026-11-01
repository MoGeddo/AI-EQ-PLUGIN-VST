# Builds the VST3 on Windows (Visual Studio 2022 + CMake 3.22+).
#   .\build_windows.ps1
# Output: build\AEQRestore_artefacts\Release\VST3\AEQ Restore.vst3
$ErrorActionPreference = "Stop"
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
Write-Host ""
Write-Host "Built. Copy the .vst3 folder to C:\Program Files\Common Files\VST3"
Write-Host "Smoke test first:  .\build\Release\aeq_tests.exe"
