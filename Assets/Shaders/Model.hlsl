// Compiled by DXC with Shader Model 6.x and HLSL 2021.
cbuffer ObjectConstants : register(b0)
{
    row_major float4x4 worldViewProjection;
    row_major float4x4 worldMatrix;
};

static const uint MAX_SKINNING_BONES = 256;

cbuffer SkinningConstants : register(b1)
{
    row_major float4x4 boneMatrices[MAX_SKINNING_BONES];
    row_major float4x4 boneNormalMatrices[MAX_SKINNING_BONES];
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
};

PixelInput vsMain(VertexInput input)
{
    PixelInput output;
    float4 localPosition = float4(input.position, 1.0f);
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
        const row_major float3x3 skinNormalMatrix = (float3x3)boneNormalMatrices[boneIndices.x] * normalizedWeights.x
            + (float3x3)boneNormalMatrices[boneIndices.y] * normalizedWeights.y
            + (float3x3)boneNormalMatrices[boneIndices.z] * normalizedWeights.z
            + (float3x3)boneNormalMatrices[boneIndices.w] * normalizedWeights.w;
        localPosition = mul(localPosition, skinMatrix);
        localNormal = normalize(mul(localNormal, skinNormalMatrix));
        localTangent = normalize(mul(localTangent, skinNormalMatrix));
        localBitangent = normalize(mul(localBitangent, skinNormalMatrix));
    }
    output.position = mul(localPosition, worldViewProjection);
    const float4 resolvedBaseColor = (propertyOverrideMask & PROPERTY_BASE_COLOR) != 0
        ? propertyBaseColor : baseColor;
    output.color = input.color * resolvedBaseColor;
    output.texCoord = input.texCoord;
    output.normal = normalize(mul(localNormal, (float3x3)worldMatrix));
    output.tangent = normalize(mul(localTangent, (float3x3)worldMatrix));
    output.bitangent = normalize(mul(localBitangent, (float3x3)worldMatrix));
    return output;
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
    const float3 lightDirection = normalize(float3(0.4f, 0.8f, -0.3f));
    const float diffuseLight = saturate(dot(normal, lightDirection));
    const float aoSample = ambientOcclusionTexture.Sample(linearSampler, input.texCoord).r;
    const float occlusion = lerp(1.0f, aoSample, saturate(resolvedOcclusionStrength));
    const float3 diffuse = albedo.rgb * (1.0f - metallicValue) * (0.15f + 0.85f * diffuseLight);
    const float3 specularColor = lerp(0.04f.xxx, albedo.rgb, metallicValue);
    const float3 specular = specularColor * diffuseLight * (1.0f - roughnessValue * 0.75f);
    const float3 emissive = emissiveTexture.Sample(linearSampler, input.texCoord).rgb
        * resolvedEmissiveColor * resolvedEmissiveIntensity;
    return float4((diffuse + specular) * occlusion + emissive, albedo.a);
}

float4 psAlphaTest(PixelInput input) : SV_TARGET
{
    const float4 color = input.color * baseColorTexture.Sample(linearSampler, input.texCoord);
    const float resolvedAlphaCutoff = (propertyOverrideMask & PROPERTY_ALPHA_CUTOFF) != 0
        ? propertyAlphaCutoff : alphaCutoff;
    clip(color.a - resolvedAlphaCutoff);
    return color;
}

void psDepthAlphaTest(PixelInput input)
{
    const float alpha = input.color.a * baseColorTexture.Sample(linearSampler, input.texCoord).a;
    const float resolvedAlphaCutoff = (propertyOverrideMask & PROPERTY_ALPHA_CUTOFF) != 0
        ? propertyAlphaCutoff : alphaCutoff;
    clip(alpha - resolvedAlphaCutoff);
}