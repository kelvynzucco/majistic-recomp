# Shared toolchain discovery for build scripts.
# Locates any Visual Studio 2022 edition (Community, Professional, Enterprise or
# Build Tools) via vswhere, enters its x64 developer shell, and puts Clang/LLVM,
# CMake and Ninja on PATH. Clang is taken from the VS-bundled LLVM component
# when installed, otherwise from a standalone LLVM install (e.g. LLVM 20.1.8).
# Exposes $vsRoot and $llvmBin to the dot-sourcing script.

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = $null
if (Test-Path $vswhere) {
    $vsRoot = & $vswhere -latest -products * -version '[17.0,18.0)' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
        Select-Object -First 1
}
if (-not $vsRoot -or -not (Test-Path $vsRoot)) {
    throw 'Visual Studio 2022 with C++ x64 tools not found (Community or Build Tools).'
}

& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

$llvmBin = Join-Path $vsRoot 'VC\Tools\Llvm\x64\bin'
if (-not (Test-Path (Join-Path $llvmBin 'clang-cl.exe'))) {
    $llvmBin = Join-Path $env:ProgramFiles 'LLVM\bin'
}
if (-not (Test-Path (Join-Path $llvmBin 'clang-cl.exe'))) {
    throw 'clang-cl.exe not found (install the VS Clang component or LLVM for Windows).'
}

$cmakeRoot = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$extraPaths = @($llvmBin, (Join-Path $cmakeRoot 'CMake\bin'), (Join-Path $cmakeRoot 'Ninja')) |
    Where-Object { Test-Path $_ }
$env:PATH = (($extraPaths + $env:PATH) -join ';')

foreach ($tool in 'cmake', 'ninja', 'clang++', 'clang-cl') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool not found on PATH." }
}
