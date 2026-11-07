param(
    [string]$CacheRoot = ""
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if ([string]::IsNullOrWhiteSpace($CacheRoot)) {
    $CacheRoot = Join-Path $root 'private\runtime-usa\cache'
}

if (-not (Test-Path $CacheRoot)) {
    Write-Host "Cache directory not found: $CacheRoot"
    exit 0
}

Write-Host "Consolidating shader caches in: $CacheRoot"

foreach ($backend in @('d3d12-default', 'd3d12-rov')) {
    $backendDir = Join-Path $CacheRoot $backend
    if (-not (Test-Path $backendDir)) { continue }

    $targetDir = Join-Path $backendDir 'release\shaders\shareable'
    New-Item -ItemType Directory -Force -Path $targetDir | Out-Null

    $targetXsh = Join-Path $targetDir '4E4D0816.xsh'
    $targetXpsoName = if ($backend -eq 'd3d12-rov') { '4E4D0816.rov.d3d12.xpso' } else { '4E4D0816.rtv.d3d12.xpso' }
    $targetXpso = Join-Path $targetDir $targetXpsoName

    # Collect all matching files from any scale subdirectories
    $allXsh = Get-ChildItem -Path $backendDir -Recurse -Filter '4E4D0816.xsh' | Where-Object { $_.FullName -ne $targetXsh }
    $allXpso = Get-ChildItem -Path $backendDir -Recurse -Filter $targetXpsoName | Where-Object { $_.FullName -ne $targetXpso }

    # Keep the largest / most comprehensive shader microcode file
    $bestXsh = ($allXsh | Sort-Object Length -Descending | Select-Object -First 1)
    if ($bestXsh -and (-not (Test-Path $targetXsh) -or $bestXsh.Length -gt (Get-Item $targetXsh).Length)) {
        Copy-Item -LiteralPath $bestXsh.FullName -Destination $targetXsh -Force
        Write-Host "[$backend] Updated 4E4D0816.xsh from $($bestXsh.FullName) ($($bestXsh.Length) bytes)"
    }

    # Keep the largest / most comprehensive PSO descriptions file
    $bestXpso = ($allXpso | Sort-Object Length -Descending | Select-Object -First 1)
    if ($bestXpso -and (-not (Test-Path $targetXpso) -or $bestXpso.Length -gt (Get-Item $targetXpso).Length)) {
        Copy-Item -LiteralPath $bestXpso.FullName -Destination $targetXpso -Force
        Write-Host "[$backend] Updated $targetXpsoName from $($bestXpso.FullName) ($($bestXpso.Length) bytes)"
    }
}

Write-Host "Cache consolidation completed."
