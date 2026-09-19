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

if (-not (Test-Path (Join-Path $ozzSource "CMakeLists.txt"))) {
    throw "ozz-animation is not initialized."
}

$actualOzzCommit = (& git -C $ozzSource rev-parse HEAD).Trim()

if ($LASTEXITCODE -ne 0) {
    throw "Failed to get ozz-animation git commit."
}

if ($actualOzzCommit -ne $expectedOzzCommit) {
    throw "Invalid ozz-animation commit. Expected '$expectedOzzCommit', found '$actualOzzCommit'."
}

Write-Host "=== CMake Configure ==="

& cmake `
    -S $source `
    -B $build `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -D "ozz_build_tests=OFF" `
    -D "ozz_build_samples=OFF" `
    -D "ozz_build_howtos=OFF" `
    -D "ozz_build_tools=OFF" `
    -D "ozz_build_fbx=OFF" `
    -D "ozz_build_gltf=OFF" `
    -D "ozz_build_data=OFF" `
    -D "ozz_build_msvc_rt_dll=OFF"

if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

Write-Host "=== Build ozz-animation ==="

& cmake `
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