$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$spikeDir = Join-Path $root 'tools\gpl-spike'
$buildDir = Join-Path $spikeDir 'build'

. (Join-Path $PSScriptRoot 'vs-env.ps1')

Write-Output "Configuring CMake for gpl_spike in $spikeDir..."
cmake -B $buildDir -S $spikeDir -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

Write-Output "Building gpl_spike (Release)..."
cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

Copy-Item (Join-Path $root 'tools\sdk\win-amd64\bin\*.dll') $buildDir -Force

Write-Output "gpl_spike built successfully: $buildDir\gpl_spike.exe"
