[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$MSBuild = ""
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$MSBuild = & (Join-Path $root "Tools\Textures\ResolveMSBuild.ps1") -MSBuild $MSBuild
$output = Join-Path $root "x64\$Configuration\RuntimeTests"
$intermediate = Join-Path $root "GameEngine\x64\$Configuration\RuntimeTests"

Push-Location $root
try {
    & $MSBuild (Join-Path $root "GameEngine.vcxproj") /m /nologo /v:minimal `
        /p:Configuration=$Configuration /p:Platform=x64 /p:GameSessionTests=true `
        "/p:SolutionDir=$root\" "/p:OutDir=$output\" "/p:IntDir=$intermediate\" `
        /p:TargetName=GameSessionTests
    if ($LASTEXITCODE -ne 0) { throw "Runtime/Play test build failed." }
    & (Join-Path $output "GameSessionTests.exe")
    if ($LASTEXITCODE -ne 0) { throw "Runtime/Play tests failed." }
} finally {
    Pop-Location
}
