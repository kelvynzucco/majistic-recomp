param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('1280x720','1920x1080','2560x1440','3840x2160')][string]$WindowSize = '1280x720',
    $Fullscreen = $false,
    $VSync = $true,
    [ValidateRange(1,3)][int]$RenderScale = 1,
    [ValidateSet('none','fxaa','fxaa_extreme')][string]$PostEffect = 'none',
    [ValidateSet('fsr','cas','bilinear')][string]$Upscaler = 'fsr',
    [ValidateSet('Default','ROV','RTV','Vulkan','Vulkan_FSI','Vulkan_FBO')][string]$RenderingPath = 'Default',
    [ValidateSet('Controller','KeyboardMouse')][string]$InputMode = 'Controller',
    [ValidateRange(0.01,10)][double]$MouseSensitivity = 1.0,
    $RecordPerformance = $true,
    $IncludeHardware = $false,
    [ValidateSet('Original','30','60','Uncapped')][string]$FrameRateMode = '30',
    $SkipCutscenes = $true,
    [ValidateRange(1,16)][int]$CutsceneSpeed = 4,
    $SkipIntroLogos = $false,
    [ValidateSet('English','Spanish','French','German','Italian','Korean','Japanese','Portuguese')][string]$Language = 'English',
    $StepFix = $false,
    $AnimSpeedFix = $true,
    $DtSmoothing = $false,
    [ValidateRange(0.5, 2.0)][double]$FovScale = 1.0,
    $PauseBlur = $true,
    $DepthOfField = $true,
    $Bloom = $true,
    $AutoCapture = $false,
    [string]$GameDataRoot = 'game-data',
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

if (-not [System.IO.Path]::IsPathRooted($GameDataRoot)) {
    $candidate = Join-Path $projectRoot $GameDataRoot
    if (-not (Test-Path $candidate) -and (Test-Path (Join-Path $projectRoot 'game-data-usa'))) {
        $candidate = Join-Path $projectRoot 'game-data-usa'
    }
    $GameDataRoot = [System.IO.Path]::GetFullPath($candidate)
}

if (-not (Test-Path (Join-Path $GameDataRoot 'default.xex'))) {
    throw "default.xex not found in $GameDataRoot"
}

$unifiedExe = Join-Path $GameDataRoot 'bin\majin.exe'
$buildUsa = Join-Path $projectRoot ('prototype-usa\out\build\win-amd64-' + $Configuration.ToLower())
$buildPal = Join-Path $projectRoot ('prototype\out\build\win-amd64-' + $Configuration.ToLower())
$prototypeExe = if (Test-Path (Join-Path $buildPal 'majin.exe')) { Join-Path $buildPal 'majin.exe' } else { Join-Path $buildUsa 'majin.exe' }
$build = if (Test-Path $buildPal) { $buildPal } else { $buildUsa }

$exe = if (Test-Path $unifiedExe) { $unifiedExe } elseif (Test-Path $prototypeExe) { $prototypeExe } else { $unifiedExe }
if (-not (Test-Path $exe)) {
    throw "Build majin.exe before running ($exe)."
}

$sessionRoot = Join-Path $GameDataRoot 'runtime'
if (-not (Test-Path $sessionRoot) -and (Test-Path (Join-Path $projectRoot 'private\runtime-usa'))) {
    $sessionRoot = Join-Path $projectRoot 'private\runtime-usa'
}
New-Item -ItemType Directory -Force -Path $sessionRoot | Out-Null

$isVulkan = ($RenderingPath -eq 'Vulkan' -or $RenderingPath -eq 'Vulkan_FSI' -or $RenderingPath -eq 'Vulkan_FBO')
$gpuBackend = if ($isVulkan) { 'vulkan' } else { 'd3d12' }
$cacheKey = $gpuBackend + '-' + $RenderingPath.ToLower() + '/' + $Configuration.ToLower()
$cacheRoot = Join-Path $sessionRoot ('cache/' + $cacheKey)
$metadataRoot = Join-Path $sessionRoot ('metadata/' + $cacheKey)
New-Item -ItemType Directory -Force -Path $cacheRoot, $metadataRoot, "$sessionRoot/user" | Out-Null

