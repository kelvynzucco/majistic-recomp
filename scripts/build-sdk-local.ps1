$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = Join-Path $root 'tools\source\rexglue-sdk'
$patchRoot = Join-Path $root 'patches\rexglue-0.10.0'
$installPrefix = ($root -replace '\\', '/') + '/tools/sdk/win-amd64'

function Check-Native($step) {
    if ($LASTEXITCODE -ne 0) {
        throw "$step failed (exit $LASTEXITCODE)."
    }
}

. (Join-Path $PSScriptRoot 'vs-env.ps1')

Push-Location $source
try {
    New-Item -ItemType Directory -Force out | Out-Null

    Write-Output '[1/5] Compiling and running XMA audio recovery unit tests...'
    clang++ -std=c++20 src/audio/empty_packet_recovery_test.cpp -o out/empty_packet_recovery_test.exe
    Check-Native 'XMA test compilation'
    & ./out/empty_packet_recovery_test.exe
    Check-Native 'XMA boundary tests'

    Write-Output '[2/5] Compiling and running GPU log filter unit tests...'
    clang++ -std=c++20 "-I$source/thirdparty/spdlog/include" "-I$source/thirdparty/fmt/include" (Join-Path $patchRoot 'texture_warning_sink_test.cpp') -o out/texture_warning_sink_test.exe
    Check-Native 'Texture logging test compilation'
    & ./out/texture_warning_sink_test.exe
    Check-Native 'Texture logging tests'

    Write-Output '[3/5] Configuring CMake for rexglue-sdk (win-amd64)...'
    cmake --preset win-amd64 -DREXGLUE_USE_VULKAN=ON -DREXGLUE_BUILD_TESTS=OFF
    Check-Native 'SDK configuration'

    Write-Output '[4/5] Building full rexglue-sdk (Release)...'
    cmake --build out/build/win-amd64 --config Release --parallel 4
    Check-Native 'SDK Release build'

    Write-Output '[5/5] Installing SDK to tools/sdk/win-amd64...'
    cmake --install out/build/win-amd64 --config Release --prefix $installPrefix
    Check-Native 'SDK install'
} finally {
    Pop-Location
}

$rexglueExe = Join-Path $root 'tools\sdk\win-amd64\bin\rexglue.exe'
$rexRuntimeDll = Join-Path $root 'tools\sdk\win-amd64\bin\rexruntime.dll'
$rexGpuDll = Join-Path $root 'tools\sdk\win-amd64\bin\rexgpu-xenos.dll'

if (!(Test-Path $rexglueExe)) { throw "Missing $rexglueExe" }
if (!(Test-Path $rexRuntimeDll)) { throw "Missing $rexRuntimeDll" }
if (!(Test-Path $rexGpuDll)) { throw "Missing $rexGpuDll" }

Write-Output "Verifying installed rexglue.exe version:"
& $rexglueExe --version
Check-Native 'rexglue --version'
Write-Output 'SUCCESS: Local ReXGlue SDK built, tested, and installed!'
