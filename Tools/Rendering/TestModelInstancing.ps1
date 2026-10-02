<#
.SYNOPSIS
Builds GameEngine Debug x64 and checks instancing through the real Renderer.
.DESCRIPTION
Requires the normal GameEngine build dependencies, a DirectX 12 GPU, and the
DirectX debug layer. Tests exact draw counts for instancing on/off, buffer growth,
empty frames, alpha test, shadows, transparent materials, overrides and skinning.
The test window is hidden and the editor layout is not saved.
#>
param(
    [string]$VisualStudioPath,
    [string]$FBXSDKRoot
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found. Specify -VisualStudioPath." }
    $VisualStudioPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $FBXSDKRoot) {
    $FBXSDKRoot = Join-Path $root "..\DirectX12\External\FBX SDK\2020.3.9"
}
$msbuild = Join-Path $VisualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
$vcvars = Join-Path $VisualStudioPath "VC\Auxiliary\Build\vcvars64.bat"
& $msbuild (Join-Path $root "GameEngine.sln") /m /p:Configuration=Debug /p:Platform=x64 "/p:FBXSDKRoot=$FBXSDKRoot" /v:minimal
if ($LASTEXITCODE -ne 0) { throw "GameEngine build failed." }

$output = Join-Path ([System.IO.Path]::GetTempPath()) ("GameEngine-Instancing-" + [guid]::NewGuid().ToString("N"))
[System.IO.Directory]::CreateDirectory($output) | Out-Null
try {
    [xml]$project = Get-Content -LiteralPath (Join-Path $root "GameEngine.vcxproj")
    $objects = foreach ($source in $project.Project.ItemGroup.ClCompile) {
        if ($source.Include -and [System.IO.Path]::GetFileName($source.Include) -ne "Main.cpp") {
            $name = [System.IO.Path]::GetFileNameWithoutExtension($source.Include) + ".obj"
            $path = Join-Path $root ("GameEngine\x64\Debug\" + $name)
            if (-not (Test-Path $path)) { throw "Build object missing: $path" }
            '"' + $path + '"'
        }
    }
    $arguments = @(
        "/nologo", "/std:c++20", "/EHsc", "/W4", "/utf-8", "/MTd", "/D_DEBUG",
        ('/I"' + $root + '\Source"'), ('/I"' + $root + '\Library"'),
        ('/I"' + $root + '\Library\DirectXTex-main\Common"'),
        ('/I"' + $root + '\Library\spdlog\include"'),
        ('/I"' + $root + '\Library\ozz-animation\include"'),
        ('/I"' + $root + '\Library\imgui"'),
        ('"' + $PSScriptRoot + '\ModelInstancingTests.cpp"'),
        "/Fe:ModelInstancingTests.exe"
    ) + $objects + @(
        "/link", "/SUBSYSTEM:CONSOLE", "/DEBUG", "/OPT:NOREF", "/OPT:NOICF",
        ('/LIBPATH:"' + $FBXSDKRoot + '\lib\x64\debug"'),
        ('/LIBPATH:"' + $root + '\Library\DirectXTex-main\DirectXTex\Bin\Desktop_2022_Win10\x64\Debug"'),
        ('/LIBPATH:"' + $root + '\Build\ozz-animation\ozz\src\animation\offline\Debug"'),
        ('/LIBPATH:"' + $root + '\Build\ozz-animation\ozz\src\animation\runtime\Debug"'),
        ('/LIBPATH:"' + $root + '\Build\ozz-animation\ozz\src\base\Debug"'),
        "ozz_animation_offline_d.lib", "ozz_animation_d.lib", "ozz_base_d.lib", "libfbxsdk.lib",
        "DirectXTex.lib", "d3d12.lib", "d3dcompiler.lib", "dxgi.lib", "dxguid.lib",
        "wininet.lib", "ws2_32.lib", "user32.lib", "gdi32.lib", "comdlg32.lib", "ole32.lib",
        "shell32.lib", "advapi32.lib", "uuid.lib", "imm32.lib"
    )
    [System.IO.File]::WriteAllText((Join-Path $output "compile.rsp"), ($arguments -join " "))
    $command = 'call "{0}" >nul && cd /d "{1}" && cl @compile.rsp && cd /d "{2}" && set "PATH={2}\x64\Debug;%PATH%" && "{1}\ModelInstancingTests.exe"' -f $vcvars, $output, $root
    & $env:ComSpec /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "Model instancing tests failed with exit code $LASTEXITCODE." }
}
finally {
    foreach ($name in @("compile.rsp", "ModelInstancingTests.exe", "ModelInstancingTests.obj", "ModelInstancingTests.ilk", "ModelInstancingTests.pdb")) {
        $path = Join-Path $output $name
        if (Test-Path $path) { Remove-Item -LiteralPath $path }
    }
    [System.IO.Directory]::Delete($output)
}
