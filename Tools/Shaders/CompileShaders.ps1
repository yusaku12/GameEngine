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
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "vsMain"; Target = "vs_6_0"; Output = "Model_vsMain_vs.cso" },
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "psMain"; Target = "ps_6_0"; Output = "Model_psMain_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "psAlphaTest"; Target = "ps_6_0"; Output = "Model_psAlphaTest_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "psDepthAlphaTest"; Target = "ps_6_0"; Output = "Model_psDepthAlphaTest_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "psGBuffer"; Target = "ps_6_0"; Output = "Model_psGBuffer_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "Model.hlsl"; Entry = "psGBufferAlphaTest"; Target = "ps_6_0"; Output = "Model_psGBufferAlphaTest_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "Deferred.hlsl"; Entry = "vsMain"; Target = "vs_6_0"; Output = "Deferred_vsMain_vs.cso" },
    @{ Source = Join-Path $ShaderRoot "Deferred.hlsl"; Entry = "psLighting"; Target = "ps_6_0"; Output = "Deferred_psLighting_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "Deferred.hlsl"; Entry = "psDebug"; Target = "ps_6_0"; Output = "Deferred_psDebug_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcess.hlsl"; Entry = "vsMain"; Target = "vs_6_0"; Output = "PostProcess_vsMain_vs.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcess.hlsl"; Entry = "psToneMapping"; Target = "ps_6_0"; Output = "PostProcess_psToneMapping_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcess.hlsl"; Entry = "psOutput"; Target = "ps_6_0"; Output = "PostProcess_psOutput_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessValidation.hlsl"; Entry = "psScale"; Target = "ps_6_0"; Output = "PostProcessValidation_psScale_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessValidation.hlsl"; Entry = "psCustomGain"; Target = "ps_6_0"; Output = "PostProcessValidation_psCustomGain_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psBloomPrefilter"; Target = "ps_6_0"; Output = "PostProcessEffects_psBloomPrefilter_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psBloomDownsample"; Target = "ps_6_0"; Output = "PostProcessEffects_psBloomDownsample_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psBloomUpsample"; Target = "ps_6_0"; Output = "PostProcessEffects_psBloomUpsample_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psBloomComposite"; Target = "ps_6_0"; Output = "PostProcessEffects_psBloomComposite_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psCustomComposite"; Target = "ps_6_0"; Output = "PostProcessEffects_psCustomComposite_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psVignette"; Target = "ps_6_0"; Output = "PostProcessEffects_psVignette_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psChromaticAberration"; Target = "ps_6_0"; Output = "PostProcessEffects_psChromaticAberration_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psFilmGrain"; Target = "ps_6_0"; Output = "PostProcessEffects_psFilmGrain_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psFxaa"; Target = "ps_6_0"; Output = "PostProcessEffects_psFxaa_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psDepthOfField"; Target = "ps_6_0"; Output = "PostProcessEffects_psDepthOfField_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psColorGradingLut"; Target = "ps_6_0"; Output = "PostProcessEffects_psColorGradingLut_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psAutoExposureDownsample"; Target = "ps_6_0"; Output = "PostProcessEffects_psAutoExposureDownsample_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psAutoExposureAdapt"; Target = "ps_6_0"; Output = "PostProcessEffects_psAutoExposureAdapt_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psTemporalAA"; Target = "ps_6_0"; Output = "PostProcessEffects_psTemporalAA_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "PostProcessEffects.hlsl"; Entry = "psMotionBlur"; Target = "ps_6_0"; Output = "PostProcessEffects_psMotionBlur_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "DebugPrimitive.hlsl"; Entry = "vsMain"; Target = "vs_6_0"; Output = "DebugPrimitive_vsMain_vs.cso" },
    @{ Source = Join-Path $ShaderRoot "DebugPrimitive.hlsl"; Entry = "psMain"; Target = "ps_6_0"; Output = "DebugPrimitive_psMain_ps.cso" },
    @{ Source = Join-Path $ShaderRoot "HiZ.hlsl"; Entry = "csMain"; Target = "cs_6_0"; Output = "HiZ_csMain_cs.cso"; Optimize = $true },
    @{ Source = Join-Path $ShaderRoot "Occlusion.hlsl"; Entry = "csMain"; Target = "cs_6_0"; Output = "Occlusion_csMain_cs.cso"; Optimize = $true }
)

foreach ($job in $jobs) {
    Write-Host "[ShaderCompiler] $($job.Entry) -> $($job.Output)"
    $optimization = "-Od"
    if ($job.Optimize) { $optimization = "-O3" }
    & $Dxc -HV 2021 -E $job.Entry -T $job.Target -Fo (Join-Path $OutputRoot $job.Output) -I $ShaderRoot -Zi -Qembed_debug $optimization $job.Source
    if ($LASTEXITCODE -ne 0) { throw "DXC failed for $($job.Source)::$($job.Entry)" }
}
Write-Host "[ShaderCompiler] Shader compilation succeeded."