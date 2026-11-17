param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('1280x720','1920x1080','2560x1440','3840x2160')][string]$WindowSize = '1280x720',
    $Fullscreen = $false,
    $VSync = $true,
    [ValidateRange(1,3)][int]$RenderScale = 1,
    [ValidateSet('none','fxaa','fxaa_extreme','dedither','dedither_fxaa','dedither_fxaa_extreme','dedither_debug')][string]$PostEffect = 'none',
    [ValidateRange(0.0, 1.0)][double]$DeDitherStrength = 0.85,
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
    $AnimSpeedFix = $false,
    $DtSmoothing = $false,
    [ValidateRange(0.5, 2.0)][double]$FovScale = 1.0,
    [ValidateRange(0.5, 3.0)][double]$DrawDistanceScale = 1.0,
    $PauseBlur = $false,
    $PauseDistortion = $true,
    $DepthOfField = $false,
    $Bloom = 1.0,
    [ValidateRange(0.0, 2.0)][double]$BloomIntensity = 1.0,
    [ValidateRange(0.0, 1.0)][double]$CasSharpness = 0.0,
    [ValidateRange(0.0, 2.0)][double]$FsrReduction = 0.0,
    $WindowFps = $null,
    $AutoCapture = $false,
    $PresentDither = $false,
    [string]$GameDataRoot = 'game-data',
    [switch]$ShaderPrebuildOnly,
    [switch]$DisableLogging,
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
if (-not $DisableLogging) {
    New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot 'reports') | Out-Null
}

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

# Detect target edition from default.xex (PAL: 67BD87EF, USA: 76FDB642)
$xexPath = Join-Path $GameDataRoot 'default.xex'
$isPal = $false
$editionSuffix = 'usa'
$editionName = 'USA / NTSC-U (0.0.0.2)'
if (Test-Path $xexPath) {
    try {
        $xexBytes = [System.IO.File]::ReadAllBytes($xexPath)
        $maxScan = [Math]::Min($xexBytes.Length, 16384)
        for ($i = 0; $i -lt ($maxScan - 4); $i++) {
            if ($xexBytes[$i] -eq 0x67 -and $xexBytes[$i+1] -eq 0xBD -and $xexBytes[$i+2] -eq 0x87 -and $xexBytes[$i+3] -eq 0xEF) {
                $isPal = $true
                $editionSuffix = 'pal'
                $editionName = 'Europe / PAL (0.0.0.3)'
                break
            }
        }
    } catch {
        Write-Warning "Could not inspect default.xex for edition identification: $_"
    }
}
Write-Output "Detected target game edition: $editionName ($editionSuffix)"

$binDir = Join-Path $GameDataRoot 'bin'
$targetExe = Join-Path $binDir 'majin.exe'
$editionExe = Join-Path $binDir "majin-$editionSuffix.exe"
$buildPal = Join-Path $projectRoot ('prototype\out\build\win-amd64-' + $Configuration.ToLower())
$buildUsa = Join-Path $projectRoot ('prototype-usa\out\build\win-amd64-' + $Configuration.ToLower())
$buildEdition = if ($isPal) { Join-Path $buildPal 'majin.exe' } else { Join-Path $buildUsa 'majin.exe' }
$build = if ($isPal) { $buildPal } else { $buildUsa }

