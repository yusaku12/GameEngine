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

struct ShapeVertexInput
{
    float3 position : POSITION;
    float heightOffset : HEIGHTOFFSET;
    float4 world0 : INSTANCEWORLD0;
    float4 world1 : INSTANCEWORLD1;
    float4 world2 : INSTANCEWORLD2;
    float4 world3 : INSTANCEWORLD3;
    float4 color : COLOR;
    float4 dimensions : DIMENSIONS;
};

PixelInput vsMain(VertexInput input)
{
    PixelInput output;
    output.position = mul(float4(input.position, 1.0f), viewProjection);
    output.color = input.color;
    return output;
}

PixelInput vsShape(ShapeVertexInput input)
{
    const row_major float4x4 world = float4x4(input.world0, input.world1, input.world2, input.world3);
    float3 localPosition = input.position * input.dimensions.xyz;
    localPosition.y += input.heightOffset * input.dimensions.w;
    const float4 worldPosition = mul(float4(localPosition, 1.0f), world);
    PixelInput output;
    output.position = mul(float4(worldPosition.xyz / worldPosition.w, 1.0f), viewProjection);
    output.color = input.color;
    return output;
}

float4 psMain(PixelInput input) : SV_TARGET
{
    return input.color;
}