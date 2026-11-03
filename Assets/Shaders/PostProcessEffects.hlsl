#include "PostProcess.hlsli"

float3 boxFilter(float2 pixel)
{
    return 0.25f * (sampleInputOffset(pixel, float2(-0.5f, -0.5f))
        + sampleInputOffset(pixel, float2(0.5f, -0.5f))
        + sampleInputOffset(pixel, float2(-0.5f, 0.5f))
        + sampleInputOffset(pixel, float2(0.5f, 0.5f)));
}

float3 karisDownsample(float2 pixel)
{
    const float3 samples[4] = {
        max(sampleInputOffset(pixel, float2(-0.5f, -0.5f)), 0.0f),
        max(sampleInputOffset(pixel, float2(0.5f, -0.5f)), 0.0f),
        max(sampleInputOffset(pixel, float2(-0.5f, 0.5f)), 0.0f),
        max(sampleInputOffset(pixel, float2(0.5f, 0.5f)), 0.0f)
    };
    float3 color = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (uint index = 0; index < 4; ++index)
    {
        const float brightness = max(samples[index].r, max(samples[index].g, samples[index].b));
        const float weight = rcp(1.0f + brightness);
        color += samples[index] * weight;
        weightSum += weight;
    }
    return color / max(weightSum, 0.00001f);
}

float4 psBloomPrefilter(PostProcessVertex input) : SV_Target0
{
    const float3 color = karisDownsample(input.position.xy);
    const float brightness = max(color.r, max(color.g, color.b));
    const float threshold = parameters[0].x;
    const float contribution = threshold < 0.0f ? 1.0f : saturate(brightness - threshold);
    return float4(min(color * contribution, 65504.0f), 1.0f);
}

float4 psBloomDownsample(PostProcessVertex input) : SV_Target0
{
    return float4(boxFilter(input.position.xy), 1.0f);
}

float4 psBloomUpsample(PostProcessVertex input) : SV_Target0
{
    float3 low = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float weight = (x == 0 ? 2.0f : 1.0f) * (y == 0 ? 2.0f : 1.0f);
            low += weight * sampleViewport(secondaryColor, secondaryViewport, input.position.xy,
                float2(x, y) * parameters[0].y).rgb;
        }
    }
    return float4(lerp(loadInputColor(input.position.xy).rgb, low / 16.0f, parameters[0].x), 1.0f);
}

float4 psBloomComposite(PostProcessVertex input) : SV_Target0
{
    const float3 bloom = sampleViewport(secondaryColor, secondaryViewport, input.position.xy, 0).rgb;
    return float4(min(max(loadInputColor(input.position.xy).rgb, 0.0f) + bloom * parameters[0].x, 65504.0f), 1.0f);
}

float4 psCustomComposite(PostProcessVertex input) : SV_Target0
{
    const float4 original = loadInputColor(input.position.xy);
    const float4 effect = sampleViewport(secondaryColor, secondaryViewport, input.position.xy, 0);
    return float4(lerp(original.rgb, effect.rgb, saturate(parameters[0].x)), original.a);
}

float4 psVignette(PostProcessVertex input) : SV_Target0
{
    const float2 uv = (input.position.xy - targetViewport.xy) / targetViewport.zw;
    float2 offset = (uv - 0.5f) * 2.0f;
    offset.x *= lerp(1.0f, parameters[0].w, parameters[0].z);
    const float radius = length(offset);
    const float falloff = smoothstep(1.0f - parameters[0].y, 1.0f, radius);
    const float strength = saturate(falloff * parameters[0].x);
    return float4(lerp(loadInputColor(input.position.xy).rgb, parameters[1].rgb, strength), 1.0f);
}

float4 psChromaticAberration(PostProcessVertex input) : SV_Target0
{
    const float2 uv = (input.position.xy - targetViewport.xy) / targetViewport.zw;
    const float2 radial = (uv - 0.5f) * 2.0f;
    const float distanceFromCenter = length(radial);
    const float onset = parameters[0].y >= 1.0f
        ? step(1.0f, distanceFromCenter) : smoothstep(parameters[0].y, 1.0f, distanceFromCenter);
    const float2 offset = radial * (parameters[0].x * 0.75f * onset);
    const float3 center = loadInputColor(input.position.xy).rgb;
    const float red = sampleInputOffset(input.position.xy, offset).r;
    const float blue = sampleInputOffset(input.position.xy, -offset).b;
    return float4(red, center.g, blue, 1.0f);
}

