// Compiled by DXC with Shader Model 6.x and HLSL 2021.
cbuffer ObjectConstants : register(b0)
{
    row_major float4x4 worldViewProjection;
};

static const uint MAX_SKINNING_BONES = 256;

cbuffer SkinningConstants : register(b1)
{
    row_major float4x4 boneMatrices[MAX_SKINNING_BONES];
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
static const uint PROPERTY_ALPHA_CUTOFF = 1u << 7;

Texture2D<float4> baseColorTexture : register(t0);
SamplerState linearSampler : register(s0);

struct VertexInput
{
    float3 position : POSITION;
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
};

PixelInput vsMain(VertexInput input)
{
    PixelInput output;
    float4 localPosition = float4(input.position, 1.0f);
    const float totalWeight = dot(input.boneWeights, 1.0f);
    if (totalWeight > 0.00001f)
    {
        const float4 normalizedWeights = input.boneWeights / totalWeight;
        localPosition = mul(localPosition, boneMatrices[min(input.boneIndices.x, MAX_SKINNING_BONES - 1)]) * normalizedWeights.x
            + mul(localPosition, boneMatrices[min(input.boneIndices.y, MAX_SKINNING_BONES - 1)]) * normalizedWeights.y
            + mul(localPosition, boneMatrices[min(input.boneIndices.z, MAX_SKINNING_BONES - 1)]) * normalizedWeights.z
            + mul(localPosition, boneMatrices[min(input.boneIndices.w, MAX_SKINNING_BONES - 1)]) * normalizedWeights.w;
    }
    output.position = mul(localPosition, worldViewProjection);
    const float4 resolvedBaseColor = (propertyOverrideMask & PROPERTY_BASE_COLOR) != 0
        ? propertyBaseColor : baseColor;
    output.color = input.color * resolvedBaseColor;
    output.texCoord = input.texCoord;
    return output;
}

float4 psMain(PixelInput input) : SV_TARGET
{
    return input.color * baseColorTexture.Sample(linearSampler, input.texCoord);
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