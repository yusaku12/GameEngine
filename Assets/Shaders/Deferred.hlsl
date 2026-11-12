// Compiled by DXC with Shader Model 6.x and HLSL 2021.
#include "ColorOutput.hlsli"
struct SceneLight
{
    float4 positionKind;
    float4 colorRange;
    float4 directionInner;
    float4 parameters;
};

struct AtmosphereSun
{
    float4 directionEnabled;
    float4 radiance;
};

cbuffer LightingConstants : register(b4)
{
    row_major float4x4 shadowViewProjection;
    float4 cameraPosition;
    float4 shadowOptions;
    float4 environment;
    SceneLight lights[32];
    float4 atmosphereOptions;
    float4 atmosphereRayleigh;
    float4 atmosphereMie;
    float4 atmosphereAbsorption;
    float4 atmosphereArtistic;
    float4 atmospherePlanetCenterFromCamera;
    AtmosphereSun atmosphereSuns[2];
};

cbuffer DeferredPassConstants : register(b5)
{
    row_major float4x4 inverseViewProjection;
    float4 viewport;
    float4 renderSize;
    float4 backgroundColor;
    float4 clipExposureDebug;
};

Texture2D<float4> albedoTexture : register(t0);
Texture2D<float4> normalTexture : register(t1);
Texture2D<float4> materialTexture : register(t2);
Texture2D<float4> emissionTexture : register(t3);
Texture2D<float> depthTexture : register(t4);
Texture2D<float> shadowTexture : register(t6);
SamplerComparisonState shadowSampler : register(s1);

struct VertexOutput
{
    float4 position : SV_Position;
};

VertexOutput vsMain(uint vertexID : SV_VertexID)
{
    const float2 corner = float2((vertexID << 1) & 2, vertexID & 2);
    VertexOutput output;
    output.position = float4(corner * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

bool reconstructWorldPosition(float2 pixelPosition, float depth, out float3 position)
{
    const float2 uv = (pixelPosition - viewport.xy) / max(viewport.zw, 1.0f);
    const float4 clipPosition = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), depth, 1.0f);
    const float4 worldPosition = mul(clipPosition, inverseViewProjection);
    if (!all(isfinite(worldPosition)) || abs(worldPosition.w) <= 0.00001f)
    {
        position = 0.0f;
        return false;
    }
    position = worldPosition.xyz / worldPosition.w;
    return all(isfinite(position));
}

bool intersectSphere(float3 origin, float3 direction, float radius, out float nearDistance, out float farDistance)
{
    const float projection = dot(origin, direction);
    const float discriminant = projection * projection - (dot(origin, origin) - radius * radius);
    if (!isfinite(discriminant) || discriminant < 0.0f)
    {
        nearDistance = 0.0f;
        farDistance = 0.0f;
        return false;
    }
    const float root = sqrt(discriminant);
    nearDistance = -projection - root;
    farDistance = -projection + root;
    return farDistance >= 0.0f;
}

void atmosphereDensity(float3 position, out float rayleighDensity, out float mieDensity, out float ozoneDensity)
{
    const float altitude = max(length(position) - atmosphereOptions.y, 0.0f);
    rayleighDensity = exp(-min(altitude / atmosphereRayleigh.w, 80.0f));
    mieDensity = exp(-min(altitude / atmosphereMie.w, 80.0f));
    ozoneDensity = saturate(1.0f - abs(altitude - 25.0f) / 15.0f);
}

float3 atmosphereTransmittance(float3 origin, float3 direction)
{
    const float groundRadius = atmosphereOptions.y;
    const float radius = length(origin);
    if (!isfinite(radius) || radius < groundRadius)
        return 0.0f;
    const float3 up = origin / radius;
    const float cosineZenith = dot(up, direction);
    const float horizonCosine = -sqrt(max(1.0f - groundRadius * groundRadius / (radius * radius), 0.0f));
    if (cosineZenith <= horizonCosine)
        return 0.0f;
    const float altitude = max(radius - groundRadius, 0.0f);
    const float rayleighDensity = exp(-min(altitude / atmosphereRayleigh.w, 80.0f));
    const float mieDensity = exp(-min(altitude / atmosphereMie.w, 80.0f));
    const float ozoneDensity = saturate(1.0f - abs(altitude - 25.0f) / 15.0f);
    const float rayleighPath = min(atmosphereRayleigh.w / max(cosineZenith, 0.02f),
        sqrt(6.2831853f * groundRadius * atmosphereRayleigh.w));
    const float miePath = min(atmosphereMie.w / max(cosineZenith, 0.02f),
        sqrt(6.2831853f * groundRadius * atmosphereMie.w));
    const float ozonePath = min(15.0f / max(cosineZenith, 0.02f),
        sqrt(6.2831853f * groundRadius * 15.0f));
    const float3 opticalDepth = atmosphereRayleigh.rgb * (rayleighDensity * rayleighPath)
        + (atmosphereMie.x + atmosphereMie.y).xxx * (mieDensity * miePath)
        + atmosphereAbsorption.rgb * atmosphereAbsorption.w * (ozoneDensity * ozonePath);
    return exp(-min(max(opticalDepth, 0.0f), 80.0f));
}

