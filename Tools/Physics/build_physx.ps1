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

if (-not (Test-Path (Join-Path $physxRoot "CMakeLists.txt"))) {
    throw "PhysX SDK was not found at '$physxRoot'. Initialize the Library/PhysX submodule first."
}

New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null

$cmakeCachePath = Join-Path $buildDirectory "CMakeCache.txt"
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
        (Get-NormalizedPath $cachedSource) -ne (Get-NormalizedPath $physxRoot) -or
        (Get-NormalizedPath $cachedBuild) -ne (Get-NormalizedPath $buildDirectory) -or
        $cachedGenerator -ne "Visual Studio 18 2026" -or
        (Get-NormalizedPath $cachedInstance) -ne (Get-NormalizedPath $visualStudioPath)
}

$cmakeArguments = @(
    "-S"
    $physxRoot
    "-B"
    $buildDirectory
    "-G"
    "Visual Studio 18 2026"
    "-A"
    "x64"
    "-DPHYSX_PRESET=vc17win64-cpu-only"
    "-DPX_GENERATE_GPU_PROJECTS=OFF"
    "-DPX_GENERATE_STATIC_LIBRARIES=ON"
    "-DPX_BUILDSNIPPETS=OFF"
    "-DPX_BUILDPVDRUNTIME=OFF"
    "-DNV_USE_STATIC_WINCRT=ON"
    "-DPX_OUTPUT_LIB_DIR=$libraryDirectory"
    "-DPX_OUTPUT_BIN_DIR=$binaryDirectory"
    "-DPX_OUTPUT_DLL_DIR=$binaryDirectory"
)
if ($needsFreshConfigure) {
    $cmakeArguments = @("--fresh") + $cmakeArguments
}

& $cmakePath @cmakeArguments
if ($LASTEXITCODE -ne 0) {
    throw "PhysX CMake project generation failed."
}

& $cmakePath --build $buildDirectory --config $Configuration --parallel
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
