param(
    [string]$Version = "0.1.1"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$staging = Join-Path $root 'updater-staging'

if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $staging 'game-data\bin'), (Join-Path $staging 'scripts') -Force | Out-Null

Copy-Item (Join-Path $root 'Majistic Launcher.exe') $staging -Force
Copy-Item (Join-Path $root 'game-data\bin\majin.exe') (Join-Path $staging 'game-data\bin') -Force
Copy-Item (Join-Path $root 'game-data\bin\rexgpu-xenos.dll') (Join-Path $staging 'game-data\bin') -Force
Copy-Item (Join-Path $root 'game-data\bin\rexruntime.dll') (Join-Path $staging 'game-data\bin') -Force
Copy-Item (Join-Path $root 'scripts\*') (Join-Path $staging 'scripts') -Recurse -Force

# Create ZIP archive with Method 0 (Store - No compression) via bsdtar (tar.exe).
# This is REQUIRED because the Tauri updater zip crate in v0.1.0 was compiled without
# DEFLATE (Method 8) support. Store (Method 0) is natively supported by all versions of the zip crate.
$zip = Join-Path $root "Majistic-Launcher-v$Version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }

$tarExe = "C:\Windows\System32\tar.exe"
if (-not (Test-Path $tarExe)) {
    $tarExe = (Get-Command tar.exe).Source
}

Push-Location $staging
try {
    & $tarExe -cf $zip --format zip --options zip:compression=store *
    if ($LASTEXITCODE -ne 0) { throw "tar.exe failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

Remove-Item $staging -Recurse -Force

# Validate that the zip local header uses Method 0
$bytes = [System.IO.File]::ReadAllBytes($zip)
$method = [System.BitConverter]::ToUInt16($bytes, 8)
if ($method -ne 0) {
    throw "Verification failed: ZIP compression method is $method, expected 0 (Store)"
}

$zipItem = Get-Item $zip
Write-Host "Created updater zip (Method 0 - Store verified): $($zipItem.FullName) ($([math]::Round($zipItem.Length / 1MB, 1)) MB)"
