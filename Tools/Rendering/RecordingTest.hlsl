struct Instance
{
    row_major float4x4 worldViewProjection;
    row_major float4x4 world;
};
StructuredBuffer<Instance> instances : register(t5);
cbuffer Properties : register(b3)
{
    float4 color;
};
float4 vsMain(float3 position : POSITION, uint instance : SV_InstanceID) : SV_POSITION
{
    return mul(float4(position, 1.0f), instances[instance].worldViewProjection);
}
float4 psMain() : SV_TARGET
{
    return color;
}
