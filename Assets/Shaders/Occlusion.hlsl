cbuffer CullConstants : register(b0)
{
    row_major float4x4 viewProjection;
    float4 viewport;
    uint2 depthSize;
    uint mipCount;
    uint batchOffset;
};

struct Candidate
{
    float3 center;
    uint canTest;
    float3 extents;
    uint padding;
};

struct Batch
{
    uint first;
    uint instanceCount;
    uint indexCount;
    uint indexStart;
    int baseVertex;
};

Texture2D<float> hierarchy : register(t0);
StructuredBuffer<Candidate> candidates : register(t1);
ByteAddressBuffer instances : register(t2);
StructuredBuffer<Batch> batches : register(t3);
RWByteAddressBuffer visibleInstances : register(u0);
RWByteAddressBuffer commands : register(u1);
static const uint INSTANCE_STRIDE = 192;

bool isVisible(Candidate candidate)
{
    if (candidate.canTest == 0 || !all(isfinite(candidate.center)) || !all(isfinite(candidate.extents))
        || any(candidate.extents < 0.0f) || all(candidate.extents == 0.0f))
        return true;

    float2 minimum = float2(1.0f, 1.0f);
    float2 maximum = float2(-1.0f, -1.0f);
    float nearestDepth = 1.0f;
    for (uint corner = 0; corner < 8; ++corner)
    {
        const float3 direction = float3((corner & 1) != 0 ? 1.0f : -1.0f,
            (corner & 2) != 0 ? 1.0f : -1.0f, (corner & 4) != 0 ? 1.0f : -1.0f);
        const float4 clip = mul(float4(candidate.center + direction * candidate.extents, 1.0f), viewProjection);
        // Near-plane intersections, camera-inside bounds and nonfinite transforms are never culled.
        if (!all(isfinite(clip)) || clip.w <= 0.00001f || clip.z <= 0.00001f)
            return true;
        const float3 ndc = clip.xyz / clip.w;
        if (!all(isfinite(ndc)))
            return true;
        minimum = min(minimum, ndc.xy);
        maximum = max(maximum, ndc.xy);
        nearestDepth = min(nearestDepth, ndc.z);
    }

    const float2 pixelMinimum = viewport.xy
        + float2(minimum.x * 0.5f + 0.5f, 0.5f - maximum.y * 0.5f) * viewport.zw - 2.0f;
    const float2 pixelMaximum = viewport.xy
        + float2(maximum.x * 0.5f + 0.5f, 0.5f - minimum.y * 0.5f) * viewport.zw + 2.0f;
    const float2 clippedMinimum = clamp(floor(pixelMinimum), 0.0f, float2(depthSize));
    const float2 clippedMaximum = clamp(ceil(pixelMaximum), 0.0f, float2(depthSize));
    if (any(clippedMinimum >= clippedMaximum))
        return true;

    const uint2 firstPixel = uint2(clippedMinimum);
    const uint2 lastPixel = uint2(clippedMaximum) - 1;
    const uint extent = max(lastPixel.x - firstPixel.x + 1, lastPixel.y - firstPixel.y + 1);
    const uint mip = min(extent <= 1 ? 0u : uint(firstbithigh(extent - 1) + 1), mipCount - 1);
    const uint2 first = firstPixel >> mip;
    const uint2 last = lastPixel >> mip;
    // This mip covers the rectangle with at most 2x2 cells; all overlapping cells are included.
    const float farthestDepth = max(max(hierarchy.Load(int3(first, mip)),
        hierarchy.Load(int3(last.x, first.y, mip))),
        max(hierarchy.Load(int3(first.x, last.y, mip)), hierarchy.Load(int3(last, mip))));
    return !isfinite(farthestDepth) || max(0.0f, nearestDepth - 0.00001f) <= farthestDepth;
}

groupshared uint visibleCount;
groupshared uint prefix[64];

[numthreads(64, 1, 1)]
void csMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    const uint batchIndex = batchOffset + group.x;
    const Batch batch = batches[batchIndex];
    if (lane == 0)
        visibleCount = 0;
    GroupMemoryBarrierWithGroupSync();

    for (uint first = 0; first < batch.instanceCount; first += 64)
    {
        const uint instance = first + lane;
        const uint sourceIndex = batch.first + instance;
        bool visible = false;
        if (instance < batch.instanceCount)
            visible = isVisible(candidates[sourceIndex]);
        prefix[lane] = visible ? 1 : 0;
        GroupMemoryBarrierWithGroupSync();
        // Stable inclusive scan, independent of wave size and lane scheduling.
        for (uint offset = 1; offset < 64; offset *= 2)
        {
            const uint addend = lane >= offset ? prefix[lane - offset] : 0;
            GroupMemoryBarrierWithGroupSync();
            prefix[lane] += addend;
            GroupMemoryBarrierWithGroupSync();
        }
        if (visible)
        {
            const uint destination = visibleCount + prefix[lane] - 1;
            const uint sourceAddress = sourceIndex * INSTANCE_STRIDE;
            const uint destinationAddress = (batch.first + destination) * INSTANCE_STRIDE;
            for (uint offset = 0; offset < INSTANCE_STRIDE; offset += 16)
                visibleInstances.Store4(destinationAddress + offset, instances.Load4(sourceAddress + offset));
        }
        GroupMemoryBarrierWithGroupSync();
        if (lane == 0)
            visibleCount += prefix[63];
        GroupMemoryBarrierWithGroupSync();
    }
    GroupMemoryBarrierWithGroupSync();

    if (lane == 0)
    {
        const uint address = batchIndex * 24;
        commands.Store4(address, uint4(batch.indexCount, visibleCount, batch.indexStart, asuint(batch.baseVertex)));
        commands.Store2(address + 16, uint2(0, visibleCount != 0 ? 1 : 0));
    }
}
