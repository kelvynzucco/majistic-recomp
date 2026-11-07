$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$xexPath = Join-Path $root 'game-data-usa\default.xex'
$privDir = Join-Path $root 'prototype-usa\private'
$repDir  = Join-Path $root 'reports'
New-Item -ItemType Directory -Force -Path $privDir, $repDir | Out-Null

if (-not (Test-Path $xexPath)) {
    Write-Error "default.xex not found at $xexPath. Please place your USA dump in game-data-usa/."
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

$imagePath = Join-Path $privDir 'loaded-image.bin'
[System.IO.File]::WriteAllBytes($imagePath, $image)
Write-Host "Saved unpacked linear PE image ($($image.Length) bytes) to $imagePath"