if (Test-Path $editionExe) {
    if (-not (Test-Path $targetExe) -or ((Get-Item $targetExe).Length -ne (Get-Item $editionExe).Length)) {
        Copy-Item -LiteralPath $editionExe -Destination $targetExe -Force
        Write-Output "Deployed precompiled game binary for $editionName (majin-$editionSuffix.exe -> majin.exe)"
    }
    $exe = $targetExe
} elseif (Test-Path $buildEdition) {
    $exe = $buildEdition
} elseif (Test-Path $targetExe) {
    $exe = $targetExe
} else {
    throw "Game executable not found ($targetExe). Please build or unpack game first."
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
$logPath = $null
if (-not $DisableLogging) {
    $logDir = Join-Path $sessionRoot 'logs'
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    $logPath = Join-Path $logDir ('runtime-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
}

$dimensions = $WindowSize.Split('x')
$launchArgs = @(
    '--gpu_plugin', 'xenos',
    "--gpu_backend=$gpuBackend",
    '--game_data_root', $GameDataRoot,
    '--user_data_root', "$sessionRoot/user",
    '--cache_root', $cacheRoot,
    '--metadata_root', $metadataRoot
)
if ($logPath) {
    $launchArgs += @('--log_file', $logPath)
} else {
    $launchArgs += @('--log_file=', '--flush_log=false')
}
$launchArgs += @(
    "--window_width=$($dimensions[0])",
    "--window_height=$($dimensions[1])",
    "--fullscreen=$($isFullscreen.ToString().ToLowerInvariant())",
    "--vsync=$($isVSync.ToString().ToLowerInvariant())",
    "--resolution_scale=$RenderScale",
    "--swap_post_effect=$PostEffect",
    "--swap_dedither_strength=$($DeDitherStrength.ToString([Globalization.CultureInfo]::InvariantCulture))",
    "--present_effect=$Upscaler",
    "--present_dither=$((ConvertTo-Bool $PresentDither).ToString().ToLowerInvariant())",
    "--gpu_allow_invalid_fetch_constants=$allowInvalid"
)
if ($Upscaler -eq 'cas') {
    $launchArgs += "--present_cas_additional_sharpness=$($CasSharpness.ToString([Globalization.CultureInfo]::InvariantCulture))"
} elseif ($Upscaler -eq 'fsr') {
    $launchArgs += "--present_fsr_sharpness_reduction=$($FsrReduction.ToString([Globalization.CultureInfo]::InvariantCulture))"
}
if ($isVulkan) {
    $launchArgs += @(
        "--render_target_path_vulkan=$vulkanTargetPath",
        '--vulkan_dynamic_rendering=false',
        '--vulkan_async_skip_incomplete_frames=false'
    )
    if ($ShaderPrebuildOnly) {
        $launchArgs += '--shader_prebuild_only=true'
    }
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

$isWindowFps = if ($null -ne $WindowFps) { ConvertTo-Bool $WindowFps } else { (-not $isFullscreen) }
$launchArgs += "--majin_window_fps=$($isWindowFps.ToString().ToLowerInvariant())"
if ($isRecordPerf -and -not $DisableLogging -and $logPath) {
    $launchArgs += @('--majin_record_performance=true', "--perf_log_csv=$logPath.performance.csv")
} else {
    $launchArgs += '--majin_record_performance=false'
}
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
$launchArgs += "--majin_draw_distance_scale=$($DrawDistanceScale.ToString([Globalization.CultureInfo]::InvariantCulture))"

$isPauseDistortion = ConvertTo-Bool $PauseDistortion
$launchArgs += "--majin_pause_distortion=$($isPauseDistortion.ToString().ToLowerInvariant())"

$isDof = ConvertTo-Bool $DepthOfField
$launchArgs += "--majin_dof=$($isDof.ToString().ToLowerInvariant())"

# Handle Bloom intensity (accepts float directly or bool)
$bloomVal = 1.0
if ($BloomIntensity -ne 1.0) {
    $bloomVal = $BloomIntensity
} elseif ($Bloom -is [bool] -or $Bloom -eq 'true' -or $Bloom -eq 'false' -or $Bloom -eq '$true' -or $Bloom -eq '$false') {
    $bloomVal = if (ConvertTo-Bool $Bloom) { 1.0 } else { 0.0 }
} else {
    $parsed = 1.0
    if ([double]::TryParse("$Bloom", [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$parsed)) {
        $bloomVal = $parsed
    }
}
$launchArgs += "--majin_bloom=$($bloomVal.ToString([Globalization.CultureInfo]::InvariantCulture))"

$isAutoCapture = ConvertTo-Bool $AutoCapture
if ($isAutoCapture) {
    $launchArgs += "--majin_auto_capture=true"
}

if ($Preview) {
    Write-Output "PREVIEW OK: $exe $($launchArgs -join ' ')"
    return
}

if ($logPath) {
    Write-Output "Runtime log: $logPath"
}
$quotedArgs = @($launchArgs | ForEach-Object {
    '"' + (($_ -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"'
})

Write-Output "Starting majin.exe ($WindowSize, Fullscreen=$isFullscreen, InputMode=$InputMode, FrameRate=$FrameRateMode)..."
$workingDir = if (Test-Path (Split-Path $exe -Parent)) {
    Split-Path $exe -Parent
} elseif (Test-Path $build) {
    $build
} else {
    $projectRoot
}
$windowStyle = if ($ShaderPrebuildOnly) { 'Hidden' } else { 'Normal' }
$process = Start-Process -FilePath $exe -ArgumentList $quotedArgs -WorkingDirectory $workingDir -WindowStyle $windowStyle -PassThru -Wait
Write-Output "majin.exe exited with ExitCode: $($process.ExitCode)"
