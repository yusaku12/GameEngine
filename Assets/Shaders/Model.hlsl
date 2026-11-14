// Compiled by DXC with Shader Model 6.x and HLSL 2021.
struct ObjectConstants
{
    row_major float4x4 worldViewProjection;
    row_major float4x4 worldMatrix;
    row_major float4x4 previousWorldViewProjection;
};

StructuredBuffer<ObjectConstants> objectInstances : register(t5);

static const uint MAX_SKINNING_BONES = 256;

cbuffer SkinningConstants : register(b1)
{
    row_major float4x4 boneMatrices[MAX_SKINNING_BONES];
    row_major float4x4 boneNormalMatrices[MAX_SKINNING_BONES];
    row_major float4x4 previousBoneMatrices[MAX_SKINNING_BONES];
};

cbuffer CachedSkinningConstants : register(b5)
{
    uint useCachedSkinning;
};

struct TerrainLayerConstants
{
    float4 color;
    float4 parameters;
};

cbuffer MaterialConstants : register(b2)
{
    float4 baseColor;
    float metallic;
    float roughness;
    float emissiveIntensity;
    float normalScale;
    float3 emissiveColor;
    float occlusionStrength;
    float alphaCutoff;
    float3 materialPadding;
    float4 terrainOptions;
    TerrainLayerConstants terrainLayers[4];
};

cbuffer MaterialProperties : register(b3)
{
    float4 propertyBaseColor;
    float propertyMetallic;
    float propertyRoughness;
    float propertyEmissiveIntensity;
    float propertyNormalScale;
    float3 propertyEmissiveColor;
    float propertyOcclusionStrength;
    float propertyAlphaCutoff;
    float3 propertyPadding;
    uint propertyOverrideMask;
};

static const uint PROPERTY_BASE_COLOR = 1u << 0;
static const uint PROPERTY_METALLIC = 1u << 1;
static const uint PROPERTY_ROUGHNESS = 1u << 2;
static const uint PROPERTY_EMISSIVE_COLOR = 1u << 3;
static const uint PROPERTY_EMISSIVE_INTENSITY = 1u << 4;
static const uint PROPERTY_NORMAL_SCALE = 1u << 5;
static const uint PROPERTY_OCCLUSION_STRENGTH = 1u << 6;
static const uint PROPERTY_ALPHA_CUTOFF = 1u << 7;

Texture2D<float4> baseColorTexture : register(t0);
Texture2D<float4> normalTexture : register(t1);
Texture2D<float4> metallicRoughnessTexture : register(t2);
Texture2D<float4> ambientOcclusionTexture : register(t3);
Texture2D<float4> emissiveTexture : register(t4);
SamplerState linearSampler : register(s0);
Texture2DArray<float> shadowTexture : register(t6);
Texture2D<float4> terrainBase0 : register(t7);
Texture2D<float4> terrainNormal0 : register(t8);
Texture2D<float4> terrainMr0 : register(t9);
Texture2D<float4> terrainBase1 : register(t10);
Texture2D<float4> terrainNormal1 : register(t11);
Texture2D<float4> terrainMr1 : register(t12);
Texture2D<float4> terrainBase2 : register(t13);
Texture2D<float4> terrainNormal2 : register(t14);
Texture2D<float4> terrainMr2 : register(t15);
Texture2D<float4> terrainBase3 : register(t16);
Texture2D<float4> terrainNormal3 : register(t17);
Texture2D<float4> terrainMr3 : register(t18);
SamplerComparisonState shadowSampler : register(s1);

struct SceneLight
{
    float4 positionKind;
    float4 colorRange;
    float4 directionInner;
    float4 parameters;
};
cbuffer LightingConstants : register(b4)
{
    row_major float4x4 shadowViewProjection[3];
    float4 cameraPosition;
    float4 shadowOptions;
    float4 shadowSplits;
    float4 shadowCameraForward;
    float4 shadowTexelWorld;
    float4 shadowDepthScale;
    float4 environment;
    SceneLight lights[32];
    float4 atmosphereOptions;
    float4 atmosphereRayleigh;
    float4 atmosphereMie;
    float4 atmosphereAbsorption;
    float4 atmosphereArtistic;
    float4 atmospherePlanetCenterFromCamera;
    float4 atmosphereSuns[4];
};

