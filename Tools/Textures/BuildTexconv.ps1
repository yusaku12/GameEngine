[CmdletBinding()]
param(
    [string]$MSBuild = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$MSBuild = & (Join-Path $PSScriptRoot "ResolveMSBuild.ps1") -MSBuild $MSBuild

$project = Join-Path $projectRoot "Library\DirectXTex-main\Texconv\Texconv_Desktop_2022_Win10.vcxproj"
& $MSBuild $project /nologo /m /t:Build /p:Configuration=Release /p:Platform=x64 /verbosity:minimal
if ($LASTEXITCODE -ne 0) { throw "texconv build failed (exit code $LASTEXITCODE)." }
$executable = Join-Path $projectRoot "Library\DirectXTex-main\Texconv\Bin\Desktop_2022_Win10\x64\Release\texconv.exe"
if (!(Test-Path -LiteralPath $executable -PathType Leaf)) { throw "texconv build produced no executable: $executable" }
Write-Host "[TextureCooker] Built: $executable"
