cbuffer DebugConstants : register(b0)
{
    row_major float4x4 viewProjection;
};

struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PixelInput vsMain(VertexInput input)
{
    PixelInput output;
    output.position = mul(float4(input.position, 1.0f), viewProjection);
    output.color = input.color;
    return output;
}

float4 psMain(PixelInput input) : SV_TARGET
{
    return input.color;
}