#include "CascadedShadow.hlsli"

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
    uint4 boneIndices : BLENDINDICES;
    float4 boneWeights : BLENDWEIGHT;
    float4 cachedPreviousPosition : TEXCOORD4;
    float cachedPositionW : TEXCOORD5;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
    float3 worldPosition : TEXCOORD1;
    noperspective float2 currentNdc : TEXCOORD2;
    noperspective float2 previousNdc : TEXCOORD3;
};

row_major float3x3 getNormalMatrix(row_major float3x3 transform)
{
    const float3 row0 = transform[0];
    const float3 row1 = transform[1];
    const float3 row2 = transform[2];
    const float3 cofactor0 = cross(row1, row2);
    const float3 cofactor1 = cross(row2, row0);
    const float3 cofactor2 = cross(row0, row1);
    const float determinant = dot(row0, cofactor0);
    if (abs(determinant) <= 0.00000001f)
        return float3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);

    const float determinantSign = determinant < 0.0f ? -1.0f : 1.0f;
    return float3x3(cofactor0 * determinantSign, cofactor1 * determinantSign, cofactor2 * determinantSign);
}

void skinVertex(uint4 indices, float4 weights, inout float4 position, inout float4 previousPosition,
    inout float3 normal, inout float3 tangent, inout float3 bitangent)
{
    const float totalWeight = dot(weights, 1.0f);
    if (totalWeight > 0.00001f)
    {
        const float4 normalizedWeights = weights / totalWeight;
        const uint4 boneIndices = min(indices, MAX_SKINNING_BONES - 1);
        const row_major float4x4 skinMatrix = boneMatrices[boneIndices.x] * normalizedWeights.x
            + boneMatrices[boneIndices.y] * normalizedWeights.y
            + boneMatrices[boneIndices.z] * normalizedWeights.z
            + boneMatrices[boneIndices.w] * normalizedWeights.w;
        const row_major float4x4 previousSkinMatrix = previousBoneMatrices[boneIndices.x] * normalizedWeights.x
            + previousBoneMatrices[boneIndices.y] * normalizedWeights.y
            + previousBoneMatrices[boneIndices.z] * normalizedWeights.z
            + previousBoneMatrices[boneIndices.w] * normalizedWeights.w;
        const row_major float3x3 skinNormalMatrix = (float3x3)boneNormalMatrices[boneIndices.x] * normalizedWeights.x
            + (float3x3)boneNormalMatrices[boneIndices.y] * normalizedWeights.y
            + (float3x3)boneNormalMatrices[boneIndices.z] * normalizedWeights.z
            + (float3x3)boneNormalMatrices[boneIndices.w] * normalizedWeights.w;
        position = mul(position, skinMatrix);
        previousPosition = mul(previousPosition, previousSkinMatrix);
        normal = normalize(mul(normal, skinNormalMatrix));
        tangent = normalize(mul(tangent, (float3x3)skinMatrix));
        bitangent = normalize(mul(bitangent, (float3x3)skinMatrix));
    }
}

PixelInput vsMain(VertexInput input, uint instanceID : SV_InstanceID)
{
    const ObjectConstants object = objectInstances[instanceID];
    PixelInput output;
    float4 localPosition = float4(input.position, 1.0f);
    float4 previousLocalPosition = localPosition;
    float3 localNormal = input.normal;
    float3 localTangent = input.tangent;
    float3 localBitangent = input.bitangent;
    if (useCachedSkinning != 0)
    {
        localPosition.w = input.cachedPositionW;
        previousLocalPosition = input.cachedPreviousPosition;
    }
    else
    {
        skinVertex(input.boneIndices, input.boneWeights, localPosition, previousLocalPosition,
            localNormal, localTangent, localBitangent);
    }
    output.position = mul(localPosition, object.worldViewProjection);
    output.currentNdc = output.position.xy / max(output.position.w, 0.00001f);
    const float4 previousPosition = mul(previousLocalPosition, object.previousWorldViewProjection);
    output.previousNdc = previousPosition.xy / max(previousPosition.w, 0.00001f);
    output.worldPosition = mul(localPosition, object.worldMatrix).xyz;
    const float4 resolvedBaseColor = (propertyOverrideMask & PROPERTY_BASE_COLOR) != 0
        ? propertyBaseColor : baseColor;
    output.color = terrainOptions.x > 0 ? input.color : input.color * resolvedBaseColor;
    output.texCoord = input.texCoord;
    const row_major float3x3 worldMatrix = (float3x3)object.worldMatrix;
    const row_major float3x3 worldNormalMatrix = getNormalMatrix(worldMatrix);
    output.normal = normalize(mul(localNormal, worldNormalMatrix));
    output.tangent = normalize(mul(localTangent, worldMatrix));
    output.bitangent = normalize(mul(localBitangent, worldMatrix));
    return output;
}

