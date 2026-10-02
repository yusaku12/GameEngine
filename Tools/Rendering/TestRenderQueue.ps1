<#
.SYNOPSIS
Compiles and runs CPU batch compatibility and ordering regression tests.
.DESCRIPTION
Requires Visual Studio C++ build tools. Run from PowerShell with:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\Rendering\TestRenderQueue.ps1
No GPU or FBX SDK is required. Temporary compiler outputs are removed.
#>
param(
    [string]$VisualStudioPath
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $VisualStudioPath) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found. Specify -VisualStudioPath." }
    $VisualStudioPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$vcvars = Join-Path $VisualStudioPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found: $vcvars" }
$output = Join-Path ([System.IO.Path]::GetTempPath()) ("GameEngine-RenderQueue-" + [guid]::NewGuid().ToString("N"))
[System.IO.Directory]::CreateDirectory($output) | Out-Null
try {
    $command = 'call "{0}" >nul && cd /d "{1}" && cl /nologo /std:c++20 /EHsc /W4 /utf-8 /MT /I"{2}\Source" /I"{2}\Library" /I"{2}\Library\DirectXTex-main\Common" /I"{2}\Library\spdlog\include" /I"{2}\Library\ozz-animation\include" "{2}\Tools\Rendering\RenderQueueTests.cpp" "{2}\Source\Graphics\Renderer\RenderQueue.cpp" "{2}\Source\Graphics\Material\MaterialPropertyBlock.cpp" "{2}\Source\Core\Math\SimpleMath.cpp" /Fe:RenderQueueTests.exe && RenderQueueTests.exe' -f $vcvars, $output, $root
    & $env:ComSpec /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "RenderQueue tests failed with exit code $LASTEXITCODE." }
}
finally {
    foreach ($name in @("RenderQueueTests.exe", "RenderQueueTests.obj", "RenderQueue.obj", "MaterialPropertyBlock.obj", "SimpleMath.obj")) {
        $path = Join-Path $output $name
        if (Test-Path $path) { Remove-Item -LiteralPath $path }
    }
    [System.IO.Directory]::Delete($output)
}
