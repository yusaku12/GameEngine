// Compiled by DXC with Shader Model 6.x and HLSL 2021.
cbuffer ObjectConstants : register(b0)
{
    row_major float4x4 worldViewProjection;
    float4 baseColor;
};

Texture2D<float4> baseColorTexture : register(t0);
SamplerState linearSampler : register(s0);

struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
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
    output.position = mul(float4(input.position, 1.0f), worldViewProjection);
    output.color = input.color * baseColor;
    output.texCoord = input.texCoord;
    return output;
}

float4 psMain(PixelInput input) : SV_TARGET
{
    return input.color * baseColorTexture.Sample(linearSampler, input.texCoord);
}