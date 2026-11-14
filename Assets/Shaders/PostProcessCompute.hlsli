#ifndef ENGINE_POST_PROCESS_COMPUTE_HLSLI
#define ENGINE_POST_PROCESS_COMPUTE_HLSLI

#include "PostProcess.hlsli"

cbuffer PostProcessDispatch : register(b3)
{
    uint2 dispatchOrigin;
    uint2 dispatchEnd;
};
RWTexture2D<float4> outputColor : register(u0);

bool postProcessPixel(uint2 thread, out uint2 pixel, out float2 position)
{
    pixel = dispatchOrigin + thread;
    position = float2(pixel) + 0.5f;
    // Match fullscreen rasterization's subpixel snapping and implicit viewport scissor.
    const float2 rasterOrigin = round(targetViewport.xy * 256.0f) / 256.0f;
    return all(pixel < dispatchEnd) && all(position >= rasterOrigin)
        && all(pixel < uint2(floor(targetViewport.xy + targetViewport.zw)));
}

#endif
