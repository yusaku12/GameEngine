struct Sample
{
    float height;
    float y;
};

struct Layer
{
    float minimumHeight;
    float maximumHeight;
    float heightBlend;
    float minimumSlope;
    float maximumSlope;
    float slopeBlend;
    float strength;
};

cbuffer Recipe : register(b0)
{
    uint width;
    uint height;
    uint materialEnabled;
    uint layerCount;
};

StructuredBuffer<Sample> samples : register(t0);
StructuredBuffer<float2> axes : register(t1);
StructuredBuffer<Layer> layers : register(t2);
RWByteAddressBuffer output : register(u0);

float3 position(uint x, uint z)
{
    return float3(axes[x].x, samples[z * width + x].y, axes[width + z].x);
}

float3 firstNormal(uint x, uint z)
{
    float3 a = position(x, z);
    return cross(position(x, z + 1) - a, position(x + 1, z) - a);
}

float3 secondNormal(uint x, uint z)
{
    float3 b = position(x + 1, z);
    return cross(position(x, z + 1) - b, position(x + 1, z + 1) - b);
}

float band(float value, float minimum, float maximum, float blend)
{
    if (value >= minimum && value <= maximum)
        return 1.0f;
    if (blend == 0.0f)
        return 0.0f;
    float distance = value < minimum ? minimum - value : value - maximum;
    float t = saturate(1.0f - distance / blend);
    return t * t * (3.0f - 2.0f * t);
}

[numthreads(8, 8, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    uint x = id.x, z = id.y;
    if (x >= width || z >= height)
        return;

    float3 sum = 0.0f;
    if (x > 0 && z > 0)
        sum += secondNormal(x - 1, z - 1);
    if (x + 1 < width && z > 0)
    {
        sum += firstNormal(x, z - 1);
        sum += secondNormal(x, z - 1);
    }
    if (x > 0 && z + 1 < height)
    {
        sum += firstNormal(x - 1, z);
        sum += secondNormal(x - 1, z);
    }
    if (x + 1 < width && z + 1 < height)
        sum += firstNormal(x, z);
    float3 normal = normalize(sum);
    float3 tangent = normalize(float3(normal.y, -normal.x, 0.0f));
    float3 bitangent = normalize(cross(tangent, normal));

    float4 weights = 1.0f;
    if (materialEnabled != 0)
    {
        weights = float4(1, 0, 0, 0);
        float slope = acos(clamp(normal.y, 0.0f, 1.0f)) * (180.0f / 3.14159265358979323846f);
        for (uint layerIndex = 1; layerIndex < layerCount; ++layerIndex)
        {
            Layer layer = layers[layerIndex];
            float coverage = layer.strength
                * band(samples[z * width + x].height, layer.minimumHeight, layer.maximumHeight, layer.heightBlend)
                * band(slope, layer.minimumSlope, layer.maximumSlope, layer.slopeBlend);
            for (uint previous = 0; previous < layerIndex; ++previous)
                weights[previous] *= 1.0f - coverage;
            weights[layerIndex] = coverage;
        }
    }

    uint vertexOffset = (z * width + x) * 96;
    output.Store3(vertexOffset, asuint(position(x, z)));
    output.Store3(vertexOffset + 12, asuint(normal));
    output.Store3(vertexOffset + 24, asuint(tangent));
    output.Store3(vertexOffset + 36, asuint(bitangent));
    output.Store2(vertexOffset + 48, asuint(float2(axes[x].y, axes[width + z].y)));
    output.Store4(vertexOffset + 56, asuint(weights));
    output.Store2(vertexOffset + 72, uint2(0, 0));
    output.Store4(vertexOffset + 80, uint4(0, 0, 0, 0));

    if (x + 1 < width && z + 1 < height)
    {
        uint a = z * width + x, b = a + 1, c = a + width, d = c + 1;
        uint offset = width * height * 96 + (z * (width - 1) + x) * 24;
        output.Store3(offset, uint3(a, c, b));
        output.Store3(offset + 12, uint3(b, c, d));
    }
}
