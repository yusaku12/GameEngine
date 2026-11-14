// Depth bias is relative to each receiver volume before caster depth expansion.
float sampleShadowCascade(float3 position, float3 normal, uint cascade)
{
    float normalScale = 1.0f;
    const float firstTexel = shadowTexelWorld.x;
    const float cascadeTexel = shadowTexelWorld[cascade];
    if (isfinite(firstTexel) && isfinite(cascadeTexel)
        && firstTexel > 0.000001f && cascadeTexel > 0.0f)
        normalScale = clamp(cascadeTexel / firstTexel, 0.125f, 8.0f);

    const float4 projected = mul(float4(position + normal * (shadowOptions.z * normalScale), 1.0f),
        shadowViewProjection[cascade]);
    if (!all(isfinite(projected)) || projected.w <= 0.00001f)
        return 1.0f;
    const float3 coordinate = projected.xyz / projected.w;
    const float2 uv = coordinate.xy * float2(0.5f, -0.5f) + 0.5f;
    if (!all(isfinite(coordinate)) || coordinate.z <= 0.0f || coordinate.z >= 1.0f
        || any(uv < 0.0f) || any(uv > 1.0f))
        return 1.0f;

    const float halfTexel = shadowOptions.w * 0.5f;
    const float compareDepth = coordinate.z - shadowOptions.y * shadowDepthScale[cascade];
    if (!isfinite(compareDepth))
        return 1.0f;
    float visibility = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float2 sampleUv = clamp(uv + float2(x, y) * shadowOptions.w,
                halfTexel.xx, (1.0f - halfTexel).xx);
            visibility += shadowTexture.SampleCmpLevelZero(shadowSampler,
                float3(sampleUv, float(cascade)), compareDepth);
        }
    return visibility / 9.0f;
}

float shadowVisibility(float3 position, float3 normal)
{
    if (!all(isfinite(shadowOptions)) || shadowOptions.x <= 0.0f
        || shadowOptions.w <= 0.0f || shadowOptions.w > 1.0f
        || !all(isfinite(position)) || !all(isfinite(normal))
        || !all(isfinite(cameraPosition.xyz)) || !all(isfinite(shadowCameraForward))
        || !all(isfinite(shadowSplits)) || !isfinite(shadowTexelWorld.w)
        || shadowSplits.w < 0.0f || shadowSplits.w > 1.0f
        || shadowTexelWorld.w < 0.0f || shadowTexelWorld.w > 1.0f
        || shadowCameraForward.w < 0.0f || shadowSplits.x <= shadowCameraForward.w
        || shadowSplits.y <= shadowSplits.x
        || shadowSplits.z <= shadowSplits.y)
        return 1.0f;

    const float forwardLengthSquared = dot(shadowCameraForward.xyz, shadowCameraForward.xyz);
    const float depth = dot(position - cameraPosition.xyz, shadowCameraForward.xyz);
    if (!isfinite(forwardLengthSquared) || forwardLengthSquared <= 0.000001f
        || !isfinite(depth) || depth < 0.0f || depth >= shadowSplits.z)
        return 1.0f;

    const uint cascade = depth < shadowSplits.x ? 0u : depth < shadowSplits.y ? 1u : 2u;
    float previousSplit = shadowCameraForward.w;
    if (cascade > 0u)
        previousSplit = shadowSplits[cascade - 1u];
    const float split = shadowSplits[cascade];
    const float visibility = sampleShadowCascade(position, normal, cascade);

    // Blend over the end of each cascade's interval; next projections must overlap it.
    const float fraction = cascade < 2u ? shadowSplits.w : shadowTexelWorld.w;
    const float width = (split - previousSplit) * fraction;
    if (width <= 0.0f || depth <= split - width)
        return visibility;
    const float blend = saturate((depth - (split - width)) / width);
    float nextVisibility = 1.0f;
    if (cascade < 2u)
        nextVisibility = sampleShadowCascade(position, normal, cascade + 1u);
    return lerp(visibility, nextVisibility, blend);
}
