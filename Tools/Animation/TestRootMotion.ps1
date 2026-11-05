param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vs = (& $vswhere -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw "Visual Studio C++ tools were not found." }
$msbuild = Join-Path $vs "MSBuild\Current\Bin\MSBuild.exe"
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$output = Join-Path $root "Build\GameEngine\x64\$Configuration\RootMotionTests"
$engineOutput = Join-Path $root "x64\$Configuration"

Push-Location $root
try {
    & $msbuild "GameEngine.sln" /m "/p:Configuration=$Configuration" /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "Engine build failed." }
    New-Item -ItemType Directory -Force -Path $output | Out-Null

    $runtime = "/MT"
    $defines = "/DNDEBUG"
    if ($Configuration -eq "Debug") { $runtime = "/MTd"; $defines = "/D_DEBUG" }
    $object = Join-Path $output "RootMotionTests.obj"
    $exe = Join-Path $output "RootMotionTests.exe"
    $compileResponse = Join-Path $output "compile.rsp"
    $linkResponse = Join-Path $output "link.rsp"
    $includes = @(".", "Source", "Library", "Library\ozz-animation\include", "Library\flatbuffers\include",
        "Library\spdlog\include", "Library\DirectXTex-main\Common", "Library\PhysX\physx\include")
    $compile = @("/nologo", "/c", "/std:c++20", "/EHsc", "/W4", "/WX", "/utf-8", $runtime, $defines,
        "/DPX_PHYSX_STATIC_LIB", "/Fo`"$object`"", "/Fd`"$output\tests.pdb`"")
    foreach ($include in $includes) { $compile += "/I`"$(Join-Path $root $include)`"" }
    $compile += "`"$(Join-Path $PSScriptRoot 'RootMotionTests.cpp')`""
    [IO.File]::WriteAllLines($compileResponse, $compile, [Text.Encoding]::Unicode)

    # Reuse resolved libraries, but get object inputs from the project: tracking logs can retain removed sources.
    $tlog = Join-Path $root "Build\GameEngine\x64\$Configuration\GameEngine.tlog\link.command.1.tlog"
    $commands = @(Get-Content -LiteralPath $tlog)
    $start = -1
    for ($i = 0; $i -lt $commands.Count; ++$i) {
        if ($commands[$i] -match '^/OUT:') { $start = $i; break }
    }
    if ($start -lt 0) { throw "No tracked engine linker command was found." }
    $end = $start + 1
    while ($end -lt $commands.Count -and -not $commands[$end].StartsWith("^")) { ++$end }
    $link = ($commands[$start..($end - 1)] -join " ")
    $objectPattern = '(?i)(?:"[^"]+\.OBJ"|[^\s"]+\.OBJ)(?=\s|$)'
    $link = [regex]::Replace($link, $objectPattern, "")
    [xml]$project = Get-Content -LiteralPath (Join-Path $root "GameEngine.vcxproj")
    $names = @{}
    foreach ($source in $project.SelectNodes('//*[local-name()="ClCompile"][@Include]')) {
        $name = [IO.Path]::GetFileNameWithoutExtension($source.Include)
        if ($name -eq "Main") { continue }
        if ($names.ContainsKey($name)) { throw "Duplicate engine object name: $name" }
        $names[$name] = $true
        $engineObject = Join-Path $root "Build\GameEngine\x64\$Configuration\$name.obj"
        if (-not (Test-Path -LiteralPath $engineObject)) { throw "Missing built engine object: $engineObject" }
        $link += " `"$engineObject`""
    }
    $link = [regex]::Replace($link, '(?i)/OUT:"[^"]+"', "/OUT:`"$exe`"")
    $link = [regex]::Replace($link, '(?i)/(ILK|PDB|IMPLIB):"[^"]+"', "")
    $link = [regex]::Replace($link, '(?i)/LTCGOUT:"[^"]+"', "/LTCGOUT:`"$output\tests.iobj`"")
    $link = [regex]::Replace($link, '(?i)/SUBSYSTEM:WINDOWS', "/SUBSYSTEM:CONSOLE")
    $link += " `"$object`" /PDB:`"$output\tests.pdb`" /ILK:`"$output\tests.ilk`" /WX"
    [IO.File]::WriteAllText($linkResponse, $link, [Text.Encoding]::Unicode)
    & $env:ComSpec /d /c "call `"$vcvars`" >nul && cl.exe @`"$compileResponse`" && link.exe @`"$linkResponse`" && set `"PATH=$engineOutput;%PATH%`" && `"$exe`""
    if ($LASTEXITCODE -ne 0) { throw "Root motion tests failed ($LASTEXITCODE)." }
}
finally {
    Pop-Location
}