$renderTarget = if ($RenderingPath -eq 'ROV') { 'rov' } elseif ($RenderingPath -eq 'RTV') { 'rtv' } else { 'rtv' }
$vulkanTargetPath = 'fbo'
$allowInvalid = 'true'
$logDir = Join-Path $sessionRoot 'logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$logPath = Join-Path $logDir ('runtime-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')

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
        "--render_target_path_vulkan=$vulkanTargetPath",
        '--vulkan_dynamic_rendering=false',
        '--vulkan_async_skip_incomplete_frames=false'
    )
} else {
    $launchArgs += "--render_target_path_d3d12=$renderTarget"
}
if (-not $isVulkan) {
    $launchArgs += '--d3d12_allow_variable_refresh_rate_and_tearing=true'
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
$is60fps = ($FrameRateMode -eq '60' -or $FrameRateMode -eq 'Uncapped')
$launchArgs += "--majin_60fps=$($is60fps.ToString().ToLowerInvariant())"
$isSkipCutscenes = ConvertTo-Bool $SkipCutscenes
$launchArgs += "--majin_skip_cutscenes=$($isSkipCutscenes.ToString().ToLowerInvariant())"
$launchArgs += "--majin_cutscene_speed=$CutsceneSpeed"
$isSkipIntroLogos = ConvertTo-Bool $SkipIntroLogos
$launchArgs += "--majin_skip_intro_logos=$($isSkipIntroLogos.ToString().ToLowerInvariant())"

$langMap = @{
    'English'    = 1
    'Japanese'   = 2
    'German'     = 3
    'French'     = 4
    'Spanish'    = 5
    'Italian'    = 6
    'Korean'     = 7
    'Portuguese' = 9
}
$langId = if ($langMap.ContainsKey($Language)) { $langMap[$Language] } else { 1 }
$launchArgs += "--user_language=$langId"

# Engine Fixes & Enhancements
$isStepFix = ConvertTo-Bool $StepFix
$launchArgs += "--majin_step_fix=$($isStepFix.ToString().ToLowerInvariant())"

$isAnimSpeedFix = ConvertTo-Bool $AnimSpeedFix
$launchArgs += "--majin_submotion_fix=$($isAnimSpeedFix.ToString().ToLowerInvariant())"

$isDtSmoothing = ConvertTo-Bool $DtSmoothing
$launchArgs += "--majin_dt_smooth=$($isDtSmoothing.ToString().ToLowerInvariant())"

$launchArgs += "--majin_fov_scale=$($FovScale.ToString([Globalization.CultureInfo]::InvariantCulture))"

$isPauseBlur = ConvertTo-Bool $PauseBlur
$launchArgs += "--majin_pause_blur=$($isPauseBlur.ToString().ToLowerInvariant())"

$isDof = ConvertTo-Bool $DepthOfField
$launchArgs += "--majin_dof=$($isDof.ToString().ToLowerInvariant())"

$isBloom = ConvertTo-Bool $Bloom
$launchArgs += "--majin_bloom=$($isBloom.ToString().ToLowerInvariant())"

$isAutoCapture = ConvertTo-Bool $AutoCapture
if ($isAutoCapture) {
    $launchArgs += "--majin_auto_capture=true"
}

if ($Preview) {
    Write-Output "PREVIEW OK: $exe $($launchArgs -join ' ')"
    return
}

Write-Output "Runtime log: $logPath"
$quotedArgs = @($launchArgs | ForEach-Object {
    '"' + (($_ -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"'
})

Write-Output "Iniciando majin.exe ($WindowSize, Fullscreen=$isFullscreen, InputMode=$InputMode, FrameRate=$FrameRateMode)..."
$workingDir = if (Test-Path (Split-Path $exe -Parent)) {
    Split-Path $exe -Parent
} elseif (Test-Path $build) {
    $build
} else {
    $projectRoot
}
$process = Start-Process -FilePath $exe -ArgumentList $quotedArgs -WorkingDirectory $workingDir -WindowStyle Normal -PassThru -Wait
Write-Output "majin.exe encerrado com ExitCode: $($process.ExitCode)"
