// Compiled by DXC with Shader Model 6.x and HLSL 2021.
cbuffer ObjectConstants : register(b0)
{
    row_major float4x4 worldViewProjection;
    float4 baseColor;
};

static const uint MAX_SKINNING_BONES = 256;

cbuffer SkinningConstants : register(b1)
{
    row_major float4x4 boneMatrices[MAX_SKINNING_BONES];
};

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
    output.color = input.color * baseColor;
    output.texCoord = input.texCoord;
    return output;
}

float4 psMain(PixelInput input) : SV_TARGET
{
    return input.color * baseColorTexture.Sample(linearSampler, input.texCoord);
}