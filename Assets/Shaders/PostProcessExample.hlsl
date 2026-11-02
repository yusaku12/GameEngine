#include "PostProcess.hlsli"

float4 psTint(PostProcessVertex input) : SV_Target0
{
    const float4 source = loadInputColor(input.position.xy);
    return float4(source.rgb * parameters[0].x + parameters[0].yzw, source.a);
}
