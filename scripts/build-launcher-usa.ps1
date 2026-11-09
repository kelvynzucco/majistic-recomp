param(
    [switch]$Dev
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

Write-Output "[1/3] Generating launcher icons..."
$genIconsScript = Join-Path $PSScriptRoot 'generate-icons.ps1'
if (Test-Path $genIconsScript) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $genIconsScript
}

$modeText = if ($Dev) { "DEVELOPMENT (Dev Mode with Diagnostics & Tooling)" } else { "RELEASE (Clean, No Local Logs, Streamlined)" }
Write-Output "[2/3] Building Majistic Launcher ($modeText)..."
$launcherDir = Join-Path $root 'launcher'

$cargoBin = Join-Path $env:USERPROFILE '.cargo\bin'
if (Test-Path $cargoBin) {
    $env:PATH = "$cargoBin;" + $env:PATH
}

Push-Location $launcherDir
try {
    if ($Dev) {
        & npm run tauri build -- --features dev-mode
    } else {
        & npm run tauri build
    }
    if ($LASTEXITCODE -ne 0) { throw "Tauri build failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Output "[3/3] Copying executable to project root..."
$sourceExe = Join-Path $launcherDir 'src-tauri\target\release\majistic-launcher.exe'
$destName = if ($Dev) { 'Majistic Launcher (Dev).exe' } else { 'Majistic Launcher.exe' }
$destExe = Join-Path $root $destName

if (Test-Path $sourceExe) {
    Copy-Item -Path $sourceExe -Destination $destExe -Force
    Write-Output "SUCCESS: $destName built successfully at: $destExe"
} else {
    throw "Built binary not found at $sourceExe"
}
