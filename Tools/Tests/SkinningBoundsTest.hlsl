#include "Model.hlsl"

StructuredBuffer<VertexInput> testVertices : register(t7);
RWStructuredBuffer<float4> testPositions : register(u0);

[numthreads(64, 1, 1)]
void csMain(uint3 thread : SV_DispatchThreadID)
{
    uint count, stride;
    testVertices.GetDimensions(count, stride);
    if (thread.x < count)
        testPositions[thread.x] = float4(vsMain(testVertices[thread.x], 0).worldPosition, 1);
}
