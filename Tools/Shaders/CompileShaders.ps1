[CmdletBinding()]
param(
    [string]$Dxc = "",
    [string]$ShaderRoot = "",
    [string]$OutputRoot = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\")).Path
if ([string]::IsNullOrWhiteSpace($Dxc)) { $Dxc = Join-Path $projectRoot "Tools\DXC\dxc.exe" }
if ([string]::IsNullOrWhiteSpace($ShaderRoot)) { $ShaderRoot = Join-Path $projectRoot "Assets\Shaders" }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $ShaderRoot "Compiled" }

if (!(Test-Path -LiteralPath $Dxc)) { throw "DXC was not found: $Dxc. Place the pinned DXC package under Tools/DXC or pass -Dxc." }
if (!(Test-Path -LiteralPath $ShaderRoot)) { throw "Shader directory was not found: $ShaderRoot" }
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$Dxc = (Resolve-Path -LiteralPath $Dxc).Path
$ShaderRoot = (Resolve-Path -LiteralPath $ShaderRoot).Path
$OutputRoot = (Resolve-Path -LiteralPath $OutputRoot).Path
$stateRoot = Join-Path $OutputRoot ".build"
New-Item -ItemType Directory -Force -Path $stateRoot | Out-Null
$successFile = Join-Path $stateRoot "success"
if (Test-Path -LiteralPath $successFile) { Remove-Item -LiteralPath $successFile }

