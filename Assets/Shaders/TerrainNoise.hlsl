struct Coordinate
{
    uint2 cell;
    float fraction;
    uint padding;
};

cbuffer Recipe : register(b0)
{
    uint width;
    uint height;
    uint octaves;
    uint seed;
    float persistence;
};
StructuredBuffer<Coordinate> coordinates : register(t0);
RWStructuredBuffer<float> heights : register(u0);

uint2 add64(uint2 a, uint2 b)
{
    uint low = a.x + b.x;
    return uint2(low, a.y + b.y + (low < a.x ? 1u : 0u));
}

uint2 shift64(uint2 value, uint bits)
{
    return uint2((value.x >> bits) | (value.y << (32u - bits)), value.y >> bits);
}

uint2 multiply64(uint2 a, uint2 b)
{
    uint a0 = a.x & 0xffffu, a1 = a.x >> 16;
    uint b0 = b.x & 0xffffu, b1 = b.x >> 16;
    uint t = a1 * b0 + ((a0 * b0) >> 16);
    uint middle = (t & 0xffffu) + a0 * b1;
    uint low = a.x * b.x;
    uint high = a1 * b1 + (t >> 16) + (middle >> 16);
    return uint2(low, high + a.x * b.y + a.y * b.x);
}

// Exact low-64-bit arithmetic matches the CPU hash without optional Int64ShaderOps.
uint2 mix64(uint2 value)
{
    value = multiply64(value ^ shift64(value, 30), uint2(0x1ce4e5b9u, 0xbf58476du));
    value = multiply64(value ^ shift64(value, 27), uint2(0x133111ebu, 0x94d049bbu));
    return value ^ shift64(value, 31);
}

float gradient(uint2 hash, float x, float z)
{
    switch (mix64(hash).x & 7u)
    {
    case 0: return x;
    case 1: return -x;
    case 2: return z;
    case 3: return -z;
    case 4: return (x + z) * 0.7071067811865475f;
    case 5: return (x - z) * 0.7071067811865475f;
    case 6: return (-x + z) * 0.7071067811865475f;
    default: return (-x - z) * 0.7071067811865475f;
    }
}

float fade(float t)
{
    return t * t * t * (t * (t * 6 - 15) + 10);
}

[numthreads(8, 8, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    float total = 0, weight = 0, amplitude = 1;
    const uint2 seedHash = mix64(uint2(seed, 0));
    for (uint octave = 0; octave < octaves && amplitude > 0; ++octave)
    {
        Coordinate x = coordinates[octave * width + id.x];
        Coordinate z = coordinates[octaves * width + octave * height + id.y];
        uint2 x0 = mix64(add64(x.cell, uint2(0x7f4a7c15u, 0x9e3779b9u)));
        uint2 x1 = mix64(add64(add64(x.cell, uint2(1, 0)), uint2(0x7f4a7c15u, 0x9e3779b9u)));
        uint2 z0 = mix64(add64(z.cell, uint2(0xd9b4e019u, 0x632be59bu)));
        uint2 z1 = mix64(add64(add64(z.cell, uint2(1, 0)), uint2(0xd9b4e019u, 0x632be59bu)));
        float u = fade(x.fraction), v = fade(z.fraction);
        float top = lerp(gradient(x0 ^ z0 ^ seedHash, x.fraction, z.fraction),
            gradient(x1 ^ z0 ^ seedHash, x.fraction - 1, z.fraction), u);
        float bottom = lerp(gradient(x0 ^ z1 ^ seedHash, x.fraction, z.fraction - 1),
            gradient(x1 ^ z1 ^ seedHash, x.fraction - 1, z.fraction - 1), u);
        total += amplitude * lerp(top, bottom, v);
        weight += amplitude;
        amplitude *= persistence;
    }
    heights[id.y * width + id.x] = saturate(0.5f + 0.3535533905932738f * total / weight);
}
