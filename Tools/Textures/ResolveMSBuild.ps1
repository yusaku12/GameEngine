[CmdletBinding()]
param([string]$MSBuild = "")

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($MSBuild)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw "vswhere was not found. Pass -MSBuild." }
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($install)) { throw "Visual Studio C++ build tools were not found." }
    $MSBuild = Join-Path $install "MSBuild\Current\Bin\MSBuild.exe"
}
if (!(Test-Path -LiteralPath $MSBuild -PathType Leaf)) { throw "MSBuild was not found: $MSBuild" }
(Get-Item -LiteralPath $MSBuild).FullName
