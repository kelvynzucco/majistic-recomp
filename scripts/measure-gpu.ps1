# Samples NVIDIA GPU utilization/power/clock while majin.exe runs and prints a
# summary. Diagnostic only: reads nvidia-smi, changes nothing.
# Usage: .\scripts\measure-gpu.ps1 -Label rov-1x -Seconds 60 [-WaitForGame]
param(
    [string]$Label = 'session',
    [ValidateRange(5, 3600)][int]$Seconds = 60,
    [switch]$WaitForGame
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$reports = Join-Path $root 'reports'
New-Item -ItemType Directory -Force -Path $reports | Out-Null
if (-not (Get-Command nvidia-smi -ErrorAction SilentlyContinue)) { throw 'nvidia-smi not found.' }

if ($WaitForGame) {
    while (-not (Get-Process majin -ErrorAction SilentlyContinue)) { Start-Sleep -Milliseconds 500 }
}

$csv = Join-Path $reports ("gpu-$Label-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.csv')
$samples = [System.Collections.Generic.List[object]]::new()
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    if (-not (Get-Process majin -ErrorAction SilentlyContinue)) { break }
    $line = (nvidia-smi --query-gpu=utilization.gpu,power.draw,clocks.gr,memory.used --format=csv,noheader,nounits) -split ',\s*'
    $samples.Add([pscustomobject]@{
        t = (Get-Date).ToString('HH:mm:ss.fff')
        util = [double]$line[0]; power = [double]$line[1]; clock = [double]$line[2]; vram = [double]$line[3]
    })
    Start-Sleep -Milliseconds 500
}
$samples | Export-Csv -NoTypeInformation -Path $csv
if ($samples.Count -eq 0) { Write-Output 'No samples (majin.exe not running).'; return }

function Stat($name) {
    $v = $samples | ForEach-Object { $_.$name } | Sort-Object
    [pscustomobject]@{
        Metric = $name
        Avg = [math]::Round(($v | Measure-Object -Average).Average, 1)
        P50 = $v[[int][math]::Floor(($v.Count - 1) * 0.5)]
        P95 = $v[[int][math]::Floor(($v.Count - 1) * 0.95)]
        Max = $v[-1]
    }
}
Write-Output "[$Label] $($samples.Count) samples -> $csv"
'util', 'power', 'clock', 'vram' | ForEach-Object { Stat $_ } | Format-Table -AutoSize | Out-String | Write-Output