ByteAddressBuffer skinningInput : register(t20);
RWByteAddressBuffer skinningOutput : register(u0);
cbuffer SkinningDispatchConstants : register(b6)
{
    uint skinningVertexCount;
    uint skinningGroupsX;
};

[numthreads(64, 1, 1)]
void csSkinning(uint3 group : SV_GroupID, uint threadIndex : SV_GroupIndex)
{
    const uint vertex = (group.y * skinningGroupsX + group.x) * 64 + threadIndex;
    if (vertex >= skinningVertexCount)
        return;

    const uint source = vertex * 96;
    const uint destination = vertex * 116;
    [unroll] for (uint offset = 48; offset < 96; offset += 16)
        skinningOutput.Store4(destination + offset, skinningInput.Load4(source + offset));

    float4 position = float4(asfloat(skinningInput.Load3(source)), 1.0f);
    float4 previousPosition = position;
    float3 normal = asfloat(skinningInput.Load3(source + 12));
    float3 tangent = asfloat(skinningInput.Load3(source + 24));
    float3 bitangent = asfloat(skinningInput.Load3(source + 36));
    const uint2 packedIndices = skinningInput.Load2(source + 72);
    const uint4 indices = uint4(packedIndices.x & 0xffff, packedIndices.x >> 16,
        packedIndices.y & 0xffff, packedIndices.y >> 16);
    const float4 weights = asfloat(skinningInput.Load4(source + 80));
    skinVertex(indices, weights, position, previousPosition, normal, tangent, bitangent);
    skinningOutput.Store3(destination, asuint(position.xyz));
    skinningOutput.Store3(destination + 12, asuint(normal));
    skinningOutput.Store3(destination + 24, asuint(tangent));
    skinningOutput.Store3(destination + 36, asuint(bitangent));
    skinningOutput.Store4(destination + 96, asuint(previousPosition));
    skinningOutput.Store(destination + 112, asuint(position.w));
}

struct SurfaceSample
{
    float4 albedo;
    float metallic;
    float roughness;
    float3 tangentNormal;
};

float3 safeTangentNormal(float3 normal)
{
    return dot(normal, normal) > 0.00000001f ? normalize(normal) : float3(0, 0, 1);
}

void accumulateTerrainLayer(uint index, Texture2D<float4> baseMap, Texture2D<float4> normalMap,
    Texture2D<float4> mrMap, float2 baseUv, float weight, inout SurfaceSample surface)
{
    if (index >= (uint)terrainOptions.x)
        return;
    const TerrainLayerConstants layer = terrainLayers[index];
    const float2 uv = baseUv * layer.parameters.w;
    surface.albedo.rgb += weight * layer.color.rgb * baseMap.Sample(linearSampler, uv).rgb;
    const float4 mr = mrMap.Sample(linearSampler, uv);
    surface.metallic += weight * saturate(layer.parameters.x * mr.b);
    surface.roughness += weight * saturate(layer.parameters.y * mr.g);
    float3 normal = normalMap.Sample(linearSampler, uv).xyz * 2 - 1;
    normal.xy *= layer.parameters.z;
    surface.tangentNormal += weight * safeTangentNormal(normal);
}

