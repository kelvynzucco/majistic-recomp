$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

New-Item -ItemType Directory -Force -Path (Join-Path $root 'launcher') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $root 'private\build\launcher') | Out-Null

. (Join-Path $PSScriptRoot 'vs-env.ps1')

$compiler = Join-Path $llvmBin 'clang-cl.exe'
$linker = Join-Path $llvmBin 'lld-link.exe'
$source = Join-Path $root 'launcher'
$output = Join-Path $root 'private\build\launcher'
$exe = Join-Path $root 'Majistic Launcher.exe'

Push-Location $source
try {
    Write-Output '[1/3] Compiling application manifest and version info (rc.exe)...'
    & rc.exe /nologo /i $source /fo (Join-Path $output 'launcher.res') 'resource.rc'
    if ($LASTEXITCODE -ne 0) { throw "rc.exe failed ($LASTEXITCODE)" }

    Write-Output '[2/3] Compiling main.cpp (clang-cl.exe)...'
    $object = Join-Path $output 'launcher.obj'
    & $compiler /nologo /c /O2 /MT /DNDEBUG /std:c++17 /EHsc /Brepro "/Fo$object" 'main.cpp'
    if ($LASTEXITCODE -ne 0) { throw "clang-cl.exe failed ($LASTEXITCODE)" }

    Write-Output '[3/3] Linking Majistic Launcher.exe (lld-link.exe)...'
    & $linker "/OUT:$exe" /SUBSYSTEM:WINDOWS /DEBUG:NONE /INCREMENTAL:NO /Brepro $object (Join-Path $output 'launcher.res') user32.lib shell32.lib
    if ($LASTEXITCODE -ne 0) { throw "lld-link.exe failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}

Write-Output "Validating Majistic Launcher.exe (--validate-only)..."
$proc = Start-Process -FilePath $exe -ArgumentList '--validate-only' -WorkingDirectory $root -PassThru -Wait
if ($proc.ExitCode -ne 0) {
    throw "Majistic Launcher.exe validation failed with exit code $($proc.ExitCode)"
}
Write-Output "SUCCESS: Majistic Launcher.exe built and validated at: $exe"
