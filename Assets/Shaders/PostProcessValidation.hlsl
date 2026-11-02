#include "PostProcess.hlsli"

// Used only by the deterministic rendering validation.
float4 psScale(PostProcessVertex input) : SV_Target0
{
    const float4 color = loadInputColor(input.position.xy);
    return float4(color.rgb * parameters[0].x, color.a);
}

float4 psCustomGain(PostProcessVertex input) : SV_Target0
{
    const float4 color = loadInputColor(input.position.xy);
    return float4(color.rgb * parameters[0].x, color.a);
}
