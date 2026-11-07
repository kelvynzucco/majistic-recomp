param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'vs-env.ps1')

$sdk = ($projectRoot -replace '\\', '/') + '/tools/sdk/win-amd64'
$preset = 'win-amd64-' + $Configuration.ToLower()

if (Get-Process majin -ErrorAction SilentlyContinue) {
    throw 'Close majin.exe before rebuilding.'
}

Push-Location (Join-Path $projectRoot 'prototype-usa')
try {
    Write-Output "[1/3] Running rexglue.exe codegen majin_manifest.toml..."
    & "$sdk/bin/rexglue.exe" --log-file "../reports/codegen-usa.log" codegen 'majin_manifest.toml'
    if ($LASTEXITCODE -ne 0) { throw "Codegen validation failed (exit $LASTEXITCODE)." }

    Write-Output "[2/3] Configuring CMake for prototype-usa ($preset)..."
    cmake --fresh --preset $preset "-DCMAKE_PREFIX_PATH=$sdk" -DMAJIN_1080P_STARTUP_PROBE=OFF
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)." }

    Write-Output "[3/3] Compiling majin.exe ($preset)..."
    cmake --build --preset $preset --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }
} finally {
    Pop-Location
}

$suffix = if ($Configuration -eq 'Debug') { 'd' } else { '' }
$runtime = Join-Path $projectRoot "tools\source\rexglue-sdk\out\win-amd64\$Configuration\rexruntime$suffix.dll"
$target = Join-Path $projectRoot "prototype-usa\out\build\$preset\rexruntime$suffix.dll"
Copy-Item -LiteralPath $runtime -Destination $target -Force
if ((Get-FileHash $runtime).Hash -ne (Get-FileHash $target).Hash) {
    throw 'Patched runtime verification failed.'
}

$gpu = Join-Path $projectRoot "tools\source\rexglue-sdk\out\win-amd64\$Configuration\rexgpu-xenos$suffix.dll"
$gpuTarget = Join-Path $projectRoot "prototype-usa\out\build\$preset\rexgpu-xenos$suffix.dll"
Copy-Item -LiteralPath $gpu -Destination $gpuTarget -Force
if ((Get-FileHash $gpu).Hash -ne (Get-FileHash $gpuTarget).Hash) {
    throw 'Patched GPU verification failed.'
}

Write-Output "SUCCESS: majin.exe ($Configuration) compiled and patched DLLs installed!"
