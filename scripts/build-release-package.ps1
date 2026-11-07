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

# 6. Create clean game-data template
$destGameData = Join-Path $releaseDir 'game-data'
New-Item -ItemType Directory -Path $destGameData -Force | Out-Null
$readmeText = @"
Majistic Recomp - Game Data Directory
====================================
Place your extracted game files here, or use 'Majistic Launcher.exe'
(in the Build & Setup tab) to automatically extract your legally owned
Xbox 360 ISO image of Majin and the Forsaken Kingdom.

Structure:
- game-data/bin/        -> Native game executable (majin.exe)
- game-data/unpacked/   -> Unpacked executable image (loaded-image.bin)
- game-data/runtime/    -> Save files (user/), cache, and logs
"@
Set-Content -Path (Join-Path $destGameData 'README.txt') -Value $readmeText -Encoding UTF8
Write-Host "[6/7] Initialized clean game-data/ template"

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
