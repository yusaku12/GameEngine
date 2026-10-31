param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$source = Join-Path $root "Tools\Animation"
$build = Join-Path $root "Build\ozz-animation"
$ozzSource = Join-Path $root "Library\ozz-animation"

$expectedOzzCommit = "744eb9d99f606eda849acb0b1204f7a3dc20bca1"
$vswherePath = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)) "Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path $vswherePath)) {
    throw "Visual Studio Installer's vswhere.exe was not found at '$vswherePath'."
}

$visualStudioPath = (& $vswherePath -latest -products "*" -property installationPath).Trim()
if ($LASTEXITCODE -ne 0 -or -not $visualStudioPath) {
    throw "A Visual Studio installation was not found."
}

$visualStudioVersion = (& $vswherePath -latest -products "*" -property installationVersion).Trim()
if ($LASTEXITCODE -ne 0 -or -not $visualStudioVersion.StartsWith("18.")) {
    throw "Visual Studio 2026 is required by the v145 project toolset."
}

$cmakePath = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
if (-not $cmakePath) {
    $cmakePath = Join-Path $visualStudioPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
if (-not (Test-Path $cmakePath)) {
    throw "CMake was not found in PATH or the Visual Studio installation."
}

$gitPath = (Get-Command git.exe -ErrorAction SilentlyContinue).Source
if (-not $gitPath) {
    $gitPath = Join-Path $visualStudioPath "Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe"
}
if (-not (Test-Path $gitPath)) {
    throw "Git was not found in PATH or the Visual Studio installation."
}

function Get-CMakeCacheValue {
    param(
        [string] $CachePath,
        [string] $Name
    )

    $line = Get-Content -LiteralPath $CachePath | Where-Object { $_.StartsWith("${Name}:") } | Select-Object -First 1
    if ($line) {
        return $line.Substring($line.IndexOf("=") + 1)
    }

    return $null
}

function Get-NormalizedPath {
    param([string] $Path)

    return [System.IO.Path]::GetFullPath($Path.Replace("/", "\")).TrimEnd("\")
}

if (-not (Test-Path (Join-Path $ozzSource "CMakeLists.txt"))) {
    throw "ozz-animation is not initialized."
}

$actualOzzCommit = (& $gitPath -C $ozzSource rev-parse HEAD).Trim()

if ($LASTEXITCODE -ne 0) {
    throw "Failed to get ozz-animation git commit."
}

if ($actualOzzCommit -ne $expectedOzzCommit) {
    throw "Invalid ozz-animation commit. Expected '$expectedOzzCommit', found '$actualOzzCommit'."
}

Write-Host "=== CMake Configure ==="

$cmakeCachePath = Join-Path $build "CMakeCache.txt"
$needsFreshConfigure = $false
if (Test-Path $cmakeCachePath) {
    $cachedSource = Get-CMakeCacheValue $cmakeCachePath "CMAKE_HOME_DIRECTORY"
    $cachedBuild = Get-CMakeCacheValue $cmakeCachePath "CMAKE_CACHEFILE_DIR"
    $cachedGenerator = Get-CMakeCacheValue $cmakeCachePath "CMAKE_GENERATOR"
    $cachedInstance = Get-CMakeCacheValue $cmakeCachePath "CMAKE_GENERATOR_INSTANCE"

    $needsFreshConfigure =
        -not $cachedSource -or
        -not $cachedBuild -or
        -not $cachedGenerator -or
        -not $cachedInstance -or
        (Get-NormalizedPath $cachedSource) -ne (Get-NormalizedPath $source) -or
        (Get-NormalizedPath $cachedBuild) -ne (Get-NormalizedPath $build) -or
        $cachedGenerator -ne "Visual Studio 18 2026" -or
        (Get-NormalizedPath $cachedInstance) -ne (Get-NormalizedPath $visualStudioPath)
}

$cmakeArguments = @(
    "-S"
    $source
    "-B"
    $build
    "-G"
    "Visual Studio 18 2026"
    "-A"
    "x64"
    "-D"
    "ozz_build_tests=OFF"
    "-D"
    "ozz_build_samples=OFF"
    "-D"
    "ozz_build_howtos=OFF"
    "-D"
    "ozz_build_tools=OFF"
    "-D"
    "ozz_build_fbx=OFF"
    "-D"
    "ozz_build_gltf=OFF"
    "-D"
    "ozz_build_data=OFF"
    "-D"
    "ozz_build_msvc_rt_dll=OFF"
)
if ($needsFreshConfigure) {
    $cmakeArguments = @("--fresh") + $cmakeArguments
}

& $cmakePath @cmakeArguments
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

Write-Host "=== Build ozz-animation ==="

& $cmakePath `
    --build $build `
    --config $Configuration `
    --parallel

if ($LASTEXITCODE -ne 0) {
    throw "ozz-animation build failed."
}

Write-Host ""
Write-Host "========================================"
Write-Host " ozz-animation build succeeded."
Write-Host " Tests are disabled."
Write-Host "========================================"