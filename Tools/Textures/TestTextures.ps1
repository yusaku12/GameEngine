[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$MSBuild = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$MSBuild = & (Join-Path $PSScriptRoot "ResolveMSBuild.ps1") -MSBuild $MSBuild
& (Join-Path $PSScriptRoot "BuildTexconv.ps1") -MSBuild $MSBuild
& $MSBuild (Join-Path $PSScriptRoot "Tests\TextureTests.vcxproj") /nologo /m /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 /verbosity:minimal
if ($LASTEXITCODE -ne 0) { throw "Texture test build failed." }

$executable = Join-Path $projectRoot "Build\TextureTests\$Configuration\TextureTests.exe"
$fixtures = Join-Path $projectRoot "Build\TextureTests\$Configuration\Fixtures"
& $executable $fixtures --generate
if ($LASTEXITCODE -ne 0) { throw "Fixture generation failed." }
$converter = Join-Path $PSScriptRoot "ConvertTexture.ps1"
$source = Join-Path $fixtures "source image.png"
$sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
foreach ($usage in @("Color", "Data", "Mask", "UI", "HDR")) {
    $input = $source
    if ($usage -eq "HDR") { $input = Join-Path $fixtures "source.hdr" }
    & $converter -Source $input -Output (Join-Path $fixtures ($usage.ToLowerInvariant() + ".dds")) -Usage $usage -Force
}
& $converter -Source $source -Output (Join-Path $fixtures "single.dds") -Usage Color -MipLevels 1 -Force
& $converter -Source (Join-Path $fixtures "opaque.png") -Output (Join-Path $fixtures "opaque.dds") -Usage Color -Force
& $converter -Source $source -Output (Join-Path $fixtures "coverage.dds") -Usage Color -AlphaCutoff 0.5 -Force
& $converter -Source (Join-Path $fixtures "odd.png") -Output (Join-Path $fixtures "odd-ui.dds") -Usage UI -Force

$script:failureChecks = 0
function Expect-Failure([scriptblock]$Action, [string]$ExpectedMessage) {
    $failed = $false
    try { & $Action }
    catch {
        if ($_.Exception.Message -notlike "*$ExpectedMessage*") { throw }
        $failed = $true
    }
    if (!$failed) { throw "Expected failure was not reported: $ExpectedMessage" }
    ++$script:failureChecks
}

$output = Join-Path $fixtures "color.dds"
$outputHash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
Expect-Failure { & $converter -Source $source -Output $output -Usage Color } "Output already exists"
Expect-Failure { & $converter -Source (Join-Path $fixtures "odd.png") -Output $output -Usage Color -Force } "divisible by four"
Expect-Failure { & $converter -Source $source -Output $output -Usage Color -Force -Texconv (Join-Path $PSScriptRoot "Tests\FailTexconv.cmd") } "exit code 17"
Expect-Failure { & $converter -Source $output -Output $output -Usage Color -Force } "Unsupported source extension"
Expect-Failure { & $converter -Source $source -Output (Join-Path $fixtures "bad.png") -Usage Color } ".dds extension"
Expect-Failure { & $converter -Source $source -Output $output -Usage Data -AlphaCutoff 0.5 -Force } "only for Color"
Expect-Failure { & $converter -Source $source -Output $output -Usage HDR -Force } "require HDR usage"
Expect-Failure { & $converter -Source $source -Output $output -Usage Color -Force -Texconv (Join-Path $fixtures "missing.exe") } "texconv was not found"
Expect-Failure { & $converter -Source $source -Output $output -Usage Color -MipLevels 15 -Force } "texconv failed"
if ((Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash -ne $outputHash) { throw "Failure modified the existing DDS." }
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $sourceHash) { throw "Source image was modified." }
if (@(Get-ChildItem -LiteralPath $fixtures -Directory -Filter ".texture-cook-*").Count -ne 0) { throw "Staging directories were not cleaned up." }
Write-Host "[TextureTests] PASS: $script:failureChecks expected failures; source and existing output preserved."

& $executable $fixtures
if ($LASTEXITCODE -ne 0) { throw "Texture CPU/GPU regression tests failed." }
