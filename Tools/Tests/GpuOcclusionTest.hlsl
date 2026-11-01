struct ObjectConstants
{
    row_major float4x4 worldViewProjection;
    row_major float4x4 worldMatrix;
};
StructuredBuffer<ObjectConstants> instances : register(t5);
RWByteAddressBuffer recordedInstances : register(u0);

struct Pixel
{
    float4 position : SV_POSITION;
    nointerpolation float color : COLOR;
};

Pixel vsMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    static const float3 positions[3] = {
        float3(-0.1f, -0.1f, 0), float3(0, 0.1f, 0), float3(0.1f, -0.1f, 0)
    };
    Pixel result;
    const ObjectConstants object = instances[instance];
    result.position = mul(float4(positions[vertex], 1), object.worldViewProjection);
    result.color = saturate(0.5f + object.worldMatrix[3][0] * 20.0f);
    if (vertex == 0)
        for (uint row = 0; row < 4; ++row)
        {
            recordedInstances.Store4(instance * 128 + row * 16, asuint(object.worldViewProjection[row]));
            recordedInstances.Store4(instance * 128 + 64 + row * 16, asuint(object.worldMatrix[row]));
        }
    return result;
}

float4 psMain(Pixel input) : SV_TARGET
{
    return float4(input.color, 1, 1, 1);
}
