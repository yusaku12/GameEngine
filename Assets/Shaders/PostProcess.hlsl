#include "PostProcess.hlsli"
#include "ColorOutput.hlsli"
#include "Aces2Output.hlsli"

PostProcessVertex vsMain(uint vertexID : SV_VertexID)
{
    const float2 corner = float2((vertexID << 1) & 2, vertexID & 2);
    PostProcessVertex output;
    output.position = float4(corner * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float3 displayEncoded(float3 color)
{
    float automaticMultiplier = 1.0f;
    const float autoExposureWeight = saturate(parameters[3].x);
    if (autoExposureWeight > 0.0f)
    {
        const float autoEV = secondaryColor.Load(int3(0, 0, 0)).r;
        automaticMultiplier = exp2(autoEV * autoExposureWeight);
    }
    color = min(max(color * (getPostProcessExposure() * automaticMultiplier), 0.0f), 65504.0f);
    return saturate(applyAces2Output(float4(color, 1.0f)).rgb);
}

float4 psToneMapping(PostProcessVertex input) : SV_Target0
{
    return float4(decodeSrgb(displayEncoded(loadInputColor(input.position.xy).rgb)), 1.0f);
}

float4 psOutput(PostProcessVertex input) : SV_Target0
{
    float3 color = loadInputColor(input.position.xy).rgb;
    if (shouldApplyToneMapping())
        return float4(displayEncoded(color), 1.0f);
    return float4(encodeSrgb(color), 1.0f);
}
