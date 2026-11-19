# SPDX-License-Identifier: GPL-3.0-only
# Majistic Recomp - Automated Cutscene Shader Harvester & Merger Script
#
# Fluxo completo:
# 1. Backup de Seguranca dos caches de shader existentes (.xpso e .xsh)
# 2. Execucao automatizada das 82 cutscenes com majin.exe (--majin_harvest_demos=true)
# 3. Consolidacao e merge via merge_shaders.exe com relatorio comparativo

param(
    [ValidateRange(1.0, 16.0)][double]$HarvestSpeed = 5.0,
    [ValidateRange(1, 82)][int]$BatchSize = 1,
    [int]$MaxDemos = 0,
    [int]$StartIndex = 0,
    [string]$GameDataRoot = 'game-data',
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$NoMerge
)

$ErrorActionPreference = 'Stop'

Write-Host "=========================================================" -ForegroundColor Cyan
Write-Host "  Majin Recomp - Automated Cutscene Shader Harvester    " -ForegroundColor Cyan
Write-Host "=========================================================" -ForegroundColor Cyan

# ---------------------------------------------------------------------------
# 0. Resolucao de Diretorios e Binarios
# ---------------------------------------------------------------------------
$projectRoot = Split-Path $PSScriptRoot -Parent

if (-not [System.IO.Path]::IsPathRooted($GameDataRoot)) {
    $candidate = Join-Path $projectRoot $GameDataRoot
    if (-not (Test-Path $candidate) -and (Test-Path (Join-Path $projectRoot 'game-data-usa'))) {
        $candidate = Join-Path $projectRoot 'game-data-usa'
    }
    $GameDataRoot = [System.IO.Path]::GetFullPath($candidate)
}

if (-not (Test-Path (Join-Path $GameDataRoot 'default.xex'))) {
    throw "default.xex nao encontrado em $GameDataRoot"
}

$binDir = Join-Path $GameDataRoot 'bin'
$targetExe = Join-Path $binDir 'majin.exe'
$buildPal = Join-Path $projectRoot ('prototype\out\build\win-amd64-' + $Configuration.ToLower() + '\majin.exe')
$buildUsa = Join-Path $projectRoot ('prototype-usa\out\build\win-amd64-' + $Configuration.ToLower() + '\majin.exe')
$buildExe = if (Test-Path $buildPal) { $buildPal } else { $buildUsa }

# Garante que majin.exe esteja atualizado com o build mais recente
if (Test-Path $buildExe) {
    if (-not (Test-Path $targetExe) -or ((Get-Item $buildExe).LastWriteTime -gt (Get-Item $targetExe).LastWriteTime)) {
        Copy-Item -LiteralPath $buildExe -Destination $targetExe -Force
        Write-Host "[INIT] Binario atualizado em $targetExe a partir do build." -ForegroundColor Green
    }
    $exe = $targetExe
} elseif (Test-Path $targetExe) {
    $exe = $targetExe
} else {
    throw "Executavel do jogo nao encontrado em $targetExe. Compile o projeto primeiro."
}

$sessionRoot = Join-Path $GameDataRoot 'runtime'
$cacheKey = 'vulkan-vulkan/' + $Configuration.ToLower()
$cacheRoot = Join-Path $sessionRoot ('cache/' + $cacheKey)
$metadataRoot = Join-Path $sessionRoot ('metadata/' + $cacheKey)
$shaderDir = Join-Path $cacheRoot 'shaders/shareable'
$logDir = Join-Path $sessionRoot 'logs'

New-Item -ItemType Directory -Force -Path $shaderDir, $logDir, "$sessionRoot/user", $metadataRoot | Out-Null

$xpsoPath = Join-Path $shaderDir '4E4D0816.fbo.vk.xpso'
$xshPath  = Join-Path $shaderDir '4E4D0816.xsh'

# ---------------------------------------------------------------------------
# 1. Backup de Seguranca
# ---------------------------------------------------------------------------
Write-Host "`n[ETAPA 1/3] Backup de Seguranca dos Caches..." -ForegroundColor Cyan

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$xpsoInitialSize = 0
$psoBefore = 0
$xshInitialSize = 0

