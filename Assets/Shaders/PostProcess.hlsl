#include "PostProcess.hlsli"
#include "ColorOutput.hlsli"

PostProcessVertex vsMain(uint vertexID : SV_VertexID)
{
    const float2 corner = float2((vertexID << 1) & 2, vertexID & 2);
    PostProcessVertex output;
    output.position = float4(corner * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float3 displayLinear(float3 color)
{
    if (parameters[0].x != 0.0f)
    {
        // Color filter -> contrast about middle gray -> Rec.709 saturation in linear HDR.
        color = max(color * parameters[1].rgb, 0.0f);
        color = max((color - 0.18f) * parameters[0].y + 0.18f, 0.0f);
        const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        color = max(lerp(luminance.xxx, color, parameters[0].z), 0.0f);
    }
    float automaticMultiplier = 1.0f;
    const float autoExposureWeight = saturate(parameters[3].x);
    if (autoExposureWeight > 0.0f)
    {
        const float autoEV = secondaryColor.Load(int3(0, 0, 0)).r;
        automaticMultiplier = exp2(autoEV * autoExposureWeight);
    }
    return toneMap(color, exposure * automaticMultiplier);
}

float4 psToneMapping(PostProcessVertex input) : SV_Target0
{
    return float4(displayLinear(loadInputColor(input.position.xy).rgb), 1.0f);
}

float4 psOutput(PostProcessVertex input) : SV_Target0
{
    float3 color = loadInputColor(input.position.xy).rgb;
    if (applyToneMapping != 0u)
        color = displayLinear(color);
    return float4(encodeSrgb(color), 1.0f);
}
