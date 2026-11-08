<#
.SYNOPSIS
    Assembles a complete, self-contained, clean-room release package for Majistic Recomp.
.PARAMETER Version
    The release version tag (defaults to 0.1.0).
.PARAMETER CreateZip
    Whether to create a compressed .zip archive of the release package.
#>
param(
    [string]$Version = "0.1.0",
    [switch]$CreateZip = $true
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$releaseDir = Join-Path $root "release-v$Version"
$zipPath = Join-Path $root "Majistic-Recomp-v$Version-Windows-x64.zip"

Write-Host "============================================================"
Write-Host "Creating Majistic Recomp Release Package: v$Version"
Write-Host "Destination: $releaseDir"
Write-Host "============================================================"

# Ensure release dir is fresh
if (Test-Path $releaseDir) {
    Write-Host "Removing previous release folder: $releaseDir..."
    Remove-Item -Path $releaseDir -Recurse -Force
}
New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null

# 1. Copy Majistic Launcher.exe
$launcherExe = Join-Path $root 'Majistic Launcher.exe'
if (-not (Test-Path $launcherExe)) {
    throw "Launcher executable not found at $launcherExe. Please build it first."
}
Copy-Item $launcherExe (Join-Path $releaseDir 'Majistic Launcher.exe') -Force
Write-Host "[1/7] Copied Majistic Launcher.exe"

# 2. Copy scripts
$destScripts = Join-Path $releaseDir 'scripts'
New-Item -ItemType Directory -Path $destScripts -Force | Out-Null
$scriptFiles = @(
    'build-launcher-usa.ps1',
    'build-usa.ps1',
    'diagnostics-common.ps1',
    'extract-iso.ps1',
    'generate-icons.ps1',
    'run-usa.ps1',
    'unpack-xex-usa.ps1',
    'vs-env.ps1'
)
foreach ($s in $scriptFiles) {
    $src = Join-Path $root "scripts\$s"
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $destScripts $s) -Force
    }
}
Write-Host "[2/7] Copied core scripts"

# 3. Copy tools/sdk/win-amd64 (precompiled SDK)
$destSdk = Join-Path $releaseDir 'tools\sdk\win-amd64'
New-Item -ItemType Directory -Path $destSdk -Force | Out-Null
$srcSdk = Join-Path $root 'tools\sdk\win-amd64'
Copy-Item "$srcSdk\*" $destSdk -Recurse -Force
Write-Host "[3/7] Copied tools/sdk/win-amd64"

# 4. Copy prototype-usa (excluding build cache 'out')
$destProto = Join-Path $releaseDir 'prototype-usa'
New-Item -ItemType Directory -Path $destProto -Force | Out-Null
$srcProto = Join-Path $root 'prototype-usa'
Get-ChildItem -Path $srcProto -Exclude 'out' | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $destProto $_.Name) -Recurse -Force
}
Write-Host "[4/7] Copied prototype-usa (source & manifest)"

# 4b. Copy prototype (PAL, excluding build cache 'out')
$destPalProto = Join-Path $releaseDir 'prototype'
New-Item -ItemType Directory -Path $destPalProto -Force | Out-Null
$srcPalProto = Join-Path $root 'prototype'
Get-ChildItem -Path $srcPalProto -Exclude 'out' | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $destPalProto $_.Name) -Recurse -Force
}
Write-Host "[4b/7] Copied prototype (PAL source & manifest)"

# 4c. Copy shared unified source
$destSrc = Join-Path $releaseDir 'src'
New-Item -ItemType Directory -Path $destSrc -Force | Out-Null
Copy-Item (Join-Path $root 'src\*') $destSrc -Recurse -Force
Write-Host "[4c/7] Copied shared unified source (src/)"

# 5. Copy branding & licenses
$destBranding = Join-Path $releaseDir 'branding'
New-Item -ItemType Directory -Path $destBranding -Force | Out-Null
Copy-Item (Join-Path $root 'branding\majistic_logo.png') (Join-Path $destBranding 'majistic_logo.png') -Force
Copy-Item (Join-Path $root 'branding\majistic_icon.png') (Join-Path $destBranding 'majistic_icon.png') -Force