SurfaceSample sampleSurface(PixelInput input)
{
    SurfaceSample surface;
    if (terrainOptions.x > 0)
    {
        float4 weights = saturate(input.color);
        weights /= max(dot(weights, 1.0f), 0.000001f);
        surface.albedo = float4(0, 0, 0, 1);
        surface.metallic = 0;
        surface.roughness = 0;
        surface.tangentNormal = 0;
        accumulateTerrainLayer(0, terrainBase0, terrainNormal0, terrainMr0, input.texCoord, weights.x, surface);
        accumulateTerrainLayer(1, terrainBase1, terrainNormal1, terrainMr1, input.texCoord, weights.y, surface);
        accumulateTerrainLayer(2, terrainBase2, terrainNormal2, terrainMr2, input.texCoord, weights.z, surface);
        accumulateTerrainLayer(3, terrainBase3, terrainNormal3, terrainMr3, input.texCoord, weights.w, surface);
        surface.tangentNormal = safeTangentNormal(surface.tangentNormal);
        return surface;
    }
    const float resolvedMetallic = (propertyOverrideMask & PROPERTY_METALLIC) != 0 ? propertyMetallic : metallic;
    const float resolvedRoughness = (propertyOverrideMask & PROPERTY_ROUGHNESS) != 0 ? propertyRoughness : roughness;
    const float resolvedNormalScale = (propertyOverrideMask & PROPERTY_NORMAL_SCALE) != 0 ? propertyNormalScale : normalScale;
    surface.albedo = input.color * baseColorTexture.Sample(linearSampler, input.texCoord);
    const float4 mr = metallicRoughnessTexture.Sample(linearSampler, input.texCoord);
    surface.metallic = saturate(resolvedMetallic * mr.b);
    surface.roughness = saturate(resolvedRoughness * mr.g);
    surface.tangentNormal = normalTexture.Sample(linearSampler, input.texCoord).xyz * 2 - 1;
    surface.tangentNormal.xy *= resolvedNormalScale;
    return surface;
}

float4 psMain(PixelInput input) : SV_TARGET
{
    const float3 resolvedEmissiveColor = (propertyOverrideMask & PROPERTY_EMISSIVE_COLOR) != 0 ? propertyEmissiveColor : emissiveColor;
    const float resolvedEmissiveIntensity = (propertyOverrideMask & PROPERTY_EMISSIVE_INTENSITY) != 0 ? propertyEmissiveIntensity : emissiveIntensity;
    const float resolvedOcclusionStrength = (propertyOverrideMask & PROPERTY_OCCLUSION_STRENGTH) != 0 ? propertyOcclusionStrength : occlusionStrength;

    const SurfaceSample surface = sampleSurface(input);
    const float4 albedo = surface.albedo;
    const float metallicValue = surface.metallic;
    const float roughnessValue = surface.roughness;
    const float3x3 tangentBasis = float3x3(normalize(input.tangent), normalize(input.bitangent), normalize(input.normal));
    const float3 normal = normalize(mul(surface.tangentNormal, tangentBasis));
    const float aoSample = ambientOcclusionTexture.Sample(linearSampler, input.texCoord).r;
    const float occlusion = lerp(1.0f, aoSample, saturate(resolvedOcclusionStrength));
    float3 color = albedo.rgb * environment.rgb * occlusion;
    float3 viewDirection = cameraPosition.xyz - input.worldPosition;
    viewDirection /= max(length(viewDirection), 0.0001f);
    const float3 f0 = lerp(0.04f.xxx, albedo.rgb, metallicValue);
    const float roughnessSquared = pow(max(roughnessValue, 0.04f), 4);
    for (uint index = 0; index < min((uint)environment.w, 32u); ++index)
    {
        const SceneLight light = lights[index];
        float3 direction = -light.directionInner.xyz;
        float attenuation = 1;
        if (light.positionKind.w != 0)
        {
            float3 offset = light.positionKind.xyz - input.worldPosition;
            float distance = length(offset);
            direction = offset / max(distance, 0.0001f);
            float fade = saturate(1 - pow(distance / light.colorRange.w, 4));
            attenuation = fade * fade / max(distance * distance, 1);
            if (light.positionKind.w == 2)
                attenuation *= smoothstep(light.parameters.x, light.directionInner.w,
                    dot(-direction, light.directionInner.xyz));
        }
        float nDotL = saturate(dot(normal, direction));
        float nDotV = max(saturate(dot(normal, viewDirection)), 0.001f);
        float3 halfVector = direction + viewDirection;
        halfVector /= max(length(halfVector), 0.0001f);
        float nDotH = saturate(dot(normal, halfVector));
        float denominator = nDotH * nDotH * (roughnessSquared - 1) + 1;
        float distribution = roughnessSquared / max(3.14159265f * denominator * denominator, 0.00001f);
        float k = pow(roughnessValue + 1, 2) / 8;
        float geometry = nDotV / (nDotV * (1 - k) + k) * nDotL / max(nDotL * (1 - k) + k, 0.0001f);
        float3 fresnel = f0 + (1 - f0) * pow(1 - saturate(dot(viewDirection, halfVector)), 5);
        float3 specular = distribution * geometry * fresnel / max(4 * nDotV * nDotL, 0.001f);
        float3 diffuse = (1 - fresnel) * (1 - metallicValue) * albedo.rgb / 3.14159265f;
        float visibility = light.parameters.y != 0 ? shadowVisibility(input.worldPosition, normal) : 1;
        color += (diffuse + specular) * light.colorRange.rgb * nDotL * attenuation * visibility;
    }
    const float3 emissive = emissiveTexture.Sample(linearSampler, input.texCoord).rgb
        * resolvedEmissiveColor * resolvedEmissiveIntensity;
    return float4(color + emissive, albedo.a);
}

