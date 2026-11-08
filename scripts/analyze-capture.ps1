param([string]$ImagePath)
Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::FromFile($ImagePath)
$n = 0
$total = 0
for ($x = 0; $x -lt $b.Width; $x += 4) {
    for ($y = 0; $y -lt $b.Height; $y += 4) {
        $total++
        $c = $b.GetPixel($x, $y)
        if ($c.R -gt 25 -or $c.G -gt 25 -or $c.B -gt 25) {
            $n++
        }
    }
}
$w = $b.Width
$h = $b.Height
$b.Dispose()
Write-Host "$ImagePath : Resolution=${w}x${h}, NonBlackSamples=$n / $total ($([math]::Round(100.0 * $n / $total, 1))%)"
