#define ENGINE_POST_PROCESS_PARAMETER_COUNT 1
#include "PostProcessCompute.hlsli"

groupshared float2 reduced[64];

float logLuminance(float3 color)
{
    return log2(max(dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f)), 0.0001f));
}

[numthreads(8, 8, 1)]
void csAutoExposure(uint3 group : SV_GroupID, uint3 lane : SV_GroupThreadID)
{
    const uint2 destination = dispatchOrigin + group.xy;
    if (any(destination >= dispatchEnd))
        return;

    const int2 first = int2(sourceViewport.xy);
    const int2 end = first + int2(sourceViewport.zw);
    const int2 base = first + int2(group.xy * 16u + lane.xy * 2u);
    float weightedSum = 0.0f;
    float totalWeight = 0.0f;
    [unroll]
    for (int y = 0; y < 2; ++y)
    {
        [unroll]
        for (int x = 0; x < 2; ++x)
        {
            const int2 position = base + int2(x, y);
            if (all(position < end))
            {
                const float4 value = inputColor.Load(int3(position, 0));
                const float weight = parameters[0].x > 0.5f ? 1.0f : value.g;
                weightedSum += (parameters[0].x > 0.5f ? logLuminance(value.rgb) : value.r) * weight;
                totalWeight += weight;
            }
        }
    }
    const uint index = lane.y * 8u + lane.x;
    reduced[index] = totalWeight > 0.0f ? float2(weightedSum / totalWeight, totalWeight * 0.25f) : 0.0f;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 1u; stride < 8u; stride *= 2u)
    {
        if ((lane.x % (stride * 2u)) == 0u && (lane.y % (stride * 2u)) == 0u)
        {
            float sum = 0.0f;
            float weight = 0.0f;
            [unroll]
            for (uint y = 0; y < 2u; ++y)
            {
                [unroll]
                for (uint x = 0; x < 2u; ++x)
                {
                    const float2 value = reduced[index + (y * 8u + x) * stride];
                    sum += value.x * value.y;
                    weight += value.y;
                }
            }
            reduced[index] = weight > 0.0f ? float2(sum / weight, weight * 0.25f) : 0.0f;
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (index == 0u)
        outputColor[destination] = float4(reduced[0], 0.0f, 1.0f);
}
