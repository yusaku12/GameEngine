cbuffer ReductionConstants : register(b0)
{
    uint2 destinationSize;
    uint2 sourceSize;
    uint copyDepth;
};

Texture2D<float> sourceDepth : register(t0);
RWTexture2D<float> destinationDepth : register(u0);

[numthreads(8, 8, 1)]
void csMain(uint3 thread : SV_DispatchThreadID)
{
    if (any(thread.xy >= destinationSize))
        return;

    float depth = 1.0f;
    if (copyDepth != 0)
    {
        if (all(thread.xy < sourceSize))
            depth = sourceDepth.Load(int3(thread.xy, 0));
    }
    else
    {
        const uint2 first = thread.xy * 2;
        const uint2 last = min(first + 1, sourceSize - 1);
        depth = max(max(sourceDepth.Load(int3(first, 0)), sourceDepth.Load(int3(last.x, first.y, 0))),
            max(sourceDepth.Load(int3(first.x, last.y, 0)), sourceDepth.Load(int3(last, 0))));
    }
    // Standard Z: uncovered pixels and the farthest depth must propagate through every mip.
    destinationDepth[thread.xy] = isfinite(depth) ? saturate(depth) : 1.0f;
}
