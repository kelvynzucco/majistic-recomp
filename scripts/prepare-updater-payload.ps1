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

$zip = Join-Path $root "Majistic-Launcher-v$Version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$staging\*" -DestinationPath $zip -CompressionLevel Optimal
Remove-Item $staging -Recurse -Force

$zipItem = Get-Item $zip
Write-Host "Created updater zip: $($zipItem.FullName) ($([math]::Round($zipItem.Length / 1MB, 1)) MB)"
