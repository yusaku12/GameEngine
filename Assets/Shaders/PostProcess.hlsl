#include "PostProcess.hlsli"
#include "ColorOutput.hlsli"

PostProcessVertex vsMain(uint vertexID : SV_VertexID)
{
    const float2 corner = float2((vertexID << 1) & 2, vertexID & 2);
    PostProcessVertex output;
    output.position = float4(corner * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

float3 applyColorGradeRange(float3 color, float4 controls, float3 offset, float weight)
{
    const float saturation = lerp(1.0f, controls.x, weight);
    const float contrast = lerp(1.0f, controls.y, weight);
    const float gamma = lerp(1.0f, controls.z, weight);
    const float gain = lerp(1.0f, controls.w, weight);
    const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = max(lerp(luminance.xxx, color, saturation), 0.0f);
    color = max((color - 0.18f) * contrast + 0.18f + offset * weight, 0.0f);
    return pow(color, rcp(gamma)) * gain;
}

float3 displayLinear(float3 color)
{
    if (parameters[0].x != 0.0f)
    {
        const float temperature = (6500.0f - parameters[1].w) / 6500.0f;
        const float tint = parameters[2].x;
        const float3 whiteBalance = float3(
            exp2(temperature * 0.65f + tint * 0.3f),
            exp2(-abs(temperature) * 0.08f - tint * 0.35f),
            exp2(-temperature * 0.65f + tint * 0.3f));
        color = max(color * parameters[1].rgb * whiteBalance + parameters[2].yzw, 0.0f);
        color = max((color - 0.18f) * parameters[0].y + 0.18f, 0.0f);
        const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        color = max(lerp(luminance.xxx, color, parameters[0].z), 0.0f);
        color = pow(color, rcp(parameters[0].w));

        const float rangeLuminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        const float gradingLuminance = saturate(log2(1.0f + max(rangeLuminance, 0.0f)) / 6.0f);
        const float shadowWeight = 1.0f - smoothstep(0.0f, 0.5f, gradingLuminance);
        const float highlightWeight = smoothstep(0.5f, 1.0f, gradingLuminance);
        const float midtoneWeight = saturate(1.0f - shadowWeight - highlightWeight);

        color = applyColorGradeRange(color, parameters[4], parameters[5].rgb, shadowWeight);
        color = applyColorGradeRange(color, parameters[6], parameters[7].rgb, midtoneWeight);
        color = applyColorGradeRange(color, parameters[8], parameters[9].rgb, highlightWeight);
    }
    float automaticMultiplier = 1.0f;
    const float autoExposureWeight = saturate(parameters[3].x);
    if (autoExposureWeight > 0.0f)
    {
        const float autoEV = secondaryColor.Load(int3(0, 0, 0)).r;
        automaticMultiplier = exp2(autoEV * autoExposureWeight);
    }
    return toneMap(color, getPostProcessExposure() * automaticMultiplier);
}

float4 psToneMapping(PostProcessVertex input) : SV_Target0
{
    return float4(displayLinear(loadInputColor(input.position.xy).rgb), 1.0f);
}

float4 psOutput(PostProcessVertex input) : SV_Target0
{
    float3 color = loadInputColor(input.position.xy).rgb;
    if (shouldApplyToneMapping())
        color = displayLinear(color);
    return float4(encodeSrgb(color), 1.0f);
}
