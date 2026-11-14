#ifndef ENGINE_POST_PROCESS_HLSLI
#define ENGINE_POST_PROCESS_HLSLI

cbuffer PostProcessFrame : register(b0)
{
    float4 sourceViewport;
    float4 targetViewport;
    uint exposureMode;
};

float getPostProcessExposure()
{
    return asfloat(exposureMode & 0x7fffffffu);
}

bool shouldApplyToneMapping()
{
    return (exposureMode & 0x80000000u) != 0u;
}

cbuffer PostProcessParameters : register(b1)
{
#ifndef ENGINE_POST_PROCESS_PARAMETER_COUNT
#define ENGINE_POST_PROCESS_PARAMETER_COUNT 12
#endif
    float4 parameters[ENGINE_POST_PROCESS_PARAMETER_COUNT];
};

Texture2D<float4> inputColor : register(t0);
Texture2D<float4> secondaryColor : register(t1);
Texture2D<float4> tertiaryColor : register(t2);
cbuffer PostProcessAuxiliary : register(b2)
{
    float4 secondaryViewport;
};
SamplerState linearClamp : register(s0);

struct PostProcessVertex
{
    float4 position : SV_Position;
};

float4 sampleViewport(Texture2D<float4> image, float4 viewport, float2 pixelPosition, float2 sourceOffset)
{
    const float2 uv = (pixelPosition - targetViewport.xy) / targetViewport.zw;
    uint width, height;
    image.GetDimensions(width, height);
    const float2 firstPixel = clamp(ceil(viewport.xy - 0.5f) + 0.5f,
        0.5f, float2(width, height) - 0.5f);
    const float2 lastPixel = min(float2(width, height) - 0.5f,
        // The implicit viewport scissor floors its right/bottom extents.
        max(firstPixel, floor(viewport.xy + viewport.zw) - 0.5f));
    const float2 mappedPosition = all(viewport == targetViewport)
        ? pixelPosition + sourceOffset : viewport.xy + uv * viewport.zw + sourceOffset;
    const float2 samplePosition = clamp(mappedPosition, firstPixel, lastPixel);
    return image.SampleLevel(linearClamp, samplePosition / float2(width, height), 0);
}

float4 loadInputColor(float2 pixelPosition)
{
    if (all(sourceViewport == targetViewport))
        return inputColor.Load(int3(int2(pixelPosition), 0));
    return sampleViewport(inputColor, sourceViewport, pixelPosition, 0);
}

float3 sampleInputOffset(float2 pixelPosition, float2 offset)
{
    return sampleViewport(inputColor, sourceViewport, pixelPosition, offset).rgb;
}

#endif
