param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('1280x720','1920x1080','2560x1440','3840x2160')][string]$WindowSize = '1280x720',
    $Fullscreen = $false,
    $VSync = $true,
    [ValidateRange(1,3)][int]$RenderScale = 1,
    [ValidateSet('none','fxaa')][string]$PostEffect = 'none',
    [ValidateSet('fsr','cas','bilinear')][string]$Upscaler = 'fsr',
    [ValidateSet('Default','ROV','Vulkan')][string]$RenderingPath = 'Default',
    [ValidateSet('Controller','KeyboardMouse')][string]$InputMode = 'KeyboardMouse',
    [ValidateRange(0.01,10)][double]$MouseSensitivity = 1.0,
    $RecordPerformance = $true,
    $IncludeHardware = $false,
    [ValidateSet('Original','60')][string]$FrameRateMode = '60',
    $SkipCutscenes = $true,
    [ValidateRange(2,16)][int]$CutsceneSpeed = 4,
    $SkipIntroLogos = $false,
    [string]$GameDataRoot = (Join-Path (Split-Path $PSScriptRoot -Parent) 'game-data-usa'),
    [switch]$Preview
)
$ErrorActionPreference = 'Stop'

function ConvertTo-Bool($val) {
    if ($val -is [bool]) { return $val }
    $s = "$val".Trim().ToLowerInvariant().TrimStart('$')
    return ($s -eq 'true' -or $s -eq '1')
}

$isFullscreen = ConvertTo-Bool $Fullscreen
$isVSync = ConvertTo-Bool $VSync
$isRecordPerf = ConvertTo-Bool $RecordPerformance

$projectRoot = Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot 'reports') | Out-Null

if (-not (Test-Path (Join-Path $GameDataRoot 'default.xex'))) {
    throw "default.xex not found in $GameDataRoot"
}

$build = Join-Path $projectRoot ('prototype-usa\out\build\win-amd64-' + $Configuration.ToLower())
$exe = Join-Path $build 'majin.exe'
if (-not (Test-Path $exe)) {
    throw "Build majin.exe before running ($exe)."
}

$sessionRoot = Join-Path $projectRoot 'private\runtime-usa'
New-Item -ItemType Directory -Force -Path $sessionRoot | Out-Null

$isVulkan = ($RenderingPath -eq 'Vulkan')
$gpuBackend = if ($isVulkan) { 'vulkan' } else { 'd3d12' }
$cacheKey = $gpuBackend + '-' + $RenderingPath.ToLower() + '/' + $Configuration.ToLower()
$cacheRoot = Join-Path $sessionRoot ('cache/' + $cacheKey)
$metadataRoot = Join-Path $sessionRoot ('metadata/' + $cacheKey)
New-Item -ItemType Directory -Force -Path $cacheRoot, $metadataRoot, "$sessionRoot/user" | Out-Null

$renderTarget = if ($RenderingPath -eq 'ROV') { 'rov' } else { '' }
$allowInvalid = 'true'
$logPath = Join-Path $projectRoot ('reports\runtime-usa-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')

$dimensions = $WindowSize.Split('x')
$launchArgs = @(
    '--gpu_plugin', 'xenos',
    "--gpu_backend=$gpuBackend",
    '--game_data_root', $GameDataRoot,
    '--user_data_root', "$sessionRoot/user",
    '--cache_root', $cacheRoot,
    '--metadata_root', $metadataRoot,
    '--log_file', $logPath,
    "--window_width=$($dimensions[0])",
    "--window_height=$($dimensions[1])",
    "--fullscreen=$($isFullscreen.ToString().ToLowerInvariant())",
    "--vsync=$($isVSync.ToString().ToLowerInvariant())",
    "--resolution_scale=$RenderScale",
    "--swap_post_effect=$PostEffect",
    "--present_effect=$Upscaler",
    "--gpu_allow_invalid_fetch_constants=$allowInvalid"
)
if ($isVulkan) {
    $launchArgs += @(
        '--render_target_path_vulkan=fbo',
        '--vulkan_dynamic_rendering=false',
        '--vulkan_async_skip_incomplete_frames=false'
    )
} else {
    $launchArgs += "--render_target_path_d3d12=$renderTarget"
}
if ($isVSync -and -not $isVulkan) {
    $launchArgs += '--d3d12_allow_variable_refresh_rate_and_tearing=false'
}

$mnk = ($InputMode -eq 'KeyboardMouse').ToString().ToLowerInvariant()
$launchArgs += @("--mnk_mode=$mnk", "--mnk_mouse=$mnk")
if ($InputMode -eq 'KeyboardMouse') {
    $launchArgs += "--mnk_sensitivity=$($MouseSensitivity.ToString([Globalization.CultureInfo]::InvariantCulture))"
    $bindings = [ordered]@{
        a='Space'; b='E,Shift+Shift'; x='LMB'; y='R'
        left_trigger='Q'; right_trigger='RMB'; left_shoulder='F'; right_shoulder='C'
        lstick_up='W'; lstick_down='S'; lstick_left='A'; lstick_right='D'; lstick_press='V'
        rstick_up='I'; rstick_down='K'; rstick_left='J'; rstick_right='L'; rstick_press='MMB'
        dpad_up='Up'; dpad_down='Down'; dpad_left='Left'; dpad_right='Right'
        start='Return'; back='Tab'; guide='None'
    }
    foreach ($binding in $bindings.GetEnumerator()) {
        $variants = foreach ($key in $binding.Value.Split(',')) {
            $key
            if ($key -ne 'None' -and -not $key.Contains('+')) { "Shift+$key" }
        }
        $launchArgs += "--keybind_$($binding.Key)=$($variants -join ',')"
    }
}

$launchArgs += "--majin_window_fps=$((-not $isFullscreen).ToString().ToLowerInvariant())"
$launchArgs += "--majin_record_performance=$($isRecordPerf.ToString().ToLowerInvariant())"
if ($isRecordPerf) { $launchArgs += "--perf_log_csv=$logPath.performance.csv" }
$launchArgs += "--majin_60fps=$(($FrameRateMode -eq '60').ToString().ToLowerInvariant())"
$isSkipCutscenes = ConvertTo-Bool $SkipCutscenes
$launchArgs += "--majin_skip_cutscenes=$($isSkipCutscenes.ToString().ToLowerInvariant())"
$launchArgs += "--majin_cutscene_speed=$CutsceneSpeed"
$isSkipIntroLogos = ConvertTo-Bool $SkipIntroLogos
$launchArgs += "--majin_skip_intro_logos=$($isSkipIntroLogos.ToString().ToLowerInvariant())"

if ($Preview) {
    Write-Output "PREVIEW OK: $exe $($launchArgs -join ' ')"
    return
}

Write-Output "Runtime log: $logPath"
$quotedArgs = @($launchArgs | ForEach-Object {
    '"' + (($_ -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"'
})

Write-Output "Iniciando majin.exe ($WindowSize, Fullscreen=$isFullscreen, InputMode=$InputMode, FrameRate=$FrameRateMode)..."
$process = Start-Process -FilePath $exe -ArgumentList $quotedArgs -WorkingDirectory $build -WindowStyle Normal -PassThru -Wait
Write-Output "majin.exe encerrado com ExitCode: $($process.ExitCode)"
