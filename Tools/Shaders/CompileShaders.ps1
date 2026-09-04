[CmdletBinding()]
param(
    [string]$Dxc = "",
    [string]$ShaderRoot = "",
    [string]$OutputRoot = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\")).Path
if ([string]::IsNullOrWhiteSpace($Dxc)) { $Dxc = Join-Path $projectRoot "Tools\DXC\dxc.exe" }
if ([string]::IsNullOrWhiteSpace($ShaderRoot)) { $ShaderRoot = Join-Path $projectRoot "Assets\Shaders" }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $ShaderRoot "Compiled" }

if (!(Test-Path -LiteralPath $Dxc)) { throw "DXC was not found: $Dxc. Place the pinned DXC package under Tools/DXC or pass -Dxc." }
if (!(Test-Path -LiteralPath $ShaderRoot)) { throw "Shader directory was not found: $ShaderRoot" }
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$jobs = @(
    @{ Source = Join-Path $ShaderRoot "ColorTriangle.hlsl"; Entry = "vsMain"; Target = "vs_6_0"; Output = "ColorTriangle_vsMain_vs.cso" },
    @{ Source = Join-Path $ShaderRoot "ColorTriangle.hlsl"; Entry = "psMain"; Target = "ps_6_0"; Output = "ColorTriangle_psMain_ps.cso" }
)

foreach ($job in $jobs) {
    Write-Host "[ShaderCompiler] $($job.Entry) -> $($job.Output)"
    & $Dxc -HV 2021 -E $job.Entry -T $job.Target -Fo (Join-Path $OutputRoot $job.Output) -I $ShaderRoot -Zi -Qembed_debug -Od $job.Source
    if ($LASTEXITCODE -ne 0) { throw "DXC failed for $($job.Source)::$($job.Entry)" }
}
Write-Host "[ShaderCompiler] Shader compilation succeeded."