$xpsoBakPath = Join-Path $shaderDir "4E4D0816.fbo.vk.xpso.preharvest-$timestamp.bak"
$xshBakPath  = Join-Path $shaderDir "4E4D0816.xsh.preharvest-$timestamp.bak"
$xpsoCanonicalBak = Join-Path $shaderDir "4E4D0816.fbo.vk.xpso.preharvest.bak"
$xshCanonicalBak  = Join-Path $shaderDir "4E4D0816.xsh.preharvest.bak"

if (Test-Path $xpsoPath) {
    $xpsoInitialSize = (Get-Item $xpsoPath).Length
    if ($xpsoInitialSize -ge 20) {
        $psoBefore = [int](($xpsoInitialSize - 20) / 68)
    }
    Copy-Item -LiteralPath $xpsoPath -Destination $xpsoBakPath -Force
    Copy-Item -LiteralPath $xpsoPath -Destination $xpsoCanonicalBak -Force
    Write-Host "  -> Backup XPSO salvo: $xpsoBakPath" -ForegroundColor White
    Write-Host "  -> Contagem inicial de PSOs: $psoBefore ($xpsoInitialSize bytes)" -ForegroundColor White
} else {
    Write-Host "  -> Nenhum XPSO previo encontrado. Criando base limpa." -ForegroundColor Yellow
}

