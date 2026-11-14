#ifndef ENGINE_POST_PROCESS_NOISE_HLSLI
#define ENGINE_POST_PROCESS_NOISE_HLSLI

float grainNoise(uint2 pixel, uint frame)
{
    uint value = pixel.x * 1973u + pixel.y * 9277u + frame * 26699u + 0x68bc21ebu;
    value = (value << 13u) ^ value;
    value = value * (value * value * 15731u + 789221u) + 1376312589u;
    return float(value & 0x7fffffffu) / 1073741824.0f - 1.0f;
}

#endif
