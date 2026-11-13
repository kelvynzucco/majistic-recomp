$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = Join-Path $root 'tools\source\rexglue-sdk'
$patchRoot = Join-Path $root 'patches\rexglue-0.10.0'
$revision = '546a082a4356173635f6c40510a41ff53e518b5a'

function Check-Native($step) {
    if ($LASTEXITCODE -ne 0) { throw "$step failed (exit $LASTEXITCODE)." }
}

New-Item -ItemType Directory -Force -Path (Split-Path $source -Parent) | Out-Null

# 1.1: Clone rexglue-sdk v0.10.0
if (!(Test-Path "$source\.git")) {
    Write-Host "[1.1] Cloning rexglue-sdk v0.10.0..."
    git clone --branch v0.10.0 --depth 1 https://github.com/rexglue/rexglue-sdk.git $source
    Check-Native 'SDK clone'
}
$head = (git -C $source rev-parse HEAD).Trim()
Check-Native 'SDK revision check'
if ($head -ne $revision) {
    throw "SDK revision mismatch; expected $revision, found $head."
}
Write-Host "[1.1] Verified rexglue-sdk HEAD = $head"

# 1.2: Initialize 14 submodules & materialize libmspack Windows symlinks
$modules = @('cli11','libmspack','FFmpeg','tomlplusplus','simde','xxHash','spdlog','fmt','utfcpp','imgui','sdl3','tracy','o1heap','inja')
foreach ($name in $modules) {
    $expected = (git -C $source ls-tree HEAD "thirdparty/$name") -split '\s+'
    Check-Native "Submodule reference $name"
    $module = Join-Path $source "thirdparty\$name"
    $actual = ''
    if (Test-Path "$module\.git") {
        $actual = (git -C $module rev-parse HEAD).Trim()
    }
    if ($actual -ne $expected[2]) {
        Write-Host "[1.2] Initializing submodule thirdparty/$name ($($expected[2]))..."
        git -C $source submodule update --init --depth 1 -- "thirdparty/$name"
        Check-Native "Dependency $name"
    }
}

$mspack = Join-Path $source 'thirdparty\libmspack'
git -C $mspack ls-files -s | ForEach-Object {
    if ($_ -match '^120000 .*\t(.+)$') {
        $dest = Join-Path $mspack $Matches[1]
        $item = Get-Item -LiteralPath $dest
        if (!($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -and $item.Length -lt 256) {
            $relative = (Get-Content -LiteralPath $dest -Raw).Trim()
            $target = [IO.Path]::GetFullPath((Join-Path (Split-Path $dest -Parent) $relative))
            Copy-Item -LiteralPath $target -Destination $dest -Force
            Write-Host "[1.2] Materialized symlink placeholder: $($Matches[1])"
        }
    }
}

# 1.3: Apply reference patches idempotently
$fpsPatch = Join-Path $patchRoot 'fps60.patch'
git -C $source apply --reverse --check $fpsPatch 2>$null
if ($LASTEXITCODE -eq 0) {
    git -C $source apply --reverse $fpsPatch
    Check-Native 'Prior timing patch removal'
}

$xmaPatch = Join-Path $patchRoot 'xma-context.patch'
git -C $source apply --reverse --check $xmaPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $xmaPatch
    Check-Native 'XMA patch check'
    git -C $source apply $xmaPatch
    Check-Native 'XMA patch application'
}
Copy-Item (Join-Path $patchRoot 'empty_packet_recovery.h') "$source\src\audio\empty_packet_recovery.h" -Force
Copy-Item (Join-Path $patchRoot 'empty_packet_recovery_test.cpp') "$source\src\audio\empty_packet_recovery_test.cpp" -Force

$loggingPatch = Join-Path $patchRoot 'texture-warning-logging.patch'
git -C $source apply --reverse --check $loggingPatch 2>$null
if ($LASTEXITCODE -ne 0 -and !([IO.File]::ReadAllText((Join-Path $source 'src\core\logging.cpp')).Contains('if (name == "gpu") sinks = {std::make_shared<majin::TextureWarningSink>(std::move(sinks))};'))) {
    git -C $source apply --check $loggingPatch
    Check-Native 'Texture logging patch check'
    git -C $source apply $loggingPatch
    Check-Native 'Texture logging patch application'
}

$flushPatch = Join-Path $patchRoot 'texture-warning-flush.patch'
git -C $source apply --reverse --check $flushPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $flushPatch
    Check-Native 'Texture flush patch check'
    git -C $source apply $flushPatch
    Check-Native 'Texture flush patch application'
}
Copy-Item -LiteralPath (Join-Path $patchRoot 'texture_warning_sink.h') -Destination (Join-Path $source 'src\core\texture_warning_sink.h') -Force

Copy-Item -LiteralPath (Join-Path $patchRoot 'release_frame_timing.h') -Destination (Join-Path $source 'src\graphics\release_frame_timing.h') -Force
$framePatch = Join-Path $patchRoot 'release-frame-capture.patch'
git -C $source apply --reverse --check $framePatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $framePatch
    Check-Native 'Release frame capture patch check'
    git -C $source apply $framePatch
    Check-Native 'Release frame capture patch application'
}

git -C $source apply --check $fpsPatch
Check-Native '60 FPS patch check'
git -C $source apply $fpsPatch
Check-Native '60 FPS patch application'
Copy-Item -LiteralPath (Join-Path $patchRoot 'frame_deadline.h') -Destination (Join-Path $source 'src\graphics\frame_deadline.h') -Force
Copy-Item -LiteralPath (Join-Path $patchRoot 'frame_deadline.h') -Destination (Join-Path $source 'src\ui\d3d12\frame_deadline.h') -Force

