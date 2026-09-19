param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$RunTests
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$source = Join-Path $root "Tools\Animation"
$build = Join-Path $root "Build\ozz-animation"
$ozzSource = Join-Path $root "Library\ozz-animation"
$expectedOzzCommit = "744eb9d99f606eda849acb0b1204f7a3dc20bca1"

if (-not (Test-Path (Join-Path $ozzSource "CMakeLists.txt"))) {
    throw "ozz-animation is not initialized. Run: git submodule update --init Library/ozz-animation"
}

$actualOzzCommit = (& git -C $ozzSource rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualOzzCommit -ne $expectedOzzCommit) {
    throw "ozz-animation must be checked out at $expectedOzzCommit (found '$actualOzzCommit')."
}

& cmake -S $source -B $build -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& cmake --build $build --config $Configuration --target AnimationOzzRoundTripTests ComponentSerializationRoundTripTests --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($RunTests) {
    & ctest --test-dir $build -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