$toolFiles = @($PSCommandPath, $Dxc, (Join-Path (Split-Path $Dxc) "dxcompiler.dll"), (Join-Path (Split-Path $Dxc) "dxil.dll"))
$toolIdentity = @($toolFiles | ForEach-Object { "$_|$((Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash)" }) -join ";"
# File names affect include lookup even if previously resolved includes did not change.
$sourceInventory = @(Get-ChildItem -LiteralPath $ShaderRoot -File -Recurse |
    Where-Object { !$_.FullName.StartsWith($OutputRoot + "\", [StringComparison]::OrdinalIgnoreCase) -and
        !$_.FullName.StartsWith((Join-Path $ShaderRoot ".cache") + "\", [StringComparison]::OrdinalIgnoreCase) } |
    Sort-Object FullName | ForEach-Object { $_.FullName }) -join ";"

function Test-CurrentShader($record, [string]$settings, [string]$output) {
    if ($Force -or $null -eq $record -or $record.Settings -cne $settings -or !(Test-Path -LiteralPath $output)) { return $false }
    if ($record.OutputHash -cne (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash) { return $false }
    if (@($record.Dependencies).Count -eq 0) { return $false }
    foreach ($dependency in $record.Dependencies) {
        if (!(Test-Path -LiteralPath $dependency.Path) -or
            $dependency.Hash -cne (Get-FileHash -LiteralPath $dependency.Path -Algorithm SHA256).Hash) { return $false }
    }
    return $true
}

$jobs = @(
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "vsMain"; Target = "vs_6_0"; Output = "Model_vsMain_vs.cso" },
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "csSkinning"; Target = "cs_6_0"; Output = "Model_csSkinning_cs.cso" },
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "psMain"; Target = "ps_6_0"; Output = "Model_psMain_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "psAlphaTest"; Target = "ps_6_0"; Output = "Model_psAlphaTest_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "psDepthAlphaTest"; Target = "ps_6_0"; Output = "Model_psDepthAlphaTest_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "psGBuffer"; Target = "ps_6_0"; Output = "Model_psGBuffer_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "psGBufferAlphaTest"; Target = "ps_6_0"; Output = "Model_psGBufferAlphaTest_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "Deferred.hlsl"; Entry = "vsMain"; Target = "vs_6_0"; Output = "Deferred_vsMain_vs.cso" },
    @{ Source = Join-Path $ShaderRoot "Deferred.hlsl"; Entry = "csLighting"; Target = "cs_6_0"; Output = "Deferred_csLighting_cs.cso" },
    @{ Source = Join-Path $ShaderRoot "Deferred.hlsl"; Entry = "psDebug"; Target = "ps_6_0"; Output = "Deferred_psDebug_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcess.hlsl"; Entry = "vsMain"; Target = "vs_6_0"; Output = "PostProcess_vsMain_vs.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcess.hlsl"; Entry = "psToneMapping"; Target = "ps_6_0"; Output = "PostProcess_psToneMapping_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcess.hlsl"; Entry = "psOutput"; Target = "ps_6_0"; Output = "PostProcess_psOutput_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "Bloom.hlsl"; Entry = "csBloomPrefilter"; Target = "cs_6_0"; Output = "Bloom_csBloomPrefilter_cs.cso" },
    @{ Source = Join-Path $ShaderRoot "Bloom.hlsl"; Entry = "csBloomDownsample"; Target = "cs_6_0"; Output = "Bloom_csBloomDownsample_cs.cso" },
    @{ Source = Join-Path $ShaderRoot "Bloom.hlsl"; Entry = "csBloomUpsample"; Target = "cs_6_0"; Output = "Bloom_csBloomUpsample_cs.cso" },
    @{ Source = Join-Path $ShaderRoot "Bloom.hlsl"; Entry = "csBloomComposite"; Target = "cs_6_0"; Output = "Bloom_csBloomComposite_cs.cso" },
    @{ Source = Join-Path $ShaderRoot "LightShafts.hlsl"; Entry = "csLightShafts"; Target = "cs_6_0"; Output = "LightShafts_csLightShafts_cs.cso" },
    @{ Source = Join-Path $ShaderRoot "LightShafts.hlsl"; Entry = "psLightShaftsFallback"; Target = "ps_6_0"; Output = "LightShafts_psLightShaftsFallback_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "AutoExposure.hlsl"; Entry = "csAutoExposure"; Target = "cs_6_0"; Output = "AutoExposure_csAutoExposure_cs.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psCustomComposite"; Target = "ps_6_0"; Output = "PostProcessEffects_psCustomComposite_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psVignette"; Target = "ps_6_0"; Output = "PostProcessEffects_psVignette_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psChromaticAberration"; Target = "ps_6_0"; Output = "PostProcessEffects_psChromaticAberration_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psFilmGrain"; Target = "ps_6_0"; Output = "PostProcessEffects_psFilmGrain_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psFxaa"; Target = "ps_6_0"; Output = "PostProcessEffects_psFxaa_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psDepthOfField"; Target = "ps_6_0"; Output = "PostProcessEffects_psDepthOfField_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psAutoExposureAdapt"; Target = "ps_6_0"; Output = "PostProcessEffects_psAutoExposureAdapt_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psTemporalAA"; Target = "ps_6_0"; Output = "PostProcessEffects_psTemporalAA_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psMotionBlur"; Target = "ps_6_0"; Output = "PostProcessEffects_psMotionBlur_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "DebugPrimitive.hlsl"; Entry = "vsMain"; Target = "vs_6_0"; Output = "DebugPrimitive_vsMain_vs.cso" },
    @{ Source = Join-Path $ShaderRoot "DebugPrimitive.hlsl"; Entry = "vsShape"; Target = "vs_6_0"; Output = "DebugPrimitive_vsShape_vs.cso" },
    @{ Source = Join-Path $ShaderRoot "DebugPrimitive.hlsl"; Entry = "psMain"; Target = "ps_6_0"; Output = "DebugPrimitive_psMain_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "HiZ.hlsl"; Entry = "csMain"; Target = "cs_6_0"; Output = "HiZ_csMain_cs.cso" },
    @{ Source = Join-Path $ShaderRoot "Occlusion.hlsl"; Entry = "csMain"; Target = "cs_6_0"; Output = "Occlusion_csMain_cs.cso" },
    @{ Source = Join-Path $ShaderRoot "TerrainNoise.hlsl"; Entry = "csMain"; Target = "cs_6_0"; Output = "TerrainNoise_csMain_cs.cso"; WarningsAsErrors = $true },
    @{ Source = Join-Path $ShaderRoot "TerrainMesh.hlsl"; Entry = "csMain"; Target = "cs_6_0"; Output = "TerrainMesh_csMain_cs.cso"; WarningsAsErrors = $true }
)

$outputs = @()
$dependencies = @()
$compiled = 0
foreach ($job in $jobs) {
    $optimization = "-O3"
    $output = Join-Path $OutputRoot $job.Output
    $recordFile = Join-Path $stateRoot ($job.Output + ".json")
    $settings = "$toolIdentity|$sourceInventory|$ShaderRoot|$($job.Source)|$($job.Entry)|$($job.Target)|$optimization|$($job.WarningsAsErrors)|-HV 2021 -Zi -Qembed_debug"
    $record = $null
    if (!$Force -and (Test-Path -LiteralPath $recordFile)) { $record = Get-Content -LiteralPath $recordFile -Raw | ConvertFrom-Json }
    if (!(Test-CurrentShader $record $settings $output)) {
        Write-Host "[ShaderCompiler] $($job.Entry) -> $($job.Output)"
        if (Test-Path -LiteralPath $recordFile) { Remove-Item -LiteralPath $recordFile }
        $temporaryOutput = $output + ".tmp"
        $dependencyFile = $output + ".d"
        try {
            $arguments = @("-HV", "2021", "-E", $job.Entry, "-T", $job.Target, "-Fo", $temporaryOutput,
                "-I", $ShaderRoot, "-Zi", "-Qembed_debug", $optimization)
            if ($job.WarningsAsErrors) { $arguments += "-WX" }
            & $Dxc @arguments $job.Source
            if ($LASTEXITCODE -ne 0) { throw "DXC failed for $($job.Source)::$($job.Entry)" }
            & $Dxc @arguments -MD -MF $dependencyFile $job.Source
            if ($LASTEXITCODE -ne 0) { throw "DXC dependency scan failed for $($job.Source)::$($job.Entry)" }
            # DXC emits one dependency per continued line, preserving spaces in paths.
            $dependencyLines = @(Get-Content -LiteralPath $dependencyFile)
            $dependencyLines[0] = $dependencyLines[0] -replace '^.*?:\s+', ''
            $resolvedDependencies = @($dependencyLines | ForEach-Object {
                $path = $_.Trim().TrimEnd('\').Trim()
                if ($path) { (Resolve-Path -LiteralPath $path).Path }
            } | Sort-Object -Unique)
            if ($resolvedDependencies -notcontains $job.Source) { throw "DXC dependency output did not contain $($job.Source)" }
            $record = @{
                Settings = $settings
                OutputHash = (Get-FileHash -LiteralPath $temporaryOutput -Algorithm SHA256).Hash
                Dependencies = @($resolvedDependencies | ForEach-Object {
                    @{ Path = $_; Hash = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
                })
            }
            Move-Item -LiteralPath $temporaryOutput -Destination $output -Force
            $record | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $recordFile -Encoding UTF8
            ++$compiled
        }
        finally {
            foreach ($temporaryFile in @($temporaryOutput, $dependencyFile)) {
                if (Test-Path -LiteralPath $temporaryFile) { Remove-Item -LiteralPath $temporaryFile }
            }
        }
    }
    $outputs += $output, $recordFile
    $dependencies += @($record.Dependencies | ForEach-Object { $_.Path })
}
$outputs | Set-Content -LiteralPath (Join-Path $stateRoot "outputs.list") -Encoding UTF8
$dependencies | Sort-Object -Unique | Set-Content -LiteralPath (Join-Path $stateRoot "dependencies.list") -Encoding UTF8
Write-Host "[ShaderCompiler] Shader compilation succeeded: $compiled compiled, $($jobs.Count - $compiled) unchanged."