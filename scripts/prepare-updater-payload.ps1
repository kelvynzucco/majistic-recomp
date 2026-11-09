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

# Sign archive with minisign for Tauri auto-updater and generate latest.json
$keyPath = Join-Path $env:USERPROFILE ".tauri\majistic-launcher.key"
if (Test-Path $keyPath) {
    Write-Host "Signing updater package for in-app updater..."
    Push-Location (Join-Path $root 'launcher')
    try {
        $signOutput = & npm run tauri -- signer sign --private-key-path $keyPath --app-version $Version $zip
        $sigPath = "$zip.sig"
        $sig = if (Test-Path $sigPath) {
            (Get-Content -Path $sigPath -Raw).Trim()
        } else {
            $rawSig = ($signOutput | Out-String).Trim()
            $sigMatch = [regex]::Match($rawSig, '(?s)(dW50cnVzdGVkIGNvbW1lbnQ6[A-Za-z0-9+/=\r\n]+?)(?:\r?\n\r?\n|$)')
            if ($sigMatch.Success) { $sigMatch.Groups[1].Value.Trim() } else { $null }
        }
        if ($sig) {
            $latestJson = [ordered]@{
                version = $Version
                notes = "Majistic Recomp v$Version Update:`n- Separate Dev & Release launcher modes (zero local logs and no dev tools in release)`n- Resolved WebView2 console window popup and lock collision errors on startup`n- Default rendering path set to Vulkan FBO and VSync disabled by default`n- Mouse Sensitivity slider in Gameplay & Controls (0.1x to 10.0x)`n- Post-processing effects disabled by default with descriptive labels`n- Launcher window size increased to 980x710 (resizable)"
                pub_date = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
                platforms = @{
                    "windows-x86_64" = @{
                        signature = $sig
                        url = "https://github.com/kelvynzucco/majistic-recomp/releases/download/v$Version/Majistic-Launcher-v$Version-windows-x64.zip"
                    }
                }
            } | ConvertTo-Json -Depth 5
            $latestJsonPath = Join-Path $root 'latest.json'
            Set-Content -Path $latestJsonPath -Value $latestJson -Encoding UTF8
            Write-Host "Generated latest.json pointing to updater package ($latestJsonPath)"
        } else {
            throw "Could not extract signature from output: $signOutput"
        }
    } finally {
        Pop-Location
    }
} else {
    Write-Warning "Minisign key not found at $keyPath. Skipping signing."
}
