<#
.SYNOPSIS
    Automated Shader Mitigation Verification Gate Runner.

.DESCRIPTION
    Launches Majin under Vulkan with controlled cache parameters, runs for a specified duration,
    gracefully closes the process, and evaluates performance telemetry against DoD thresholds
    via scripts/analyze-performance-csv.mjs.

.PARAMETER Cold
    Runs with clean cache (first boot). Evaluates against DoD §6.1a thresholds.

.PARAMETER Warm
    Runs with pre-populated cache (subsequent session). Evaluates against DoD §6.1b thresholds.

.PARAMETER DurationSeconds
    Execution duration in seconds before initiating graceful shutdown (default: 60).

.PARAMETER VerifyGate
    Evaluates output against gate thresholds; exits with code 1 if failed.

.PARAMETER GameplayOnly
    Restricts gate evaluation to active 3D gameplay window.

.PARAMETER Vkpc
    Sets --vulkan_pipeline_cache=true/false (default: 'false').

.PARAMETER GameDataRoot
    Path to game-data folder (default: 'game-data-usa').

.PARAMETER Configuration
    Build configuration: 'Release' or 'Debug' (default: 'Release').

.PARAMETER OutputPath
    Optional file path to save the resulting JSON report.
#>
param(
    [switch]$Cold,
    [switch]$Warm,
    [int]$DurationSeconds = 60,
    [switch]$VerifyGate,
    [switch]$GameplayOnly,
    [switch]$Json,
    [string]$Vkpc = 'false',
    [string]$GameDataRoot = 'game-data',
    [string]$Configuration = 'Release',
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent

if (-not $Cold -and -not $Warm) {
    $Cold = $true
}

# Resolve game data root
if (-not [System.IO.Path]::IsPathRooted($GameDataRoot)) {
    $candidate = Join-Path $projectRoot $GameDataRoot
    if (-not (Test-Path $candidate) -and (Test-Path (Join-Path $projectRoot 'game-data'))) {
        $candidate = Join-Path $projectRoot 'game-data'
    }
    $GameDataRoot = [System.IO.Path]::GetFullPath($candidate)
}

if (-not (Test-Path (Join-Path $GameDataRoot 'default.xex'))) {
    throw "default.xex not found in $GameDataRoot"
}

# Locate majin.exe
$unifiedExe = Join-Path $GameDataRoot 'bin\majin.exe'
$buildPal = Join-Path $projectRoot ('prototype\out\build\win-amd64-' + $Configuration.ToLower())
$buildUsa = Join-Path $projectRoot ('prototype-usa\out\build\win-amd64-' + $Configuration.ToLower())
$exe = if (Test-Path $unifiedExe) {
    $unifiedExe
} elseif (Test-Path (Join-Path $buildPal 'majin.exe')) {
    Join-Path $buildPal 'majin.exe'
} elseif (Test-Path (Join-Path $buildUsa 'majin.exe')) {
    Join-Path $buildUsa 'majin.exe'
} else {
    $unifiedExe
}

if (-not (Test-Path $exe)) {
    throw "majin.exe executable not found at $exe. Please build the runtime first."
}

# Session directories
$sessionRoot = Join-Path $GameDataRoot 'runtime'
$cacheKey = 'vulkan-vulkan/' + $Configuration.ToLower()
$cacheRoot = Join-Path $sessionRoot ('cache/' + $cacheKey)
$metadataRoot = Join-Path $sessionRoot ('metadata/' + $cacheKey)
$logDir = Join-Path $sessionRoot 'logs'
$userDir = Join-Path $sessionRoot 'user'

New-Item -ItemType Directory -Force -Path $logDir, $userDir, $metadataRoot, $cacheRoot | Out-Null

# Handle Cold vs Warm cache state
if ($Cold) {
    Write-Host "[verify-shader-mitigation] COLD run requested: Purging pipeline cache at $cacheRoot..." -ForegroundColor Cyan
    if (Test-Path $cacheRoot) {
        Remove-Item -Path "$cacheRoot\*" -Recurse -Force -ErrorAction SilentlyContinue
    }
} else {
    Write-Host "[verify-shader-mitigation] WARM run requested: Retaining existing cache at $cacheRoot." -ForegroundColor Cyan
    $existingCaches = Get-ChildItem -Path $cacheRoot -Filter "*.xpso" -ErrorAction SilentlyContinue
    if (-not $existingCaches -or $existingCaches.Count -eq 0) {
        Write-Warning "[verify-shader-mitigation] WARM run requested but no .xpso cache found in $cacheRoot! Results may reflect cold run."
    }
}

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $logDir "runtime-verify-$timestamp.log"
$csvPath = "$logPath.performance.csv"
$fh1CsvPath = "$logPath.fh1.csv"

# Build launch arguments
$launchArgs = @(
    '--gpu_plugin', 'xenos',
    '--gpu_backend=vulkan',
    '--game_data_root', $GameDataRoot,
    '--user_data_root', $userDir,
    '--cache_root', $cacheRoot,
    '--metadata_root', $metadataRoot,
    '--log_file', $logPath,
    '--window_width=1280',
    '--window_height=720',
    '--fullscreen=false',
    '--vsync=true',
    '--resolution_scale=1',
    '--swap_post_effect=none',
    '--present_effect=fsr',
    '--present_dither=false',
    '--gpu_allow_invalid_fetch_constants=true',
    '--render_target_path_vulkan=fbo',
    '--vulkan_dynamic_rendering=false',
    '--vulkan_async_skip_incomplete_frames=false',
    "--vulkan_pipeline_cache=$Vkpc",
    '--mnk_mode=false',
    '--mnk_mouse=false',
    '--majin_window_fps=true',
    '--majin_record_performance=true',
    "--perf_log_csv=$csvPath",
    "--fh1_frame_cost_csv=$fh1CsvPath",
    '--majin_60fps=false',
    '--majin_skip_cutscenes=true',
    '--majin_cutscene_speed=4',
    '--majin_skip_intro_logos=true',
    '--majin_fov_scale=1.0',
    '--majin_pause_blur=false',
    '--majin_dof=false',
    '--majin_bloom=false'
)

$quotedArgs = @($launchArgs | ForEach-Object {
    '"' + (($_ -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"'
})

$workingDir = Split-Path $exe -Parent

Write-Host "[verify-shader-mitigation] Launching majin.exe (Duration: ${DurationSeconds}s, Mode: $(if ($Cold) { 'Cold' } else { 'Warm' }))..." -ForegroundColor Green
$proc = Start-Process -FilePath $exe -ArgumentList $quotedArgs -WorkingDirectory $workingDir -WindowStyle Normal -PassThru

try {
    $elapsed = 0
    $checkInterval = 2
    while ($elapsed -lt $DurationSeconds) {
        Start-Sleep -Seconds $checkInterval
        $elapsed += $checkInterval
        if ($proc.HasExited) {
            Write-Warning "[verify-shader-mitigation] Process exited prematurely after $elapsed seconds (ExitCode: $($proc.ExitCode))."
            break
        }
    }
} finally {
    if (-not $proc.HasExited) {
        Write-Host "[verify-shader-mitigation] Initiating graceful shutdown via CloseMainWindow..." -ForegroundColor Cyan
        $proc.CloseMainWindow() | Out-Null
        $closed = $proc.WaitForExit(6000)
        if (-not $closed -and -not $proc.HasExited) {
            Write-Warning "[verify-shader-mitigation] Process did not exit cleanly within 6s; terminating process."
            $proc.Kill() | Out-Null
            $proc.WaitForExit(3000)
        }
    }
}

Write-Host "[verify-shader-mitigation] Run completed. Waiting for CSV flush..." -ForegroundColor Green
Start-Sleep -Seconds 1

# Check CSV files (prefer fh1CsvPath for detailed shader compiler telemetry)
$targetCsv = if (Test-Path $fh1CsvPath) {
    $fh1CsvPath
} elseif (Test-Path $csvPath) {
    $csvPath
} else {
    throw "Neither $fh1CsvPath nor $csvPath was produced by the run!"
}

# Run analyzer
$analyzerScript = Join-Path $PSScriptRoot 'analyze-performance-csv.mjs'
$analyzerArgs = @($analyzerScript, "--input=$targetCsv")

if (Test-Path $logPath) {
    $analyzerArgs += "--log=$logPath"
}
if ($Cold) {
    $analyzerArgs += '--cold'
} else {
    $analyzerArgs += '--warm'
}
if ($GameplayOnly) {
    $analyzerArgs += '--gameplay-only'
}
if ($VerifyGate) {
    $analyzerArgs += '--gate'
}
if ($Json -or $OutputPath) {
    $analyzerArgs += '--json'
}

Write-Host "[verify-shader-mitigation] Running analyzer: node $($analyzerArgs -join ' ')" -ForegroundColor Cyan
$analysisOutput = & node $analyzerArgs
$exitCode = $LASTEXITCODE

if ($OutputPath) {
    [System.IO.File]::WriteAllText($OutputPath, $analysisOutput, [System.Text.UTF8Encoding]::new($false))
    Write-Host "[verify-shader-mitigation] Analysis saved to $OutputPath" -ForegroundColor Green
}

if (-not $Json) {
    Write-Host $analysisOutput
}

if ($exitCode -eq 0) {
    Write-Host "[verify-shader-mitigation] GATE PASSED (exit code 0)" -ForegroundColor Green
} else {
    Write-Warning "[verify-shader-mitigation] GATE FAILED (exit code $exitCode)"
}

exit $exitCode
