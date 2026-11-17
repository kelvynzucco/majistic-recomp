param(
    [string]$GameDataRoot = 'game-data'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not [System.IO.Path]::IsPathRooted($GameDataRoot)) {
    $candidate = Join-Path $root $GameDataRoot
    if (-not (Test-Path $candidate) -and (Test-Path (Join-Path $root 'game-data'))) {
        $candidate = Join-Path $root 'game-data'
    } elseif (-not (Test-Path $candidate) -and (Test-Path (Join-Path $root 'game-data-usa'))) {
        $candidate = Join-Path $root 'game-data-usa'
    }
    $GameDataRoot = [System.IO.Path]::GetFullPath($candidate)
}
$xexPath = Join-Path $GameDataRoot 'default.xex'
$privDirUsa = Join-Path $root 'prototype-usa\private'
$privDirPal = Join-Path $root 'prototype\private'
$unpackedDir = Join-Path $GameDataRoot 'unpacked'
$repDir  = Join-Path $root 'reports'
New-Item -ItemType Directory -Force -Path $privDirUsa, $privDirPal, $unpackedDir, $repDir | Out-Null

if (-not (Test-Path $xexPath)) {
    Write-Error "default.xex not found at $xexPath. Please place your game dump in $GameDataRoot."
    exit 1
}

$bytes = [System.IO.File]::ReadAllBytes($xexPath)
$retailKey = [byte[]]@(0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3, 0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91)
$encFileKey = $bytes[(0x88 + 0x150)..(0x88 + 0x15F)]

$aesKeyDec = [System.Security.Cryptography.Aes]::Create()
$aesKeyDec.Mode = [System.Security.Cryptography.CipherMode]::ECB
$aesKeyDec.Padding = [System.Security.Cryptography.PaddingMode]::None
$fileKey = $aesKeyDec.CreateDecryptor($retailKey, $null).TransformFinalBlock($encFileKey, 0, 16)

$aesPayload = [System.Security.Cryptography.Aes]::Create()
$aesPayload.Mode = [System.Security.Cryptography.CipherMode]::CBC
$aesPayload.Padding = [System.Security.Cryptography.PaddingMode]::None
$iv = New-Object byte[] 16

$peOffset = 0x3000
$rawPayloadSize = $bytes.Length - $peOffset
$decrypted = $aesPayload.CreateDecryptor($fileKey, $iv).TransformFinalBlock($bytes, $peOffset, $rawPayloadSize)

# Expand the 4 Basic Compression blocks from Format Info header at 0x1768
$fmtOffset = 0x1768
$blocks = @(
    @{ Data = 0x148000; Zero = 0x8000 },
    @{ Data = 0x9E8000; Zero = 0x8000 },
    @{ Data = 0x48000;  Zero = 0x270000 },
    @{ Data = 0x30000;  Zero = 0x0 }
)

$imageSize = 0xE30000
$image = New-Object byte[] $imageSize
$srcPos = 0
$dstPos = 0
foreach ($b in $blocks) {
    [Array]::Copy($decrypted, $srcPos, $image, $dstPos, $b.Data)
    $srcPos += $b.Data
    $dstPos += $b.Data + $b.Zero
}

$imagePathUsa = Join-Path $privDirUsa 'loaded-image.bin'
[System.IO.File]::WriteAllBytes($imagePathUsa, $image)

if (Test-Path $privDirPal) {
    $imagePathPal = Join-Path $privDirPal 'loaded-image.bin'
    [System.IO.File]::WriteAllBytes($imagePathPal, $image)
}

$unpackedImagePath = Join-Path $unpackedDir 'loaded-image.bin'
[System.IO.File]::WriteAllBytes($unpackedImagePath, $image)
Write-Host "Saved unpacked linear PE image ($($image.Length) bytes) to $imagePathUsa and $unpackedImagePath"

# Detect edition from default.xex (USA = 76FDB642, PAL = 67BD87EF) and configure matching executable
$isPal = $false
$maxScan = [Math]::Min($bytes.Length, 16384)
for ($i = 0; $i -lt ($maxScan - 4); $i++) {
    if ($bytes[$i] -eq 0x67 -and $bytes[$i+1] -eq 0xBD -and $bytes[$i+2] -eq 0x87 -and $bytes[$i+3] -eq 0xEF) {
        $isPal = $true
        break
    }
}
$editionName = if ($isPal) { 'Europe / PAL (0.0.0.3)' } else { 'USA / NTSC-U (0.0.0.2)' }
$editionSuffix = if ($isPal) { 'pal' } else { 'usa' }
Write-Host "Detected game edition: $editionName"

$binDir = Join-Path $GameDataRoot 'bin'
New-Item -ItemType Directory -Force -Path $binDir | Out-Null
$targetExe = Join-Path $binDir 'majin.exe'
$precompiledEdition = Join-Path $binDir "majin-$editionSuffix.exe"
$buildEdition = if ($isPal) {
    Join-Path $root 'prototype\out\build\win-amd64-release\majin.exe'
} else {
    Join-Path $root 'prototype-usa\out\build\win-amd64-release\majin.exe'
}

if (Test-Path $precompiledEdition) {
    Copy-Item -LiteralPath $precompiledEdition -Destination $targetExe -Force
    Write-Host "Deployed precompiled game binary for $editionName (majin-$editionSuffix.exe -> majin.exe)"
} elseif (Test-Path $buildEdition) {
    Copy-Item -LiteralPath $buildEdition -Destination $targetExe -Force
    Write-Host "Deployed built game binary for $editionName ($buildEdition -> majin.exe)"
}