if (Test-Path $xshPath) {
    $xshInitialSize = (Get-Item $xshPath).Length
    Copy-Item -LiteralPath $xshPath -Destination $xshBakPath -Force
    Copy-Item -LiteralPath $xshPath -Destination $xshCanonicalBak -Force
    Write-Host "  -> Backup XSH salvo: $xshBakPath" -ForegroundColor White
    Write-Host "  -> Tamanho inicial do XSH: $([Math]::Round($xshInitialSize / 1KB, 2)) KB ($xshInitialSize bytes)" -ForegroundColor White
} else {
    Write-Host "  -> Nenhum XSH previo encontrado. Criando base limpa." -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 2. Execucao do Harvester em Lotes Seguros
# ---------------------------------------------------------------------------
$demoPackageDir = Join-Path $GameDataRoot 'finalizedXBox360/KB/demo/package'
if (-not (Test-Path $demoPackageDir)) {
    $demoPackageDir = Join-Path $projectRoot 'game-data/finalizedXBox360/KB/demo/package'
}
$demoFiles = Get-ChildItem -Path $demoPackageDir -Filter "demo*.pak" -ErrorAction SilentlyContinue
$totalAvailable = if ($demoFiles) { $demoFiles.Count } else { 82 }
$totalDemosToRun = if ($MaxDemos -gt 0) { [Math]::Min($totalAvailable - $StartIndex, $MaxDemos) } else { $totalAvailable - $StartIndex }
$endIndex = $StartIndex + $totalDemosToRun

Write-Host "`n[ETAPA 2/3] Executando Harvester de Cutscenes em Lotes ($totalDemosToRun cutscenes, offset $($StartIndex + 1) a $endIndex de $totalAvailable)..." -ForegroundColor Cyan

$workingDir = Split-Path $exe -Parent
$batchIndex = 0

for ($start = $StartIndex; $start -lt $endIndex; $start += $BatchSize) {
    $batchIndex++
    $count = [Math]::Min($BatchSize, $endIndex - $start)
    $batchEnd = $start + $count
    Write-Host "`n>>> [LOTE $batchIndex] Cutscenes $($start + 1) ate $batchEnd de $totalAvailable <<<" -ForegroundColor Cyan

    $runtimeLog = Join-Path $logDir ("harvest-$timestamp-batch$batchIndex.log")
    $launchArgs = @(
        '--gpu_plugin', 'xenos',
        '--gpu_backend=vulkan',
        '--game_data_root', $GameDataRoot,
        '--user_data_root', "$sessionRoot/user",
        '--cache_root', $cacheRoot,
        '--metadata_root', $metadataRoot,
        '--log_file', $runtimeLog,
        '--flush_log=true',
        '--window_width=1280',
        '--window_height=720',
        '--fullscreen=false',
        '--vsync=false',
        '--resolution_scale=1',
        '--swap_post_effect=none',
        '--swap_dedither_strength=0.85',
        '--present_effect=cas',
        '--present_dither=false',
        '--gpu_allow_invalid_fetch_constants=true',
        '--render_target_path_vulkan=fbo',
        '--vulkan_dynamic_rendering=false',
        '--vulkan_async_skip_incomplete_frames=false',
        '--async_shader_compilation=false',
        '--majin_window_fps=false',
        '--majin_record_performance=false',
        '--majin_60fps=true',
        '--majin_skip_cutscenes=false',
        '--majin_skip_intro_logos=true',
        '--user_language=1',
        '--majin_step_fix=false',
        '--majin_submotion_fix=false',
        '--majin_dt_smooth=false',
        '--majin_fov_scale=1.0',
        '--majin_draw_distance_scale=1.0',
        '--majin_pause_distortion=false',
        '--majin_dof=true',
        '--majin_bloom=1.0',
        '--majin_harvest_demos=true',
        "--majin_harvest_speed=$HarvestSpeed",
        "--majin_harvest_start=$start",
        "--majin_harvest_count=$count"
    )

    $quotedArgs = @($launchArgs | ForEach-Object {
        '"' + (($_ -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"'
    })

    $process = Start-Process -FilePath $exe -ArgumentList $quotedArgs -WorkingDirectory $workingDir -PassThru

    $lastLogPos = 0
    while (-not $process.HasExited) {
        if (Test-Path $runtimeLog) {
            try {
                $stream = [System.IO.File]::Open($runtimeLog, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
                if ($stream.Length -gt $lastLogPos) {
                    $stream.Seek($lastLogPos, [System.IO.SeekOrigin]::Begin) | Out-Null
                    $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
                    while (-not $reader.EndOfStream) {
                        $line = $reader.ReadLine()
                        if ($line -match '\[HARVEST\]') {
                            Write-Host $line -ForegroundColor Green
                        } elseif ($line -match '\[error\]|\[critical\]|FATAL') {
                            Write-Host $line -ForegroundColor Red
                        } elseif ($line -match '\[warn\]') {
                            Write-Host $line -ForegroundColor Yellow
                        }
                    }
                    $lastLogPos = $stream.Position
                    $reader.Close()
                }
                $stream.Close()
            } catch { }
        }
        Start-Sleep -Milliseconds 250
    }

    if (Test-Path $runtimeLog) {
        try {
            $stream = [System.IO.File]::Open($runtimeLog, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
            if ($stream.Length -gt $lastLogPos) {
                $stream.Seek($lastLogPos, [System.IO.SeekOrigin]::Begin) | Out-Null
                $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
                while (-not $reader.EndOfStream) {
                    $line = $reader.ReadLine()
                    if ($line -match '\[HARVEST\]') {
                        Write-Host $line -ForegroundColor Green
                    }
                }
                $reader.Close()
            }
            $stream.Close()
        } catch { }
    }

    Write-Host "`n[LOTE $batchIndex] Finalizado com ExitCode: $($process.ExitCode)" -ForegroundColor $(if ($process.ExitCode -eq 0) { 'Cyan' } else { 'Yellow' })
}

if ($NoMerge) {
    Write-Host "Parametro -NoMerge especificado. Encerrando sem consolidacao." -ForegroundColor Yellow
    return
}

# ---------------------------------------------------------------------------
# 3. Consolidacao via merge_shaders.exe e Relatorio
# ---------------------------------------------------------------------------
Write-Host "`n[ETAPA 3/3] Consolidacao via merge_shaders.exe..." -ForegroundColor Cyan

$mergeShadersDir = Join-Path $projectRoot 'tools\merge-shaders'
$mergeShadersExe = Join-Path $mergeShadersDir 'merge_shaders.exe'
$mergeShadersSrc = Join-Path $mergeShadersDir 'merge_shaders.cpp'

if (-not (Test-Path $mergeShadersExe)) {
    Write-Host "  merge_shaders.exe nao encontrado. Tentando compilar..." -ForegroundColor Yellow
    $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    if (Test-Path $vcvars) {
        $sdkInclude = Join-Path $projectRoot 'tools\sdk\win-amd64\include'
        $objPath = Join-Path $mergeShadersDir 'merge_shaders.obj'
        cmd /c "call `"$vcvars`" >nul && cl /nologo /EHsc /O2 /std:c++20 /I`"$sdkInclude`" `"$mergeShadersSrc`" /Fe`"$mergeShadersExe`" /Fo`"$objPath`" && del `"$objPath`""
    }
    if (-not (Test-Path $mergeShadersExe)) {
        throw "Nao foi possivel compilar ou localizar merge_shaders.exe em $mergeShadersDir"
    }
    Write-Host "  merge_shaders.exe compilado com sucesso." -ForegroundColor Green
}

# Função auxiliar para cópia segura com retentativas (evita colisão de handle no Windows)
function Safe-CopyFile([string]$Source, [string]$Dest) {
    if (-not (Test-Path $Source)) { return }
    for ($attempt = 1; $attempt -le 10; $attempt++) {
        try {
            Copy-Item -LiteralPath $Source -Destination $Dest -Force -ErrorAction Stop
            return
        } catch {
            Start-Sleep -Milliseconds 300
        }
    }
    Copy-Item -LiteralPath $Source -Destination $Dest -Force
}

# Aguarda liberação completa de handles pelo kernel do Windows
Start-Sleep -Milliseconds 1000

# Preserva copia bruta do que o harvester acabou de gerar antes do merge
$xshHarvestedPath  = Join-Path $shaderDir "4E4D0816.xsh.harvested-$timestamp"
$xpsoHarvestedPath = Join-Path $shaderDir "4E4D0816.fbo.vk.xpso.harvested-$timestamp"

Safe-CopyFile -Source $xshPath -Dest $xshHarvestedPath
Safe-CopyFile -Source $xpsoPath -Dest $xpsoHarvestedPath

$baseXsh  = if (Test-Path $xshBakPath)  { $xshBakPath }  else { $xshPath }
$baseXpso = if (Test-Path $xpsoBakPath) { $xpsoBakPath } else { $xpsoPath }
$newXsh   = if (Test-Path $xshHarvestedPath)  { $xshHarvestedPath }  else { $xshPath }
$newXpso  = if (Test-Path $xpsoHarvestedPath) { $xpsoHarvestedPath } else { $xpsoPath }

$mergeArgs = @(
    "--base-xsh=$baseXsh",
    "--base-xpso=$baseXpso",
    "--new-xsh=$newXsh",
    "--new-xpso=$newXpso",
    "--out-xsh=$xshPath",
    "--out-xpso=$xpsoPath"
)

Write-Host "  Executando merge_shaders.exe..." -ForegroundColor White
$mergeProcess = Start-Process -FilePath $mergeShadersExe -ArgumentList $mergeArgs -WorkingDirectory $projectRoot -PassThru -Wait
if ($mergeProcess.ExitCode -ne 0) {
    Write-Warning "merge_shaders.exe retornou codigo de saida: $($mergeProcess.ExitCode)"
}

# ---------------------------------------------------------------------------
# Relatorio Final Comparativo
# ---------------------------------------------------------------------------
$psoAfter = 0
$xshFinalSize = 0

if (Test-Path $xpsoPath) {
    $xpsoFinalSize = (Get-Item $xpsoPath).Length
    if ($xpsoFinalSize -ge 20) {
        $psoAfter = [int](($xpsoFinalSize - 20) / 68)
    }
}
if (Test-Path $xshPath) {
    $xshFinalSize = (Get-Item $xshPath).Length
}

$psoDiff = $psoAfter - $psoBefore
$xshDiff = $xshFinalSize - $xshInitialSize

Write-Host "`n=========================================================" -ForegroundColor Green
Write-Host "            RELATORIO DO SHADER HARVEST                  " -ForegroundColor Green
Write-Host "=========================================================" -ForegroundColor Green
Write-Host ("  PSOs antes do harvest:     {0,5}" -f $psoBefore) -ForegroundColor White
Write-Host ("  PSOs apos o harvest:      {0,5} (+{1} novos PSOs consolidados)" -f $psoAfter, $psoDiff) -ForegroundColor $(if ($psoDiff -gt 0) { 'Green' } else { 'Yellow' })
Write-Host ("  Tamanho do .xsh antes:    {0,8:N2} KB ({1} bytes)" -f ($xshInitialSize / 1KB), $xshInitialSize) -ForegroundColor White
Write-Host ("  Tamanho do .xsh apos:     {0,8:N2} KB ({1} bytes) (+{2:N2} KB)" -f ($xshFinalSize / 1KB), $xshFinalSize, ($xshDiff / 1KB)) -ForegroundColor $(if ($xshDiff -gt 0) { 'Green' } else { 'Yellow' })
Write-Host "=========================================================`n" -ForegroundColor Green