# 1.4: Apply D3D12 startup PSO precompilation patch
$psoPatch = Join-Path $patchRoot 'd3d12-blocking-pso-precompile.patch'
git -C $source apply --reverse --check $psoPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $psoPatch
    Check-Native 'D3D12 startup PSO precompile patch check'
    git -C $source apply $psoPatch
    Check-Native 'D3D12 startup PSO precompile patch application'
}

# 1.5: Apply D3D12 GPU optimizations and UI loop fix patch
$gpuOptPatch = Join-Path $patchRoot 'd3d12-gpu-and-ui-optimizations.patch'
git -C $source apply --reverse --check $gpuOptPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $gpuOptPatch
    Check-Native 'D3D12 GPU optimizations and UI loop fix patch check'
    git -C $source apply $gpuOptPatch
    Check-Native 'D3D12 GPU optimizations and UI loop fix patch application'
}

# 1.6: Apply SDL audio frequency-ratio patch (cutscene fast-forward audio)
$audioTurboPatch = Join-Path $patchRoot 'sdl-audio-turbo-frequency.patch'
git -C $source apply --reverse --check $audioTurboPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $audioTurboPatch
    Check-Native 'SDL audio turbo frequency patch check'
    git -C $source apply $audioTurboPatch
    Check-Native 'SDL audio turbo frequency patch application'
}

# 1.7: Enable the built-in spatial FSR 1 / CAS present effects (no external SDK)
$fsrPatch = Join-Path $patchRoot 'builtin-fsr1-cas.patch'
git -C $source apply --reverse --check $fsrPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $fsrPatch
    Check-Native 'Built-in FSR 1 / CAS patch check'
    git -C $source apply $fsrPatch
    Check-Native 'Built-in FSR 1 / CAS patch application'
}

# 1.8: Apply Vulkan command processor and render target fixes
$vulkanPatch = Join-Path $patchRoot 'vulkan-command-processor-fixes.patch'
git -C $source apply --reverse --check $vulkanPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $vulkanPatch
    Check-Native 'Vulkan command processor patch check'
    git -C $source apply $vulkanPatch
    Check-Native 'Vulkan command processor patch application'
}

# 1.9: Apply telemetry attribution fix and compile/wait metrics
$telemetryPatch = Join-Path $patchRoot 'telemetry-cost-attribution.patch'
git -C $source apply --reverse --check $telemetryPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $telemetryPatch
    Check-Native 'Telemetry cost attribution patch check'
    git -C $source apply $telemetryPatch
    Check-Native 'Telemetry cost attribution patch application'
}

# 1.10: Apply monolithic shared VkPipelineCache patch
$monolithicPatch = Join-Path $patchRoot 'vulkan-monolithic-pipeline-cache.patch'
git -C $source apply --reverse --check $monolithicPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $monolithicPatch
    Check-Native 'Monolithic pipeline cache patch check'
    git -C $source apply $monolithicPatch
    Check-Native 'Monolithic pipeline cache patch application'
}

# 1.11: Apply Extended Dynamic State (EDS 1, 2, 3) patch
$edsPatch = Join-Path $patchRoot 'vulkan-extended-dynamic-state.patch'
git -C $source apply --reverse --check $edsPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $edsPatch
    Check-Native 'Extended Dynamic State patch check'
    git -C $source apply $edsPatch
    Check-Native 'Extended Dynamic State patch application'
}

# 1.12: Apply Restricted 2-Layer Async Pipeline (M3) patch
$asyncPatch = Join-Path $patchRoot 'vulkan-restricted-async.patch'
git -C $source apply --reverse --check $asyncPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $asyncPatch
    Check-Native 'Restricted Async Pipeline patch check'
    git -C $source apply $asyncPatch
    Check-Native 'Restricted Async Pipeline patch application'
}

# 1.13: Apply Extended Dynamic State Invalidation and Barrier Fixes patch
$fixesPatch = Join-Path $patchRoot 'vulkan-dynamic-state-and-barrier-fixes.patch'
git -C $source apply --reverse --check $fixesPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $fixesPatch
    Check-Native 'Dynamic State and Barrier Fixes patch check'
    git -C $source apply $fixesPatch
    Check-Native 'Dynamic State and Barrier Fixes patch application'
}

# 1.14: Apply Non-volatile memory accessors template patch (Issue #17)
$nonVolatilePatch = Join-Path $patchRoot 'codegen-nonvolatile-memory-accessors.patch'
git -C $source apply --reverse --check $nonVolatilePatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $nonVolatilePatch
    Check-Native 'Non-volatile memory accessors patch check'
    git -C $source apply $nonVolatilePatch
    Check-Native 'Non-volatile memory accessors patch application'
}

# 1.15: Apply SharedMemory watch page bitmask spatial partitioning patch (Issue #22)
$watchBitmaskPatch = Join-Path $patchRoot 'shared-memory-watch-page-bitmask.patch'
git -C $source apply --reverse --check $watchBitmaskPatch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $watchBitmaskPatch
    Check-Native 'SharedMemory watch page bitmask patch check'
    git -C $source apply $watchBitmaskPatch
    Check-Native 'SharedMemory watch page bitmask patch application'
}

Write-Host "[1.1 - 1.15] ReXGlue SDK source checked out, submodules initialized, and all patches applied successfully!"
