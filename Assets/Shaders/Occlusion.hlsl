cbuffer QueryConstants : register(b0)
{
    float4 rectangle;
    float nearestDepth;
};

float4 vsMain(uint vertexID : SV_VertexID) : SV_POSITION
{
    static const float2 corners[6] = {
        float2(0, 0), float2(0, 1), float2(1, 0),
        float2(1, 0), float2(0, 1), float2(1, 1)
    };
    return float4(lerp(rectangle.xy, rectangle.zw, corners[vertexID]), nearestDepth, 1.0f);
}
