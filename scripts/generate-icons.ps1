# Generate all launcher icons from branding/majistic_icon.png
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$rootDir = Split-Path -Parent $scriptDir
$sourcePng = Join-Path $rootDir "branding\majistic_icon.png"
$iconsDir = Join-Path $rootDir "launcher\src-tauri\icons"

if (-not (Test-Path $sourcePng)) {
    Write-Error "Source icon not found: $sourcePng"
    exit 1
}

Add-Type -AssemblyName System.Drawing

function Resize-Bitmap {
    param(
        [System.Drawing.Bitmap]$source,
        [int]$width,
        [int]$height
    )
    $dest = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($dest)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($source, 0, 0, $width, $height)
    $g.Dispose()
    return $dest
}

function Create-BmpIconData {
    param([System.Drawing.Bitmap]$bmp)
    $w = $bmp.Width
    $h = $bmp.Height
    $andRowStride = [int]([Math]::Ceiling($w / 32.0) * 4)
    $andMaskSize = $andRowStride * $h
    $xorSize = $w * $h * 4
    $totalSize = 40 + $xorSize + $andMaskSize

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)

    # BITMAPINFOHEADER (40 bytes)
    $bw.Write([uint32]40)              # biSize
    $bw.Write([int32]$w)               # biWidth
    $bw.Write([int32]($h * 2))         # biHeight (doubled for ICO BMP)
    $bw.Write([uint16]1)               # biPlanes
    $bw.Write([uint16]32)              # biBitCount (32bpp ARGB)
    $bw.Write([uint32]0)               # biCompression (BI_RGB)
    $bw.Write([uint32]($xorSize + $andMaskSize)) # biSizeImage
    $bw.Write([int32]0)                # biXPelsPerMeter
    $bw.Write([int32]0)                # biYPelsPerMeter
    $bw.Write([uint32]0)               # biClrUsed
    $bw.Write([uint32]0)               # biClrImportant

    # XOR mask (bottom-up BGRA)
    for ($y = $h - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $w; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $bw.Write([byte]$c.B)
            $bw.Write([byte]$c.G)
            $bw.Write([byte]$c.R)
            $bw.Write([byte]$c.A)
        }
    }

    # AND mask (bottom-up 1bpp, 1 = transparent, 0 = opaque)
    for ($y = $h - 1; $y -ge 0; $y--) {
        $row = New-Object byte[] $andRowStride
        for ($x = 0; $x -lt $w; $x++) {
            $c = $bmp.GetPixel($x, $y)
            if ($c.A -eq 0) {
                $byteIdx = [int]($x / 8)
                $bitIdx = 7 - ($x % 8)
                $row[$byteIdx] = [byte]($row[$byteIdx] -bor (1 -shl $bitIdx))
            }
        }
        $bw.Write($row, 0, $andRowStride)
    }

    $bw.Flush()
    $bytes = $ms.ToArray()
    $bw.Dispose()
    $ms.Dispose()
    return $bytes
}

Write-Host "Loading source image: $sourcePng"
$src = [System.Drawing.Bitmap]::FromFile($sourcePng)

# Generate individual PNG files for web / icons
$pngSizes = @{
    "16x16.png" = 16
    "32x32.png" = 32
    "64x64.png" = 64
    "128x128.png" = 128
    "128x128@2x.png" = 256
    "icon.png" = 512
}

foreach ($name in $pngSizes.Keys) {
    $size = $pngSizes[$name]
    $destPath = Join-Path $iconsDir $name
    $resized = Resize-Bitmap $src $size $size
    $resized.Save($destPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $resized.Dispose()
    Write-Host "Generated $destPath ($size x $size)"
}

# Now generate multi-resolution icon.ico:
# Sizes <= 64 are BMP (standard uncompressed DIB with alpha), size 256 is PNG compressed
$icoBmpSizes = @(16, 24, 32, 48, 64)
$icoPngSizes = @(256)

$imageBytesList = New-Object System.Collections.Generic.List[byte[]]
$sizesList = New-Object System.Collections.Generic.List[int]

foreach ($s in $icoBmpSizes) {
    $resized = Resize-Bitmap $src $s $s
    $bmpData = Create-BmpIconData $resized
    $imageBytesList.Add($bmpData)
    $sizesList.Add($s)
    $resized.Dispose()
}

foreach ($s in $icoPngSizes) {
    $resized = Resize-Bitmap $src $s $s
    $ms = New-Object System.IO.MemoryStream
    $resized.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $imageBytesList.Add($ms.ToArray())
    $sizesList.Add($s)
    $ms.Dispose()
    $resized.Dispose()
}

$icoPath = Join-Path $iconsDir "icon.ico"
$icoMs = New-Object System.IO.MemoryStream
$icoBw = New-Object System.IO.BinaryWriter($icoMs)

$count = $imageBytesList.Count
$icoBw.Write([uint16]0)      # reserved
$icoBw.Write([uint16]1)      # type 1 = icon
$icoBw.Write([uint16]$count) # count

$offset = 6 + (16 * $count)
for ($i = 0; $i -lt $count; $i++) {
    $s = $sizesList[$i]
    $data = $imageBytesList[$i]
    $wByte = if ($s -eq 256) { [byte]0 } else { [byte]$s }
    $hByte = if ($s -eq 256) { [byte]0 } else { [byte]$s }

    $icoBw.Write($wByte)
    $icoBw.Write($hByte)
    $icoBw.Write([byte]0)        # color count
    $icoBw.Write([byte]0)        # reserved
    $icoBw.Write([uint16]1)      # planes
    $icoBw.Write([uint16]32)     # bpp
    $icoBw.Write([uint32]$data.Length)
    $icoBw.Write([uint32]$offset)
    $offset += $data.Length
}

for ($i = 0; $i -lt $count; $i++) {
    $icoBw.Write($imageBytesList[$i])
}

$icoBw.Flush()
[System.IO.File]::WriteAllBytes($icoPath, $icoMs.ToArray())
$icoBw.Dispose()
$icoMs.Dispose()
$src.Dispose()

Write-Host "Generated multi-resolution $icoPath with $($imageBytesList.Count) frames (16-64 BMP, 256 PNG)"