float3 integrateAtmosphere(float3 origin, float3 viewDirection, float maximumDistance,
    out float3 transmittance)
{
    const float groundRadius = atmosphereOptions.y;
    const float outerRadius = groundRadius + atmosphereOptions.z;
    const float cameraRadius = length(origin);
    if (!isfinite(cameraRadius) || cameraRadius < groundRadius)
    {
        transmittance = 1.0f;
        return 0.0f;
    }
    float outerNear;
    float outerFar;
    if (!intersectSphere(origin, viewDirection, outerRadius, outerNear, outerFar))
    {
        transmittance = 1.0f;
        return 0.0f;
    }

    const float start = max(outerNear, 0.0f);
    float end = min(outerFar, maximumDistance);
    float groundNear;
    float groundFar;
    if (intersectSphere(origin, viewDirection, groundRadius, groundNear, groundFar))
    {
        if (groundNear <= start + 0.0001f && groundFar > start)
            end = start;
        else if (groundNear > start && groundNear < end)
            end = groundNear;
    }
    const float segmentLength = max(end - start, 0.0f);
    if (segmentLength <= 0.0f)
    {
        transmittance = 1.0f;
        return 0.0f;
    }

    float3 accumulatedRayleigh = 0.0f;
    float3 accumulatedMie = 0.0f;
    float opticalRayleigh = 0.0f;
    float opticalMie = 0.0f;
    float opticalOzone = 0.0f;
    const float stepLength = segmentLength / 12.0f;
    const float anisotropy = atmosphereMie.z;
    [loop]
    for (uint index = 0; index < 12; ++index)
    {
        const float3 samplePosition = origin + viewDirection * (start + (index + 0.5f) * stepLength);
        float densityRayleigh;
        float densityMie;
        float densityOzone;
        atmosphereDensity(samplePosition, densityRayleigh, densityMie, densityOzone);
        opticalRayleigh += densityRayleigh * stepLength;
        opticalMie += densityMie * stepLength;
        opticalOzone += densityOzone * stepLength;

        const float3 viewOpticalDepth = atmosphereRayleigh.rgb * opticalRayleigh
            + (atmosphereMie.x + atmosphereMie.y).xxx * opticalMie
            + atmosphereAbsorption.rgb * atmosphereAbsorption.w * opticalOzone;
        const float3 viewTransmittance = exp(-min(max(viewOpticalDepth, 0.0f), 80.0f));
        [unroll]
        for (uint sunIndex = 0; sunIndex < 2; ++sunIndex)
        {
            const AtmosphereSun sun = atmosphereSuns[sunIndex];
            if (sun.directionEnabled.w <= 0.0f)
                continue;
            const float3 sunDirection = normalize(sun.directionEnabled.xyz);
            const float cosineSunAngle = dot(viewDirection, sunDirection);
            const float rayleighPhase = 3.0f / (16.0f * 3.14159265f)
                * (1.0f + cosineSunAngle * cosineSunAngle);
            const float mieDenominator = max(1.0f + anisotropy * anisotropy
                - 2.0f * anisotropy * cosineSunAngle, 0.0001f);
            const float miePhase = (1.0f - anisotropy * anisotropy)
                / (4.0f * 3.14159265f * mieDenominator * sqrt(mieDenominator));
            const float3 sunTransmittance = atmosphereTransmittance(samplePosition, sunDirection);
            const float3 incidentRadiance = sun.radiance.rgb * atmosphereArtistic.w * sunTransmittance;
            accumulatedRayleigh += viewTransmittance * incidentRadiance * atmosphereRayleigh.rgb
                * (densityRayleigh * rayleighPhase * stepLength);
            accumulatedMie += viewTransmittance * incidentRadiance * atmosphereMie.x
                * (densityMie * miePhase * stepLength);
        }
    }
    const float3 opticalDepth = atmosphereRayleigh.rgb * opticalRayleigh
        + (atmosphereMie.x + atmosphereMie.y).xxx * opticalMie
        + atmosphereAbsorption.rgb * atmosphereAbsorption.w * opticalOzone;
    transmittance = exp(-min(max(opticalDepth, 0.0f), 80.0f));
    return min(max(accumulatedRayleigh + accumulatedMie, 0.0f), 65504.0f);
}

