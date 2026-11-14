#define ENGINE_POST_PROCESS_PARAMETER_COUNT 3
#include "PostProcessCompute.hlsli"
#include "PostProcessNoise.hlsli"

static const uint SAMPLE_COUNT = 64;

float3 prefilterLightShafts(float3 color, float threshold)
{
    const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    const float knee = max(threshold * 0.5f, 0.0001f);
    float softContribution = clamp(luminance - threshold + knee, 0.0f, 2.0f * knee);
    softContribution = softContribution * softContribution / (4.0f * knee);
    const float contribution = saturate(max(luminance - threshold, softContribution));
    return color * (contribution / max(luminance, 0.0001f));
}

void initializeShaftRay(float2 pixel, out float2 sampleUv, out float2 stepVector)
{
    const float2 uv = (pixel - targetViewport.xy) / targetViewport.zw;
    stepVector = (parameters[0].xy - uv) * (0.95f / SAMPLE_COUNT);
    sampleUv = uv + stepVector * (grainNoise(uint2(pixel), 0u) * 0.5f);
}

float sampleShaftDepth(float2 sampleUv)
{
    const float2 samplePixel = targetViewport.xy + sampleUv * targetViewport.zw;
    return sampleViewport(secondaryColor, secondaryViewport, samplePixel, 0).r;
}

bool needsShaftFallback(uint2 pixel)
{
    // Missing helper lanes or divergent ray bounds make Pixel Shader derivatives GPU-dependent.
    const uint2 quad = pixel & ~1u;
    const float2 rasterOrigin = round(targetViewport.xy * 256.0f) / 256.0f;
    const bool fullQuad = all(float2(quad) + 0.5f >= max(rasterOrigin, targetViewport.xy))
        && all(quad + 1u < uint2(floor(targetViewport.xy + targetViewport.zw)));
    const bool sourceInside = all(parameters[0].xy >= 0.0f) && all(parameters[0].xy <= 1.0f);
    return !fullQuad || !sourceInside;
}

[numthreads(8, 8, 1)]
void csLightShafts(uint3 id : SV_DispatchThreadID)
{
    uint2 destination;
    float2 pixel;
    if (!postProcessPixel(id.xy, destination, pixel))
        return;
    const bool occlusion = parameters[2].x > 0.5f;
    if (occlusion && needsShaftFallback(destination))
        return;

    float2 sampleUv, stepVector;
    initializeShaftRay(pixel, sampleUv, stepVector);
    // Pixel Shader fwidth uses the coarse derivatives of the aligned 2x2 quad.
    float2 quadUv0 = 0, quadUvX = 0, quadUvY = 0, quadStep0 = 0, quadStepX = 0, quadStepY = 0;
    if (occlusion)
    {
        const float2 quadPixel = float2(destination & ~1u) + 0.5f;
        initializeShaftRay(quadPixel, quadUv0, quadStep0);
        initializeShaftRay(quadPixel + float2(1, 0), quadUvX, quadStepX);
        initializeShaftRay(quadPixel + float2(0, 1), quadUvY, quadStepY);
    }
    float3 accumulated = 0.0f;
    float illuminationDecay = 1.0f;
    [loop]
    for (uint index = 0; index < SAMPLE_COUNT; ++index)
    {
        sampleUv += stepVector;
        if (occlusion)
        {
            quadUv0 += quadStep0;
            quadUvX += quadStepX;
            quadUvY += quadStepY;
        }
        const bool insideViewport = all(sampleUv >= 0.0f) && all(sampleUv <= 1.0f);
        if (insideViewport)
        {
            const float2 samplePixel = targetViewport.xy + sampleUv * targetViewport.zw;
            const float3 sampleColor = max(sampleViewport(inputColor, sourceViewport, samplePixel, 0).rgb, 0.0f);
            const float3 brightColor = prefilterLightShafts(sampleColor, parameters[0].w);
            float visibility = 1.0f;
            if (occlusion)
            {
                const float depth = sampleShaftDepth(sampleUv);
                const float depth0 = sampleShaftDepth(quadUv0);
                const float depthX = sampleShaftDepth(quadUvX);
                const float depthY = sampleShaftDepth(quadUvY);
                const float depthEdgeWidth = max((abs(depthX - depth0) + abs(depthY - depth0)) * 2.0f, 0.000001f);
                const float skyVisibility = smoothstep(1.0f - depthEdgeWidth, 1.0f, depth);
                visibility = lerp(1.0f - saturate(parameters[1].w), 1.0f, skyVisibility);
            }
            accumulated += brightColor * (visibility * illuminationDecay);
        }
        illuminationDecay *= 0.98f;
    }
    const float3 shaftColor = accumulated * (parameters[0].z / SAMPLE_COUNT) * parameters[1].rgb;
    outputColor[destination] = float4(min(max(loadInputColor(pixel).rgb + shaftColor, 0.0f), 65504.0f), 1.0f);
}

float4 psLightShaftsFallback(PostProcessVertex input) : SV_Target0
{
    const float2 pixel = input.position.xy;
    if (!needsShaftFallback(uint2(pixel)))
        discard;

    const float2 uv = (pixel - targetViewport.xy) / targetViewport.zw;
    const float2 stepVector = (parameters[0].xy - uv) * (0.95f / SAMPLE_COUNT);
    float2 sampleUv = uv + stepVector * (grainNoise(uint2(pixel), 0u) * 0.5f);
    float3 accumulated = 0.0f;
    float illuminationDecay = 1.0f;
    [loop]
    for (uint index = 0; index < SAMPLE_COUNT; ++index)
    {
        sampleUv += stepVector;
        if (all(sampleUv >= 0.0f) && all(sampleUv <= 1.0f))
        {
            const float2 samplePixel = targetViewport.xy + sampleUv * targetViewport.zw;
            const float3 sampleColor = max(sampleViewport(inputColor, sourceViewport, samplePixel, 0).rgb, 0.0f);
            const float3 brightColor = prefilterLightShafts(sampleColor, parameters[0].w);
            const float depth = sampleViewport(secondaryColor, secondaryViewport, samplePixel, 0).r;
            const float depthEdgeWidth = max(fwidth(depth) * 2.0f, 0.000001f);
            const float skyVisibility = smoothstep(1.0f - depthEdgeWidth, 1.0f, depth);
            const float visibility = lerp(1.0f - saturate(parameters[1].w), 1.0f, skyVisibility);
            accumulated += brightColor * (visibility * illuminationDecay);
        }
        illuminationDecay *= 0.98f;
    }
    const float3 shaftColor = accumulated * (parameters[0].z / SAMPLE_COUNT) * parameters[1].rgb;
    return float4(min(max(loadInputColor(pixel).rgb + shaftColor, 0.0f), 65504.0f), 1.0f);
}
