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
Texture2D<float> shadowTexture : register(t6);
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
    row_major float4x4 shadowViewProjection;
    float4 cameraPosition;
    float4 shadowOptions;
    float4 environment;
    SceneLight lights[32];
};

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

PixelInput vsMain(VertexInput input, uint instanceID : SV_InstanceID)
{
    const ObjectConstants object = objectInstances[instanceID];
    PixelInput output;
    float4 localPosition = float4(input.position, 1.0f);
    float4 previousLocalPosition = localPosition;
    float3 localNormal = input.normal;
    float3 localTangent = input.tangent;
    float3 localBitangent = input.bitangent;
    const float totalWeight = dot(input.boneWeights, 1.0f);
    if (totalWeight > 0.00001f)
    {
        const float4 normalizedWeights = input.boneWeights / totalWeight;
        const uint4 boneIndices = min(input.boneIndices, MAX_SKINNING_BONES - 1);
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
        localPosition = mul(localPosition, skinMatrix);
        previousLocalPosition = mul(previousLocalPosition, previousSkinMatrix);
        localNormal = normalize(mul(localNormal, skinNormalMatrix));
        localTangent = normalize(mul(localTangent, (float3x3)skinMatrix));
        localBitangent = normalize(mul(localBitangent, (float3x3)skinMatrix));
    }
    output.position = mul(localPosition, object.worldViewProjection);
    output.currentNdc = output.position.xy / max(output.position.w, 0.00001f);
    const float4 previousPosition = mul(previousLocalPosition, object.previousWorldViewProjection);
    output.previousNdc = previousPosition.xy / max(previousPosition.w, 0.00001f);
    output.worldPosition = mul(localPosition, object.worldMatrix).xyz;
    const float4 resolvedBaseColor = (propertyOverrideMask & PROPERTY_BASE_COLOR) != 0
        ? propertyBaseColor : baseColor;
    output.color = input.color * resolvedBaseColor;
    output.texCoord = input.texCoord;
    const row_major float3x3 worldMatrix = (float3x3)object.worldMatrix;
    const row_major float3x3 worldNormalMatrix = getNormalMatrix(worldMatrix);
    output.normal = normalize(mul(localNormal, worldNormalMatrix));
    output.tangent = normalize(mul(localTangent, worldMatrix));
    output.bitangent = normalize(mul(localBitangent, worldMatrix));
    return output;
}

float shadowVisibility(float3 position, float3 normal)
{
    if (shadowOptions.x == 0)
        return 1;
    float4 projected = mul(float4(position + normal * shadowOptions.z, 1), shadowViewProjection);
    float3 coordinate = projected.xyz / projected.w;
    float2 uv = coordinate.xy * float2(0.5f, -0.5f) + 0.5f;
    if (coordinate.z <= 0 || coordinate.z >= 1 || any(uv < 0) || any(uv > 1))
        return 1;
    float visibility = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            visibility += shadowTexture.SampleCmpLevelZero(shadowSampler,
                uv + float2(x, y) * shadowOptions.w, coordinate.z - shadowOptions.y);
    return visibility / 9;
}

float4 psMain(PixelInput input) : SV_TARGET
{
    const float resolvedMetallic = (propertyOverrideMask & PROPERTY_METALLIC) != 0 ? propertyMetallic : metallic;
    const float resolvedRoughness = (propertyOverrideMask & PROPERTY_ROUGHNESS) != 0 ? propertyRoughness : roughness;
    const float3 resolvedEmissiveColor = (propertyOverrideMask & PROPERTY_EMISSIVE_COLOR) != 0 ? propertyEmissiveColor : emissiveColor;
    const float resolvedEmissiveIntensity = (propertyOverrideMask & PROPERTY_EMISSIVE_INTENSITY) != 0 ? propertyEmissiveIntensity : emissiveIntensity;
    const float resolvedNormalScale = (propertyOverrideMask & PROPERTY_NORMAL_SCALE) != 0 ? propertyNormalScale : normalScale;
    const float resolvedOcclusionStrength = (propertyOverrideMask & PROPERTY_OCCLUSION_STRENGTH) != 0 ? propertyOcclusionStrength : occlusionStrength;

    const float4 albedo = input.color * baseColorTexture.Sample(linearSampler, input.texCoord);
    const float4 metallicRoughnessSample = metallicRoughnessTexture.Sample(linearSampler, input.texCoord);
    const float metallicValue = saturate(resolvedMetallic * metallicRoughnessSample.b);
    const float roughnessValue = saturate(resolvedRoughness * metallicRoughnessSample.g);
    float3 tangentNormal = normalTexture.Sample(linearSampler, input.texCoord).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= resolvedNormalScale;
    const float3x3 tangentBasis = float3x3(normalize(input.tangent), normalize(input.bitangent), normalize(input.normal));
    const float3 normal = normalize(mul(tangentNormal, tangentBasis));
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
    const float resolvedMetallic = (propertyOverrideMask & PROPERTY_METALLIC) != 0 ? propertyMetallic : metallic;
    const float resolvedRoughness = (propertyOverrideMask & PROPERTY_ROUGHNESS) != 0 ? propertyRoughness : roughness;
    const float3 resolvedEmissiveColor = (propertyOverrideMask & PROPERTY_EMISSIVE_COLOR) != 0
        ? propertyEmissiveColor : emissiveColor;
    const float resolvedEmissiveIntensity = (propertyOverrideMask & PROPERTY_EMISSIVE_INTENSITY) != 0
        ? propertyEmissiveIntensity : emissiveIntensity;
    const float resolvedNormalScale = (propertyOverrideMask & PROPERTY_NORMAL_SCALE) != 0
        ? propertyNormalScale : normalScale;
    const float resolvedOcclusionStrength = (propertyOverrideMask & PROPERTY_OCCLUSION_STRENGTH) != 0
        ? propertyOcclusionStrength : occlusionStrength;

    const float4 albedo = input.color * baseColorTexture.Sample(linearSampler, input.texCoord);
    const float4 metallicRoughnessSample = metallicRoughnessTexture.Sample(linearSampler, input.texCoord);
    const float metallicValue = saturate(resolvedMetallic * metallicRoughnessSample.b);
    const float roughnessValue = saturate(resolvedRoughness * metallicRoughnessSample.g);
    float3 tangentNormal = normalTexture.Sample(linearSampler, input.texCoord).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= resolvedNormalScale;
    const float3x3 tangentBasis = float3x3(normalize(input.tangent), normalize(input.bitangent), normalize(input.normal));
    const float3 normal = normalize(mul(tangentNormal, tangentBasis));
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