float grainNoise(uint2 pixel, uint frame)
{
    uint value = pixel.x * 1973u + pixel.y * 9277u + frame * 26699u + 0x68bc21ebu;
    value = (value << 13u) ^ value;
    value = value * (value * value * 15731u + 789221u) + 1376312589u;
    return float(value & 0x7fffffffu) / 1073741824.0f - 1.0f;
}

float4 psFilmGrain(PostProcessVertex input) : SV_Target0
{
    const float3 color = loadInputColor(input.position.xy).rgb;
    const float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    const float response = lerp(1.0f, 1.0f - saturate(luminance), parameters[0].y);
    const uint frame = uint(max(round(parameters[0].z), 0.0f));
    const float noise = grainNoise(uint2(input.position.xy), frame);
    return float4(max(color + noise * parameters[0].x * response * 0.08f, 0.0f), 1.0f);
}

float perceptualLuma(float3 color)
{
    // Stable edge/endpoint comparisons despite floating-point contraction differences.
    return round(sqrt(max(dot(color, float3(0.2126f, 0.7152f, 0.0722f)), 0.0f)) * 4096.0f) / 4096.0f;
}

float sampleLuma(float2 pixel, float2 offset)
{
    return perceptualLuma(sampleInputOffset(pixel, offset));
}

float4 psFxaa(PostProcessVertex input) : SV_Target0
{
    const float2 pixel = input.position.xy;
    const float3 original = loadInputColor(pixel).rgb;
    const float center = perceptualLuma(original);
    const float north = sampleLuma(pixel, float2(0, -1));
    const float south = sampleLuma(pixel, float2(0, 1));
    const float west = sampleLuma(pixel, float2(-1, 0));
    const float east = sampleLuma(pixel, float2(1, 0));
    const float minimum = min(center, min(min(north, south), min(west, east)));
    const float maximum = max(center, max(max(north, south), max(west, east)));
    const float range = maximum - minimum;
    if (range <= max(parameters[0].z, maximum * parameters[0].y))
        return float4(original, 1.0f);

    const float nw = sampleLuma(pixel, float2(-1, -1));
    const float ne = sampleLuma(pixel, float2(1, -1));
    const float sw = sampleLuma(pixel, float2(-1, 1));
    const float se = sampleLuma(pixel, float2(1, 1));
    const float horizontal = abs(nw - 2.0f * west + sw) + 2.0f * abs(north - 2.0f * center + south)
        + abs(ne - 2.0f * east + se);
    const float vertical = abs(nw - 2.0f * north + ne) + 2.0f * abs(west - 2.0f * center + east)
        + abs(sw - 2.0f * south + se);
    const bool horizontalEdge = horizontal >= vertical;
    const float negative = horizontalEdge ? north : west;
    const float positive = horizontalEdge ? south : east;
    const bool negativeSteeper = abs(negative - center) >= abs(positive - center);
    const float neighbor = negativeSteeper ? negative : positive;
    const float gradient = abs(neighbor - center);
    const float edgeLuma = (neighbor + center) * 0.5f;
    const float2 perpendicular = (horizontalEdge ? float2(0, 1) : float2(1, 0))
        * (negativeSteeper ? -1.0f : 1.0f);
    const float2 tangent = horizontalEdge ? float2(1, 0) : float2(0, 1);
    const float2 origin = perpendicular * 0.5f;
    float negativeDistance = 1.0f;
    float positiveDistance = 1.0f;
    float negativeDelta = sampleLuma(pixel, origin - tangent) - edgeLuma;
    float positiveDelta = sampleLuma(pixel, origin + tangent) - edgeLuma;
    bool negativeDone = abs(negativeDelta) >= gradient * 0.25f;
    bool positiveDone = abs(positiveDelta) >= gradient * 0.25f;
    [unroll]
    for (int step = 0; step < 8; ++step)
    {
        const float stride = step < 3 ? 1.0f : (step < 6 ? 2.0f : 4.0f);
        if (!negativeDone)
        {
            negativeDistance += stride;
            negativeDelta = sampleLuma(pixel, origin - tangent * negativeDistance) - edgeLuma;
            negativeDone = abs(negativeDelta) >= gradient * 0.25f;
        }
        if (!positiveDone)
        {
            positiveDistance += stride;
            positiveDelta = sampleLuma(pixel, origin + tangent * positiveDistance) - edgeLuma;
            positiveDone = abs(positiveDelta) >= gradient * 0.25f;
        }
    }
    const bool nearerNegative = negativeDistance <= positiveDistance;
    const float endpointDelta = nearerNegative ? negativeDelta : positiveDelta;
    const bool consistentEdge = (endpointDelta < 0) != (center - edgeLuma < 0);
    const float edgeBlend = consistentEdge
        ? 0.5f - min(negativeDistance, positiveDistance) / (negativeDistance + positiveDistance) : 0.0f;
    const float average = (2.0f * (north + south + west + east) + nw + ne + sw + se) / 12.0f;
    float subpixel = saturate(abs(average - center) / range);
    subpixel = subpixel * subpixel * (3.0f - 2.0f * subpixel);
    subpixel = subpixel * subpixel * parameters[0].x;
    const float blend = max(edgeBlend, subpixel);
    const float3 filtered = sampleInputOffset(pixel, perpendicular * blend);
    return float4(lerp(original, filtered, parameters[0].w), 1.0f);
}