$destLicenses = Join-Path $releaseDir 'LICENSES'
New-Item -ItemType Directory -Path $destLicenses -Force | Out-Null
Copy-Item (Join-Path $root 'LICENSES\*') $destLicenses -Recurse -Force
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $releaseDir 'LICENSE') -Force
Copy-Item (Join-Path $root 'README.md') (Join-Path $releaseDir 'README.md') -Force
Write-Host "[5/7] Copied branding, LICENSE, and README.md"

# 6. Deploy precompiled game binary, runtime DLLs, and game-data template
$destGameData = Join-Path $releaseDir 'game-data'
$destBin = Join-Path $destGameData 'bin'
$destUnpacked = Join-Path $destGameData 'unpacked'
New-Item -ItemType Directory -Path $destGameData, $destBin, $destUnpacked -Force | Out-Null

$srcBin = Join-Path $root 'game-data\bin'
$srcPalExe = Join-Path $root 'prototype\out\build\win-amd64-release\majin.exe'
$srcUsaExe = Join-Path $root 'prototype-usa\out\build\win-amd64-release\majin.exe'

$exeSource = if (Test-Path (Join-Path $srcBin 'majin.exe')) {
    Join-Path $srcBin 'majin.exe'
} elseif (Test-Path $srcPalExe) {
    $srcPalExe
} elseif (Test-Path $srcUsaExe) {
    $srcUsaExe
} else {
    $null
}

if ($exeSource) {
    Copy-Item $exeSource (Join-Path $destBin 'majin.exe') -Force
    $exeSize = (Get-Item (Join-Path $destBin 'majin.exe')).Length / 1MB
    Write-Host ("[6/7] Copied precompiled majin.exe ({0:N1} MB)" -f $exeSize)
} else {
    Write-Warning "majin.exe not found! Please build it before packaging."
}

# Copy runtime DLLs to game-data/bin
$runtimeDll = if (Test-Path (Join-Path $srcBin 'rexruntime.dll')) {
    Join-Path $srcBin 'rexruntime.dll'
} else {
    Join-Path $root 'tools\sdk\win-amd64\bin\rexruntime.dll'
}
$gpuDll = if (Test-Path (Join-Path $srcBin 'rexgpu-xenos.dll')) {
    Join-Path $srcBin 'rexgpu-xenos.dll'
} else {
    Join-Path $root 'tools\sdk\win-amd64\bin\rexgpu-xenos.dll'
}
if (Test-Path $runtimeDll) { Copy-Item $runtimeDll $destBin -Force }
if (Test-Path $gpuDll) { Copy-Item $gpuDll $destBin -Force }

$readmeText = @"
Majistic Recomp - Game Data Directory
====================================
This directory contains the game executable and your extracted game assets.
Use 'Majistic Launcher.exe' (in the Setup & Build tab) to automatically
extract your legally owned Xbox 360 ISO image of Majin and the Forsaken Kingdom.

Structure:
- game-data/bin/        -> Native game executable (majin.exe) and runtime DLLs
- game-data/unpacked/   -> Decrypted executable image (loaded-image.bin)
- game-data/runtime/    -> Save files (user/), cache, and logs
"@
Set-Content -Path (Join-Path $destGameData 'README.txt') -Value $readmeText -Encoding UTF8
Write-Host "[6/7] Initialized game-data/ with precompiled binary and runtime DLLs"

# 7. Create ZIP archive if requested
if ($CreateZip) {
    if (Test-Path $zipPath) {
        Remove-Item $zipPath -Force
    }
    Write-Host "[7/7] Compressing into $zipPath..."
    Compress-Archive -Path "$releaseDir\*" -DestinationPath $zipPath -CompressionLevel Optimal
    $zipSize = (Get-Item $zipPath).Length / 1MB
    $msg = "Created archive: {0} ({1:N1} MB)" -f $zipPath, $zipSize
    Write-Host $msg
}

Write-Host @"
============================================================
SUCCESS: Release v$Version generated!
Folder:  $releaseDir
Archive: $zipPath

You can test 'Majistic Launcher.exe' inside '$releaseDir'.
============================================================
"@
