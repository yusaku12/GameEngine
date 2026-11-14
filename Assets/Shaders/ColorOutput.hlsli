#ifndef ENGINE_COLOR_OUTPUT_HLSLI
#define ENGINE_COLOR_OUTPUT_HLSLI

float3 encodeSrgb(float3 value)
{
    value = saturate(value);
    const float3 linearValue = value * 12.92f;
    const float3 srgbValue = 1.055f * pow(value, 1.0f / 2.4f) - 0.055f;
    return lerp(linearValue, srgbValue, step(0.0031308f, value));
}

float3 decodeSrgb(float3 value)
{
    value = saturate(value);
    const float3 linearValue = value / 12.92f;
    const float3 srgbValue = pow((value + 0.055f) / 1.055f, 2.4f);
    return lerp(linearValue, srgbValue, step(0.04045f, value));
}

#endif
