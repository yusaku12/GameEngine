param(
    [Parameter()]
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$physxRoot = Join-Path $repositoryRoot "Library\PhysX\physx"
$buildDirectory = Join-Path $repositoryRoot "Build\PhysX"
$libraryDirectory = Join-Path $buildDirectory "lib"
$binaryDirectory = Join-Path $buildDirectory "bin"
$configurationLibraryDirectory = Join-Path $libraryDirectory "bin\win.x86_64.vc143.mt\$($Configuration.ToLowerInvariant())"

if (-not (Test-Path (Join-Path $physxRoot "CMakeLists.txt"))) {
    throw "PhysX SDK was not found at '$physxRoot'. Initialize the Library/PhysX submodule first."
}

New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null

& cmake `
    -S $physxRoot `
    -B $buildDirectory `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DPHYSX_PRESET=vc17win64-cpu-only `
    -DPX_GENERATE_GPU_PROJECTS=OFF `
    -DPX_GENERATE_STATIC_LIBRARIES=ON `
    -DPX_BUILDSNIPPETS=OFF `
    -DPX_BUILDPVDRUNTIME=OFF `
    -DNV_USE_STATIC_WINCRT=ON `
    "-DPX_OUTPUT_LIB_DIR=$libraryDirectory" `
    "-DPX_OUTPUT_BIN_DIR=$binaryDirectory" `
    "-DPX_OUTPUT_DLL_DIR=$binaryDirectory"
if ($LASTEXITCODE -ne 0) {
    throw "PhysX CMake project generation failed."
}

& cmake --build $buildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) {
    throw "PhysX $Configuration build failed."
}

$requiredLibraries = @(
    "PhysX_static_64.lib",
    "PhysXExtensions_static_64.lib",
    "PhysXCooking_static_64.lib",
    "PhysXCommon_static_64.lib",
    "PhysXPvdSDK_static_64.lib",
    "PhysXFoundation_static_64.lib"
)
$missingLibraries = $requiredLibraries | Where-Object {
    -not (Test-Path (Join-Path $configurationLibraryDirectory $_))
}
if ($missingLibraries.Count -gt 0) {
    throw "PhysX $Configuration build is missing required libraries: $($missingLibraries -join ', ')"
}