float4 psAlphaTest(PixelInput input) : SV_TARGET
{
    const float4 color = input.color * baseColorTexture.Sample(linearSampler, input.texCoord);
    const float resolvedAlphaCutoff = (propertyOverrideMask & PROPERTY_ALPHA_CUTOFF) != 0
        ? propertyAlphaCutoff : alphaCutoff;
    clip(color.a - resolvedAlphaCutoff);
    return psMain(input);
}

struct GBufferOutput
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 material : SV_Target2;
    float4 emission : SV_Target3;
    float4 velocity : SV_Target4;
};

GBufferOutput psGBuffer(PixelInput input)
{
    const float3 resolvedEmissiveColor = (propertyOverrideMask & PROPERTY_EMISSIVE_COLOR) != 0
        ? propertyEmissiveColor : emissiveColor;
    const float resolvedEmissiveIntensity = (propertyOverrideMask & PROPERTY_EMISSIVE_INTENSITY) != 0
        ? propertyEmissiveIntensity : emissiveIntensity;
    const float resolvedOcclusionStrength = (propertyOverrideMask & PROPERTY_OCCLUSION_STRENGTH) != 0
        ? propertyOcclusionStrength : occlusionStrength;

    const SurfaceSample surface = sampleSurface(input);
    const float4 albedo = surface.albedo;
    const float metallicValue = surface.metallic;
    const float roughnessValue = surface.roughness;
    const float3x3 tangentBasis = float3x3(normalize(input.tangent), normalize(input.bitangent), normalize(input.normal));
    const float3 normal = normalize(mul(surface.tangentNormal, tangentBasis));
    const float aoSample = ambientOcclusionTexture.Sample(linearSampler, input.texCoord).r;
    const float occlusion = lerp(1.0f, aoSample, saturate(resolvedOcclusionStrength));
    const float3 emission = emissiveTexture.Sample(linearSampler, input.texCoord).rgb
        * resolvedEmissiveColor * resolvedEmissiveIntensity;

    GBufferOutput output;
    output.albedo = albedo;
    output.normal = float4(normal, 1.0f);
    output.material = float4(metallicValue, roughnessValue, occlusion, 1.0f);
    output.emission = float4(emission, 1.0f);
    output.velocity = float4(
        (input.previousNdc.x - input.currentNdc.x) * 0.5f,
        (input.currentNdc.y - input.previousNdc.y) * 0.5f,
        input.position.z, 1.0f);
    return output;
}

GBufferOutput psGBufferAlphaTest(PixelInput input)
{
    const float4 color = input.color * baseColorTexture.Sample(linearSampler, input.texCoord);
    const float resolvedAlphaCutoff = (propertyOverrideMask & PROPERTY_ALPHA_CUTOFF) != 0
        ? propertyAlphaCutoff : alphaCutoff;
    clip(color.a - resolvedAlphaCutoff);
    return psGBuffer(input);
}

void psDepthAlphaTest(PixelInput input)
{
    const float alpha = input.color.a * baseColorTexture.Sample(linearSampler, input.texCoord).a;
    const float resolvedAlphaCutoff = (propertyOverrideMask & PROPERTY_ALPHA_CUTOFF) != 0
        ? propertyAlphaCutoff : alphaCutoff;
    clip(alpha - resolvedAlphaCutoff);
}