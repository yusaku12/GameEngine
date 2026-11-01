struct ObjectConstants
{
    row_major float4x4 worldViewProjection;
    row_major float4x4 worldMatrix;
};
StructuredBuffer<ObjectConstants> instances : register(t5);

float4 vsMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID) : SV_POSITION
{
    static const float3 positions[3] = {
        float3(-0.1f, -0.1f, 0), float3(0, 0.1f, 0), float3(0.1f, -0.1f, 0)
    };
    return mul(float4(positions[vertex], 1), instances[instance].worldViewProjection);
}

float4 psMain() : SV_TARGET
{
    return float4(1, 1, 1, 1);
}
