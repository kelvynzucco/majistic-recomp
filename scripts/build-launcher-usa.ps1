$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

Write-Output "[1/2] Building Majistic Launcher (Tauri v2 + Tailwind)..."
$launcherDir = Join-Path $root 'launcher'

$cargoBin = Join-Path $env:USERPROFILE '.cargo\bin'
if (Test-Path $cargoBin) {
    $env:PATH = "$cargoBin;" + $env:PATH
}

Push-Location $launcherDir
try {
    & npm run tauri build
    if ($LASTEXITCODE -ne 0) { throw "Tauri build failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Write-Output "[2/2] Copying executable to project root..."
$sourceExe = Join-Path $launcherDir 'src-tauri\target\release\majistic-launcher.exe'
$destExe = Join-Path $root 'Majistic Launcher.exe'

if (Test-Path $sourceExe) {
    Copy-Item -Path $sourceExe -Destination $destExe -Force
    Write-Output "SUCCESS: Majistic Launcher built successfully at: $destExe"
} else {
    throw "Built binary not found at $sourceExe"
}