float3 evaluateAtmosphereSky(float2 pixelPosition)
{
    float3 pointOnRay;
    if (!reconstructWorldPosition(pixelPosition, 0.5f, pointOnRay))
        return backgroundColor.rgb;
    const float3 viewDirection = normalize(pointOnRay - cameraPosition.xyz);
    const float3 planetCenterFromCamera = atmospherePlanetCenterFromCamera.xyz;
    const float3 cameraFromPlanetCenter = -planetCenterFromCamera;
    float3 transmittance;
    float3 radiance = integrateAtmosphere(cameraFromPlanetCenter, viewDirection, 1.0e20f, transmittance);
    const float cosineSunRadius = cos(radians(atmosphereArtistic.z));
    [unroll]
    for (uint sunIndex = 0; sunIndex < 2; ++sunIndex)
    {
        const AtmosphereSun sun = atmosphereSuns[sunIndex];
        if (sun.directionEnabled.w <= 0.0f)
            continue;
        const float cosine = dot(viewDirection, normalize(sun.directionEnabled.xyz));
        const float disk = smoothstep(cosineSunRadius, cos(radians(atmosphereArtistic.z * 0.9f)), cosine);
        radiance += sun.radiance.rgb * atmosphereArtistic.w * atmosphereArtistic.y * disk * transmittance;
    }
    return min(max(radiance, 0.0f), 65504.0f);
}

float shadowVisibility(float3 position, float3 normal)
{
    if (shadowOptions.x == 0.0f)
        return 1.0f;
    const float4 projected = mul(float4(position + normal * shadowOptions.z, 1.0f), shadowViewProjection);
    if (!all(isfinite(projected)) || projected.w <= 0.00001f)
        return 1.0f;
    const float3 coordinate = projected.xyz / projected.w;
    const float2 uv = coordinate.xy * float2(0.5f, -0.5f) + 0.5f;
    if (!all(isfinite(coordinate)) || coordinate.z <= 0.0f || coordinate.z >= 1.0f
        || any(uv < 0.0f) || any(uv > 1.0f))
        return 1.0f;
    float visibility = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            visibility += shadowTexture.SampleCmpLevelZero(shadowSampler,
                uv + float2(x, y) * shadowOptions.w, coordinate.z - shadowOptions.y);
    return visibility / 9.0f;
}

float3 evaluateLighting(float3 position, float3 albedo, float3 normal, float3 material,
    float3 emission)
{
    const float metallic = saturate(material.x);
    const float roughness = saturate(material.y);
    const float occlusion = saturate(material.z);
    const float3 viewOffset = cameraPosition.xyz - position;
    const float3 viewDirection = viewOffset / max(length(viewOffset), 0.0001f);
    const float3 f0 = lerp(0.04f.xxx, albedo, metallic);
    const float roughnessSquared = pow(max(roughness, 0.04f), 4.0f);
    float3 color = albedo * environment.rgb * occlusion;

    for (uint index = 0; index < min((uint)environment.w, 32u); ++index)
    {
        const SceneLight light = lights[index];
        float3 direction = -light.directionInner.xyz;
        float attenuation = 1.0f;
        if (light.positionKind.w != 0.0f)
        {
            const float3 offset = light.positionKind.xyz - position;
            const float distance = length(offset);
            if (light.colorRange.w <= 0.0f || distance >= light.colorRange.w)
                continue;
            direction = offset / max(distance, 0.0001f);
            const float fade = saturate(1.0f - pow(distance / light.colorRange.w, 4.0f));
            attenuation = fade * fade / max(distance * distance, 1.0f);
            if (light.positionKind.w == 2.0f)
                attenuation *= smoothstep(light.parameters.x, light.directionInner.w,
                    dot(-direction, light.directionInner.xyz));
        }

        const float nDotL = saturate(dot(normal, direction));
        if (nDotL <= 0.0f)
            continue;
        const float nDotV = max(saturate(dot(normal, viewDirection)), 0.001f);
        const float3 halfOffset = direction + viewDirection;
        const float3 halfVector = halfOffset / max(length(halfOffset), 0.0001f);
        const float nDotH = saturate(dot(normal, halfVector));
        const float denominator = nDotH * nDotH * (roughnessSquared - 1.0f) + 1.0f;
        const float distribution = roughnessSquared
            / max(3.14159265f * denominator * denominator, 0.00001f);
        const float k = pow(roughness + 1.0f, 2.0f) / 8.0f;
        const float geometry = nDotV / (nDotV * (1.0f - k) + k)
            * nDotL / max(nDotL * (1.0f - k) + k, 0.0001f);
        const float3 fresnel = f0 + (1.0f - f0)
            * pow(1.0f - saturate(dot(viewDirection, halfVector)), 5.0f);
        const float3 specular = distribution * geometry * fresnel
            / max(4.0f * nDotV * nDotL, 0.001f);
        const float3 diffuse = (1.0f - fresnel) * (1.0f - metallic) * albedo / 3.14159265f;
        const float visibility = light.parameters.y != 0.0f
            ? shadowVisibility(position, normal) : 1.0f;
        color += (diffuse + specular) * light.colorRange.rgb * nDotL * attenuation * visibility;
    }

    return color + emission;
}

