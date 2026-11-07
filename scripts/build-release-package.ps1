<#
.SYNOPSIS
    Builds and packages Majistic Launcher for a GitHub Release, including updater artifacts.
.PARAMETER Version
    The version tag to build (e.g. 0.1.0). If omitted, uses the version in tauri.conf.json.
.PARAMETER Notes
    Optional release notes to include in latest.json.
#>
param(
    [string]$Version,
    [string]$Notes = "Majistic Recomp release"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$launcherDir = Join-Path $root 'launcher'
$tauriConfPath = Join-Path $launcherDir 'src-tauri\tauri.conf.json'
$keyPath = Join-Path $env:USERPROFILE '.tauri\majistic-launcher.key'

# 1. Update version if specified
if ($Version) {
    Write-Host "[1/4] Setting version to $Version..."
    $conf = Get-Content $tauriConfPath -Raw | ConvertFrom-Json
    $conf.version = $Version
    $conf | ConvertTo-Json -Depth 10 | Set-Content $tauriConfPath -Encoding UTF8
} else {
    $conf = Get-Content $tauriConfPath -Raw | ConvertFrom-Json
    $Version = $conf.version
    Write-Host "[1/4] Using current version: $Version"
}

# 2. Generate icons
Write-Host "[2/4] Generating icons..."
$genIcons = Join-Path $PSScriptRoot 'generate-icons.ps1'
if (Test-Path $genIcons) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $genIcons
}

# 3. Setup signing key
if (Test-Path $keyPath) {
    Write-Host "[3/4] Found signing key at $keyPath. Enabling updater signing..."
    $env:TAURI_SIGNING_PRIVATE_KEY = Get-Content $keyPath -Raw
} else {
    Write-Warning "Signing key not found at $keyPath. Build will proceed without updater signature."
}

# 4. Build launcher
Write-Host "[4/4] Building production launcher..."
$cargoBin = Join-Path $env:USERPROFILE '.cargo\bin'
if (Test-Path $cargoBin) {
    $env:PATH = "$cargoBin;" + $env:PATH
}

Push-Location $launcherDir
try {
    & npm run tauri build
    if ($LASTEXITCODE -ne 0) { throw "Tauri build failed" }
} finally {
    Pop-Location
}

# Copy binary to root
$sourceExe = Join-Path $launcherDir 'src-tauri\target\release\majistic-launcher.exe'
$destExe = Join-Path $root 'Majistic Launcher.exe'
if (Test-Path $sourceExe) {
    Copy-Item $sourceExe $destExe -Force
    Write-Host "Updated root executable: $destExe"
}

$releaseDir = Join-Path $root "release-v$Version"
if (-not (Test-Path $releaseDir)) {
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
}

Copy-Item $destExe (Join-Path $releaseDir 'Majistic Launcher.exe') -Force

Write-Host @"
============================================================
Release Build Complete: v$Version
============================================================
Files prepared in: $releaseDir

To publish on GitHub:
1. Create a tag and release: v$Version
2. Upload the artifacts from: $releaseDir
3. Users who open the launcher will be able to check and update automatically!
============================================================
"@