float linearViewDepth(float deviceDepth, float nearClip, float farClip, bool perspective)
{
    if (!perspective)
        return lerp(nearClip, farClip, deviceDepth);
    return nearClip * farClip / max(farClip - deviceDepth * (farClip - nearClip), 0.00001f);
}

float4 psDepthOfField(PostProcessVertex input) : SV_Target0
{
    const float2 pixel = input.position.xy;
    const float3 center = loadInputColor(pixel).rgb;
    const float rawDepth = sampleViewport(secondaryColor, secondaryViewport, pixel, 0).r;
    const float distance = linearViewDepth(rawDepth, parameters[0].w, parameters[1].x,
        parameters[2].x > 0.5f);
    const float circleOfConfusion = saturate(abs(distance - parameters[0].x)
        / max(parameters[0].y, 0.0001f));
    const float radius = circleOfConfusion * parameters[0].z;
    static const float2 disk[12] = {
        float2(0.1305f, 0.9914f), float2(-0.6088f, 0.7934f),
        float2(-0.9851f, 0.1710f), float2(-0.7934f, -0.6088f),
        float2(-0.1710f, -0.9851f), float2(0.6088f, -0.7934f),
        float2(0.9239f, -0.3827f), float2(0.3827f, 0.9239f),
        float2(-0.3827f, 0.3827f), float2(0.7071f, 0.7071f),
        float2(-0.7071f, -0.7071f), float2(0.0f, 0.0f)
    };
    float3 accumulated = center;
    float weightSum = 1.0f;
    [unroll]
    for (uint index = 0; index < 12; ++index)
    {
        const float2 offset = disk[index] * radius;
        const float sampleDepth = linearViewDepth(
            sampleViewport(secondaryColor, secondaryViewport, pixel, offset).r,
            parameters[0].w, parameters[1].x, parameters[2].x > 0.5f);
        const float depthDifference = abs(sampleDepth - distance);
        const float depthWeight = exp2(-depthDifference / max(distance * 0.04f, 0.1f));
        const float weight = lerp(1.0f, depthWeight, saturate(radius));
        accumulated += sampleInputOffset(pixel, offset) * weight;
        weightSum += weight;
    }
    const float3 blur = accumulated / max(weightSum, 0.0001f);
    return float4(lerp(center, blur, saturate(circleOfConfusion * parameters[1].y)), 1.0f);
}

float4 psMotionBlur(PostProcessVertex input) : SV_Target0
{
    const float2 pixel = input.position.xy;
    const float4 centerMotion = sampleViewport(secondaryColor, secondaryViewport, pixel, 0);
    const float2 viewportSize = targetViewport.zw;
    const float2 motionPixels = clamp(centerMotion.xy * viewportSize,
        -parameters[0].y, parameters[0].y) * parameters[0].x;
    float3 accumulated = loadInputColor(pixel).rgb;
    float weightSum = 1.0f;
    [unroll]
    for (uint index = 0; index < 8; ++index)
    {
        const float amount = (index + 0.5f) / 8.0f;
        const float2 offset = motionPixels * amount;
        const float4 sampleMotion = sampleViewport(secondaryColor, secondaryViewport, pixel, offset);
        const float depthTolerance = max(0.001f, centerMotion.z * 0.01f);
        if (abs(sampleMotion.z - centerMotion.z) <= depthTolerance)
        {
            accumulated += sampleInputOffset(pixel, offset);
            weightSum += 1.0f;
        }
    }
    return float4(accumulated / weightSum, 1.0f);
}

