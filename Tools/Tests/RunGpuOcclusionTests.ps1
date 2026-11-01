[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Warp
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$msbuild = & (Join-Path $root "Tools\Textures\ResolveMSBuild.ps1")
& $msbuild (Join-Path $root "GameEngine.sln") /m "/p:Configuration=$Configuration" /p:Platform=x64 /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw "Engine build failed." }

$output = Join-Path $root "Build\Tests\GpuOcclusion\$Configuration"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$dxc = Join-Path $root "Tools\DXC\dxc.exe"
foreach ($stage in @("vs", "ps")) {
    & $dxc -WX -HV 2021 -E "${stage}Main" -T "${stage}_6_0" -Fo (Join-Path $output "$stage.cso") (Join-Path $PSScriptRoot "GpuOcclusionTest.hlsl")
    if ($LASTEXITCODE -ne 0) { throw "Test shader compilation failed." }
}
& $dxc -WX -HV 2021 -O3 -E csMain -T cs_6_0 -I (Join-Path $root "Assets\Shaders") -Fo (Join-Path $output "skinning.cso") (Join-Path $PSScriptRoot "SkinningBoundsTest.hlsl")
if ($LASTEXITCODE -ne 0) { throw "Production skinning bounds test shader compilation failed." }

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($install)) { throw "Visual Studio C++ tools were not found." }
$vcvars = Join-Path $install "VC\Auxiliary\Build\vcvars64.bat"
$objects = @("Pch", "SimpleMath", "SkinningBounds", "GpuOcclusionCulling", "Pipeline", "Resource", "Command", "Descriptor", "Fence", "RenderQueue", "Shader", "Logger")
$arguments = @("/nologo", "/std:c++20", "/EHsc", "/utf-8", "/W4", "/WX", "/external:W0")
if ($Configuration -eq "Debug") { $arguments += @("/MTd", "/D_DEBUG", "/Od") }
else { $arguments += @("/MT", "/DNDEBUG", "/O2") }
$includes = @("Source", "Library", "Library\ozz-animation\include", "Library\flatbuffers\include", "Library\spdlog\include", "Library\DirectXTex-main\Common", "Library\imgui", "Library\ImGuizmo", "Library\PhysX\physx\include")
foreach ($include in $includes) { $arguments += "/I`"$(Join-Path $root $include)`"" }
$arguments += "/Fo`"$(Join-Path $output 'GpuOcclusionTests.obj')`""
$arguments += "`"$(Join-Path $PSScriptRoot 'GpuOcclusionTests.cpp')`""
foreach ($object in $objects) { $arguments += "`"$(Join-Path $root "Build\GameEngine\x64\$Configuration\$object.obj")`"" }
$arguments += @("/link", "/SUBSYSTEM:CONSOLE", "/OUT:`"$(Join-Path $output 'GpuOcclusionTests.exe')`"", "d3d12.lib", "dxgi.lib", "d3dcompiler.lib", "dxguid.lib")
if ($Configuration -eq "Debug") { $arguments += @("/DEBUG", "/INCREMENTAL", "/OPT:NOREF", "/OPT:NOICF") }
else { $arguments += @("/INCREMENTAL:NO", "/OPT:REF", "/OPT:ICF") }
& $env:ComSpec /d /s /c "call `"$vcvars`" >nul && cl $($arguments -join ' ')"
if ($LASTEXITCODE -ne 0) { throw "GPU regression test build failed." }

Push-Location $root
try {
    $testArguments = @($output)
    if ($Warp) { $testArguments += "--warp" }
    & (Join-Path $output "GpuOcclusionTests.exe") @testArguments
    if ($LASTEXITCODE -ne 0) { throw "GPU occlusion regression tests failed." }
}
finally { Pop-Location }