float4 psLighting(VertexOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    const float depth = depthTexture.Load(int3(pixel, 0));
    if (!isfinite(depth) || depth < 0.0f || depth >= 1.0f)
    {
        if (atmosphereOptions.x > 0.0f && renderSize.z > 0.0f)
            return float4(evaluateAtmosphereSky(input.position.xy), backgroundColor.a);
        return backgroundColor;
    }

    float3 position;
    if (!reconstructWorldPosition(input.position.xy, depth, position))
        return backgroundColor;
    const float4 packedNormal = normalTexture.Load(int3(pixel, 0));
    const float normalLength = length(packedNormal.xyz);
    if (!all(isfinite(position)) || !all(isfinite(packedNormal.xyz)) || normalLength <= 0.0001f)
        return backgroundColor;
    const float3 normal = packedNormal.xyz / normalLength;
    const float4 albedo = albedoTexture.Load(int3(pixel, 0));
    const float4 material = materialTexture.Load(int3(pixel, 0));
    const float3 emission = emissionTexture.Load(int3(pixel, 0)).rgb;
    float3 result = evaluateLighting(position, albedo.rgb, normal, material.rgb, emission);
    if (atmosphereOptions.x > 0.0f)
    {
        const float3 worldOffset = position - cameraPosition.xyz;
        const float distanceKm = length(worldOffset) / max(atmosphereOptions.w, 0.0001f)
            * atmosphereArtistic.x;
        const float3 viewDirection = normalize(worldOffset);
        const float3 cameraFromPlanetCenter = -atmospherePlanetCenterFromCamera.xyz;
        const float3 rayOrigin = cameraFromPlanetCenter;
        float3 transmittance;
        const float3 inScattering = integrateAtmosphere(rayOrigin, viewDirection, distanceKm, transmittance);
        result = result * transmittance + inScattering;
    }
    return float4(clamp(result, 0.0f, 65504.0f), albedo.a);
}

float4 psDebug(VertexOutput input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    const uint debugView = (uint)clipExposureDebug.w;
    if (debugView == 1u)
    {
        const float4 albedo = albedoTexture.Load(int3(pixel, 0));
        return float4(encodeSrgb(albedo.rgb), albedo.a);
    }
    if (debugView == 2u)
    {
        const float depth = depthTexture.Load(int3(pixel, 0));
        const float3 normal = normalTexture.Load(int3(pixel, 0)).xyz;
        if (depth >= 1.0f || !all(isfinite(normal)) || dot(normal, normal) <= 0.0001f)
            return float4(0.0f, 0.0f, 0.0f, 1.0f);
        return float4(encodeSrgb(normalize(normal) * 0.5f + 0.5f), 1.0f);
    }
    if (debugView >= 3u && debugView <= 5u)
    {
        const float4 material = materialTexture.Load(int3(pixel, 0));
        const float channel = debugView == 3u ? material.x : debugView == 4u ? material.y : material.z;
        return float4(encodeSrgb(channel.xxx), 1.0f);
    }
    if (debugView == 6u)
        return float4(encodeSrgb(toneMap(emissionTexture.Load(int3(pixel, 0)).rgb, clipExposureDebug.z)), 1.0f);
    if (debugView == 7u)
    {
        const float depth = depthTexture.Load(int3(pixel, 0));
        const float nearClip = max(clipExposureDebug.x, 0.0001f);
        const float farClip = max(clipExposureDebug.y, nearClip + 0.0001f);
        const float linearDepth = nearClip * farClip / max(farClip - depth * (farClip - nearClip), 0.0001f);
        const float visibleDepth = saturate(linearDepth / farClip);
        return float4(encodeSrgb(visibleDepth.xxx), 1.0f);
    }
    if (debugView == 8u)
    {
        if (shadowOptions.x == 0.0f)
            return float4(1.0f, 1.0f, 1.0f, 1.0f);
        const float2 uv = (input.position.xy - viewport.xy) / max(viewport.zw, 1.0f);
        const int2 shadowPixel = clamp(int2(uv * 2048.0f), 0, 2047);
        const float shadowDepth = shadowTexture.Load(int3(shadowPixel, 0));
        return float4(encodeSrgb(shadowDepth.xxx), 1.0f);
    }
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}