float4 psTemporalAA(PostProcessVertex input) : SV_Target0
{
    const float2 pixel = input.position.xy;
    const float3 current = loadInputColor(pixel).rgb;
    const float4 motion = sampleViewport(secondaryColor, secondaryViewport, pixel, 0);
    const float2 historyPixel = pixel + motion.xy * targetViewport.zw;
    const float2 viewportMin = targetViewport.xy + 0.5f;
    const float2 viewportMax = targetViewport.xy + targetViewport.zw - 0.5f;
    const bool insideViewport = all(historyPixel >= viewportMin) && all(historyPixel <= viewportMax);
    float3 neighborhoodMin = current;
    float3 neighborhoodMax = current;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float3 neighbor = sampleInputOffset(pixel, float2(x, y));
            neighborhoodMin = min(neighborhoodMin, neighbor);
            neighborhoodMax = max(neighborhoodMax, neighbor);
        }
    }
    const float currentDepth = linearViewDepth(motion.z, parameters[0].z,
        parameters[0].w, parameters[1].x > 0.5f);
    if (parameters[0].y < 0.5f || !insideViewport)
        return float4(current, currentDepth);
    const float4 previous = sampleViewport(tertiaryColor, targetViewport, historyPixel, 0);
    const float depthTolerance = max(0.02f, currentDepth * 0.02f);
    const bool depthMatches = abs(previous.a - currentDepth) <= depthTolerance;
    const float3 clampedHistory = clamp(previous.rgb, neighborhoodMin, neighborhoodMax);
    const float feedback = depthMatches ? saturate(parameters[0].x) : 0.0f;
    return float4(lerp(current, clampedHistory, feedback), currentDepth);
}

float3 sampleColorGradingLut(float3 color)
{
    uint width, height;
    secondaryColor.GetDimensions(width, height);
    const float size = (float)height;
    const float blue = saturate(color.b) * (size - 1.0f);
    const float lowerSlice = floor(blue);
    const float upperSlice = min(lowerSlice + 1.0f, size - 1.0f);
    const float2 redGreen = saturate(color.rg) * (size - 1.0f);
    const float v = (redGreen.y + 0.5f) / size;
    const float lowerU = (lowerSlice * size + redGreen.x + 0.5f) / width;
    const float upperU = (upperSlice * size + redGreen.x + 0.5f) / width;
    const float3 lower = secondaryColor.SampleLevel(linearClamp, float2(lowerU, v), 0).rgb;
    const float3 upper = secondaryColor.SampleLevel(linearClamp, float2(upperU, v), 0).rgb;
    return lerp(lower, upper, blue - lowerSlice);
}

float4 psColorGradingLut(PostProcessVertex input) : SV_Target0
{
    const float4 source = loadInputColor(input.position.xy);
    const float3 graded = sampleColorGradingLut(source.rgb);
    return float4(lerp(source.rgb, graded, saturate(parameters[0].x)), source.a);
}

float logLuminance(float3 color)
{
    return log2(max(dot(max(color, 0.0f), float3(0.2126f, 0.7152f, 0.0722f)), 0.0001f));
}

float4 psAutoExposureDownsample(PostProcessVertex input) : SV_Target0
{
    const float2 pixel = input.position.xy;
    float4 values = sampleViewport(inputColor, sourceViewport, pixel, float2(-0.5f, -0.5f));
    float result = parameters[0].x > 0.5f ? logLuminance(values.rgb) : values.r;
    values = sampleViewport(inputColor, sourceViewport, pixel, float2(0.5f, -0.5f));
    result += parameters[0].x > 0.5f ? logLuminance(values.rgb) : values.r;
    values = sampleViewport(inputColor, sourceViewport, pixel, float2(-0.5f, 0.5f));
    result += parameters[0].x > 0.5f ? logLuminance(values.rgb) : values.r;
    values = sampleViewport(inputColor, sourceViewport, pixel, float2(0.5f, 0.5f));
    result += parameters[0].x > 0.5f ? logLuminance(values.rgb) : values.r;
    return float4(result * 0.25f, 0.0f, 0.0f, 1.0f);
}

float4 psAutoExposureAdapt(PostProcessVertex input) : SV_Target0
{
    const float averageLogLuminance = loadInputColor(input.position.xy).r;
    const float targetEV = clamp(log2(max(parameters[0].w, 0.0001f))
        - averageLogLuminance + parameters[1].z, parameters[1].x, parameters[1].y);
    if (parameters[1].w < 0.5f)
        return float4(targetEV, 0.0f, 0.0f, 1.0f);

    const float previousEV = secondaryColor.Load(int3(0, 0, 0)).r;
    const float speed = targetEV > previousEV ? parameters[0].y : parameters[0].z;
    const float blend = 1.0f - exp(-speed * max(parameters[0].x, 0.0f));
    return float4(lerp(previousEV, targetEV, blend), 0.0f, 0.0f, 1.0f);
}
