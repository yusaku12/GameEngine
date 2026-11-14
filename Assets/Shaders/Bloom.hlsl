#define ENGINE_POST_PROCESS_PARAMETER_COUNT 1
#include "PostProcessCompute.hlsli"

float bloomLuminance(float3 color)
{
    return max(dot(color, float3(0.2126f, 0.7152f, 0.0722f)), 0.0f);
}

float3 thresholdBloomSample(float2 pixel, float2 offset, float threshold)
{
    const float3 color = max(sampleInputOffset(pixel, offset), 0.0f);
    return threshold < 0.0f || bloomLuminance(color) >= threshold ? color : 0.0f;
}

float3 karisAverage(float3 a, float3 b, float3 c, float3 d)
{
    const float wa = rcp(4.0f + bloomLuminance(a));
    const float wb = rcp(4.0f + bloomLuminance(b));
    const float wc = rcp(4.0f + bloomLuminance(c));
    const float wd = rcp(4.0f + bloomLuminance(d));
    return (a * wa + b * wb + c * wc + d * wd) / max(wa + wb + wc + wd, 0.00001f);
}

float3 bloomTent(Texture2D<float4> image, float4 imageViewport, float2 pixel)
{
    return 0.25f * (
        sampleViewport(image, imageViewport, pixel, float2(-0.5f, -0.5f)).rgb
        + sampleViewport(image, imageViewport, pixel, float2(0.5f, -0.5f)).rgb
        + sampleViewport(image, imageViewport, pixel, float2(-0.5f, 0.5f)).rgb
        + sampleViewport(image, imageViewport, pixel, float2(0.5f, 0.5f)).rgb);
}

[numthreads(8, 8, 1)]
void csBloomPrefilter(uint3 id : SV_DispatchThreadID)
{
    uint2 destination;
    float2 pixel;
    if (!postProcessPixel(id.xy, destination, pixel))
        return;
    const float threshold = parameters[0].x;
    const float3 center = karisAverage(
        thresholdBloomSample(pixel, float2(-0.5f, -0.5f), threshold),
        thresholdBloomSample(pixel, float2(0.5f, -0.5f), threshold),
        thresholdBloomSample(pixel, float2(-0.5f, 0.5f), threshold),
        thresholdBloomSample(pixel, float2(0.5f, 0.5f), threshold));
    const float3 a = thresholdBloomSample(pixel, float2(-1.0f, -1.0f), threshold);
    const float3 b = thresholdBloomSample(pixel, float2(0.0f, -1.0f), threshold);
    const float3 c = thresholdBloomSample(pixel, float2(1.0f, -1.0f), threshold);
    const float3 d = thresholdBloomSample(pixel, float2(-1.0f, 0.0f), threshold);
    const float3 e = thresholdBloomSample(pixel, float2(0.0f, 0.0f), threshold);
    const float3 f = thresholdBloomSample(pixel, float2(1.0f, 0.0f), threshold);
    const float3 g = thresholdBloomSample(pixel, float2(-1.0f, 1.0f), threshold);
    const float3 h = thresholdBloomSample(pixel, float2(0.0f, 1.0f), threshold);
    const float3 i = thresholdBloomSample(pixel, float2(1.0f, 1.0f), threshold);
    const float3 result = center * 0.5f
        + (karisAverage(a, b, d, e) + karisAverage(c, b, e, f)
            + karisAverage(g, d, e, h) + karisAverage(i, e, f, h)) * 0.125f;
    outputColor[destination] = float4(min(max(result, 0.0f), 65504.0f), 1.0f);
}

[numthreads(8, 8, 1)]
void csBloomDownsample(uint3 id : SV_DispatchThreadID)
{
    uint2 destination;
    float2 pixel;
    if (!postProcessPixel(id.xy, destination, pixel))
        return;
    const float2 direction = parameters[0].x < 0.5f ? float2(1.0f, 0.0f) : float2(0.0f, 1.0f);
    const float radius = max(parameters[0].y, 0.0f);
    float3 result = sampleInputOffset(pixel, 0.0f) * 0.2270270270f;
    result += (sampleInputOffset(pixel, direction * (1.3846153846f * radius))
        + sampleInputOffset(pixel, -direction * (1.3846153846f * radius))) * 0.3162162162f;
    result += (sampleInputOffset(pixel, direction * (3.2307692308f * radius))
        + sampleInputOffset(pixel, -direction * (3.2307692308f * radius))) * 0.0702702703f;
    outputColor[destination] = float4(min(max(result, 0.0f), 65504.0f), 1.0f);
}

[numthreads(8, 8, 1)]
void csBloomUpsample(uint3 id : SV_DispatchThreadID)
{
    uint2 destination;
    float2 pixel;
    if (!postProcessPixel(id.xy, destination, pixel))
        return;
    const float3 high = loadInputColor(pixel).rgb;
    const float3 low = bloomTent(secondaryColor, secondaryViewport, pixel);
    outputColor[destination] = float4(min(max(high + low * saturate(parameters[0].x), 0.0f), 65504.0f), 1.0f);
}

[numthreads(8, 8, 1)]
void csBloomComposite(uint3 id : SV_DispatchThreadID)
{
    uint2 destination;
    float2 pixel;
    if (!postProcessPixel(id.xy, destination, pixel))
        return;
    const float3 bloom = bloomTent(secondaryColor, secondaryViewport, pixel);
    outputColor[destination] = float4(min(max(loadInputColor(pixel).rgb + bloom * parameters[0].x, 0.0f),
        65504.0f), 1.0f);
}
