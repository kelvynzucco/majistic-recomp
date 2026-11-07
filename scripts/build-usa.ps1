param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$GameDataRoot = 'game-data'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'vs-env.ps1')

$sdk = ($projectRoot -replace '\\', '/') + '/tools/sdk/win-amd64'
$preset = 'win-amd64-' + $Configuration.ToLower()

if (Get-Process majin -ErrorAction SilentlyContinue) {
    throw 'Close majin.exe before rebuilding.'
}

# Resolve game data directory
if (-not [System.IO.Path]::IsPathRooted($GameDataRoot)) {
    $candidate = Join-Path $projectRoot $GameDataRoot
    if (-not (Test-Path $candidate) -and (Test-Path (Join-Path $projectRoot 'game-data'))) {
        $candidate = Join-Path $projectRoot 'game-data'
    } elseif (-not (Test-Path $candidate) -and (Test-Path (Join-Path $projectRoot 'game-data-usa'))) {
        $candidate = Join-Path $projectRoot 'game-data-usa'
    }
    $GameDataRoot = [System.IO.Path]::GetFullPath($candidate)
}

$xexPath = Join-Path $GameDataRoot 'default.xex'
if (-not (Test-Path $xexPath)) {
    throw "Entrypoint XEX not found at $xexPath. Please check your game-data directory."
}

# Detect edition from default.xex (USA = 76FDB642, PAL = 67BD87EF)
$isPal = $false
try {
    $headerBytes = [System.IO.File]::ReadAllBytes($xexPath)
    $maxScan = [Math]::Min($headerBytes.Length, 16384)
    for ($i = 0; $i -lt ($maxScan - 4); $i++) {
        if ($headerBytes[$i] -eq 0x67 -and $headerBytes[$i+1] -eq 0xBD -and $headerBytes[$i+2] -eq 0x87 -and $headerBytes[$i+3] -eq 0xEF) {
            $isPal = $true
            break
        }
    }
} catch {
    $isPal = $false
}

$protoFolder = if ($isPal) { 'prototype' } else { 'prototype-usa' }
$editionName = if ($isPal) { 'Europe / PAL (0.0.0.3)' } else { 'USA / NTSC-U (0.0.0.2)' }
Write-Output "Detected Majin and the Forsaken Kingdom: $editionName (using $protoFolder)"

# Dynamically sync majin_manifest.toml with actual GameDataRoot
$protoPath = Join-Path $projectRoot $protoFolder
$relGameData = [System.IO.Path]::GetRelativePath($protoPath, $GameDataRoot).Replace('\', '/')
$relXex = "$relGameData/default.xex"

$manifestPath = Join-Path $protoPath 'majin_manifest.toml'
if (Test-Path $manifestPath) {
    $manifestContent = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8
    $updatedManifest = $manifestContent -replace '(?m)^game_root\s*=.*$', "game_root = `"$relGameData`""
    $updatedManifest = $updatedManifest -replace '(?m)^file_path\s*=.*$', "file_path = `"$relXex`""
    if ($updatedManifest -ne $manifestContent) {
        Set-Content -LiteralPath $manifestPath -Value $updatedManifest -Encoding UTF8
        Write-Output "Updated $manifestPath with game_root = '$relGameData'"
    }
}

Push-Location $protoPath
try {
    Write-Output "[1/3] Running rexglue.exe codegen majin_manifest.toml ($protoFolder)..."
    & "$sdk/bin/rexglue.exe" --log-file "../reports/codegen-$protoFolder.log" codegen 'majin_manifest.toml'
    if ($LASTEXITCODE -ne 0) { throw "Codegen validation failed (exit $LASTEXITCODE)." }

    Write-Output "[2/3] Configuring CMake for $protoFolder ($preset)..."
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
if (-not (Test-Path $runtime)) {
    $runtime = Join-Path $projectRoot "tools\sdk\win-amd64\bin\rexruntime$suffix.dll"
}
$target = Join-Path $protoPath "out\build\$preset\rexruntime$suffix.dll"
Copy-Item -LiteralPath $runtime -Destination $target -Force

$gpu = Join-Path $projectRoot "tools\source\rexglue-sdk\out\win-amd64\$Configuration\rexgpu-xenos$suffix.dll"
if (-not (Test-Path $gpu)) {
    $gpu = Join-Path $projectRoot "tools\sdk\win-amd64\bin\rexgpu-xenos$suffix.dll"
}
$gpuTarget = Join-Path $protoPath "out\build\$preset\rexgpu-xenos$suffix.dll"
Copy-Item -LiteralPath $gpu -Destination $gpuTarget -Force

# Deploy executable and runtime DLLs into unified game-data/bin
$gameDataBin = Join-Path $GameDataRoot "bin"
New-Item -ItemType Directory -Force -Path $gameDataBin | Out-Null
$builtExe = Join-Path $protoPath "out\build\$preset\majin.exe"
if (Test-Path $builtExe) {
    Copy-Item $builtExe -Destination (Join-Path $gameDataBin "majin.exe") -Force
    Copy-Item $target -Destination $gameDataBin -Force
    Copy-Item $gpuTarget -Destination $gameDataBin -Force
    Write-Output "Copied majin.exe and patched DLLs to $gameDataBin"
}

Write-Output "SUCCESS: majin.exe ($Configuration) compiled and installed into $gameDataBin!"
