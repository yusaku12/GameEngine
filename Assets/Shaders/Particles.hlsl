cbuffer ParticleConstants : register(b0)
{
    row_major float4x4 viewProjection;
};

cbuffer ParticleAppearance : register(b1)
{
    uint appearance;
};

cbuffer ParticleDepthConstants : register(b2)
{
    row_major float4x4 inverseViewProjection;
    float4 depthViewport;
    float4 depthRange;
    float4 cameraForward;
};

cbuffer ParticleSoftConstants : register(b3)
{
    float softDistance;
};

cbuffer ParticleDistortionConstants : register(b4)
{
    float uvDistortion;
};

cbuffer ParticleRefractionConstants : register(b5)
{
    float refractionPixels;
};

Texture2D particleTexture : register(t0);
Texture2D<float> sceneDepth : register(t1);
Texture2D secondaryTexture : register(t2);
Texture2D maskTexture : register(t3);
Texture2D distortionTexture : register(t4);
Texture2D sceneColor : register(t5);
SamplerState particleSampler : register(s0);

struct GpuParticleState
{
    float4 positionAge;
    float4 velocityLifetime;
    float4 startColor;
    float4 endColor;
    float4 sizeRotation;
    uint4 randomData;
};

RWStructuredBuffer<GpuParticleState> simulationParticles : register(u0);
RWByteAddressBuffer simulationTrails : register(u4);

cbuffer GpuTrailSettings : register(b13)
{
    row_major float4x4 trailWorld;
    float4 trailSettings; // lifetime, width, minimum distance, blend mode
};

static const uint trailPointCapacity = 16;
static const uint trailPointBytes = 32;
static const uint trailHeaderBytes = 32;
static const uint trailStride = trailHeaderBytes + (trailPointCapacity + 1) * trailPointBytes;
static const uint trailReset = 1u;
static const uint trailAgePass = 2u;

struct GpuTrailPoint { float4 positionAge; float4 color; };
struct GpuTrailHeader
{
    uint first, count, attached, blend;
    float lifetime, width, minDistance;
    uint padding;
};

cbuffer GpuSimulation : register(b0)
{
    uint4 simulationHeader;
    float4 simulationTiming;
    float4 simulationSpeedShape;
    uint simulationShape;
    float3 simulationBoxExtents;
    float4 simulationDirection;
    float4 simulationGravityDrag;
    float4 simulationSizeRange;
    float4 simulationStartColor;
    float4 simulationEndColor;
    float4 simulationInitialColorMin;
    float4 simulationInitialColorMax;
    float4 simulationRotation;
    uint4 simulationFlags;
};

uint hashRandom(inout uint state)
{
    state ^= state >> 16;
    state *= 0x7feb352d;
    state ^= state >> 15;
    state *= 0x846ca68b;
    state ^= state >> 16;
    return state;
}

float randomUnit(inout uint state)
{
    return (hashRandom(state) >> 8) * (1.0f / 16777216.0f);
}

float3 randomDirection(inout uint state)
{
    const float z = randomUnit(state) * 2.0f - 1.0f;
    const float angle = randomUnit(state) * 6.28318530718f;
    const float radial = sqrt(max(0.0f, 1.0f - z * z));
    return float3(radial * cos(angle), radial * sin(angle), z);
}

uint trailPointAddress(uint base, uint index)
{
    return base + trailHeaderBytes + index * trailPointBytes;
}

void appendTrailPoint(uint base, inout GpuTrailHeader header, GpuTrailPoint samplePoint)
{
    if (header.count == trailPointCapacity)
    {
        header.first = (header.first + 1) % trailPointCapacity;
        --header.count;
    }
    simulationTrails.Store<GpuTrailPoint>(trailPointAddress(base,
        (header.first + header.count++) % trailPointCapacity), samplePoint);
}

void advanceTrail(uint base, inout GpuTrailHeader header, float delta)
{
    if (delta <= 0.0f)
        return;
    for (uint i = 0; i < header.count; ++i)
    {
        const uint address = trailPointAddress(base, (header.first + i) % trailPointCapacity) + 12;
        simulationTrails.Store(address, asuint(asfloat(simulationTrails.Load(address)) + delta));
    }
    while (header.count != 0 && asfloat(simulationTrails.Load(
        trailPointAddress(base, header.first) + 12)) >= header.lifetime)
    {
        header.first = (header.first + 1) % trailPointCapacity;
        --header.count;
    }
}

void updateTrail(uint base, inout GpuTrailHeader header, GpuTrailPoint tip, bool detach)
{
    if (header.count == 0)
        appendTrailPoint(base, header, tip);
    else
    {
        const uint lastAddress = trailPointAddress(base, (header.first + header.count - 1) % trailPointCapacity);
        const GpuTrailPoint last = simulationTrails.Load<GpuTrailPoint>(lastAddress);
        const float3 movement = tip.positionAge.xyz - last.positionAge.xyz;
        const float distanceSquared = dot(movement, movement);
        if (distanceSquared > 1e-10f && (detach || distanceSquared >= header.minDistance * header.minDistance))
            appendTrailPoint(base, header, tip);
        else if (detach)
            simulationTrails.Store<GpuTrailPoint>(lastAddress, tip);
    }
    simulationTrails.Store<GpuTrailPoint>(trailPointAddress(base, trailPointCapacity), tip);
    header.attached = detach ? 0u : 1u;
}

uint allocateTrail(uint capacity)
{
    const uint list = capacity * trailStride;
    uint index;
    simulationTrails.InterlockedAdd(list, 1, index);
    return index < simulationTrails.Load(list + 4)
        ? simulationTrails.Load(list + 8 + index * 4) + 1 : 0;
}

groupshared uint trailPrefix[64];

void ageTrailsAndBuildFreeList(uint lane)
{
    const uint capacity = simulationHeader.x;
    const uint list = capacity * trailStride;
    const uint chunk = (capacity + 63) / 64;
    const uint begin = lane * chunk;
    const uint end = min(begin + chunk, capacity);
    const bool reset = simulationHeader.y != 0 || (simulationFlags.w & trailReset) != 0;
    uint2 freeMask = 0;
    for (uint index = begin; index < end; ++index)
    {
        const uint base = index * trailStride;
        GpuTrailHeader header = (GpuTrailHeader)0;
        if (!reset)
        {
            header = simulationTrails.Load<GpuTrailHeader>(base);
            advanceTrail(base, header, simulationTiming.y);
        }
        if (header.attached == 0 && header.count == 0)
        {
            const uint bit = index - begin;
            freeMask[bit / 32] |= 1u << (bit % 32);
        }
        simulationTrails.Store<GpuTrailHeader>(base, header);
    }

    const uint freeCount = countbits(freeMask.x) + countbits(freeMask.y);
    trailPrefix[lane] = freeCount;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint stride = 1; stride < 64; stride <<= 1)
    {
        const uint previous = lane >= stride ? trailPrefix[lane - stride] : 0u;
        GroupMemoryBarrierWithGroupSync();
        trailPrefix[lane] += previous;
        GroupMemoryBarrierWithGroupSync();
    }
    uint destination = trailPrefix[lane] - freeCount;
    for (uint word = 0; word < 2; ++word)
    {
        uint mask = freeMask[word];
        while (mask != 0)
        {
            simulationTrails.Store(list + 8 + destination++ * 4,
                begin + word * 32 + (uint)firstbitlow(mask));
            mask &= mask - 1;
        }
    }
    if (lane == 63)
        simulationTrails.Store2(list, uint2(0, trailPrefix[lane]));
}

GpuTrailPoint particleTrailPoint(GpuParticleState particle)
{
    const float age = saturate(particle.positionAge.w / particle.velocityLifetime.w);
    GpuTrailPoint tip;
    tip.positionAge = float4(mul(float4(particle.positionAge.xyz, 1.0f), trailWorld).xyz, 0.0f);
    tip.color = lerp(particle.startColor, particle.endColor, age);
    return tip;
}

[numthreads(64, 1, 1)]
void csSimulate(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if ((simulationFlags.w & trailAgePass) != 0)
    {
        ageTrailsAndBuildFreeList(index);
        return;
    }
    if (index >= simulationHeader.x)
        return;
    const bool hasTrails = simulationFlags.y != 0;
    GpuParticleState particle = simulationParticles[index];
    if (simulationHeader.y != 0)
    {
        particle.positionAge.w = -1.0f;
        particle.randomData.y = 0;
    }
    const bool hasAttachedTrail = hasTrails && particle.randomData.y != 0;
    const uint trailBase = hasAttachedTrail ? (particle.randomData.y - 1) * trailStride : 0;
    const uint spawnOffset = (index + simulationHeader.x - simulationHeader.z)
        % simulationHeader.x;
    if (spawnOffset < simulationHeader.w)
    {
        if (hasAttachedTrail)
        {
            GpuTrailHeader trail = simulationTrails.Load<GpuTrailHeader>(trailBase);
            GpuTrailPoint oldTip = simulationTrails.Load<GpuTrailPoint>(
                trailPointAddress(trailBase, trailPointCapacity));
            oldTip.positionAge.w += simulationTiming.y;
            if (oldTip.positionAge.w < trail.lifetime)
                updateTrail(trailBase, trail, oldTip, true);
            else
                trail.attached = 0;
            simulationTrails.Store<GpuTrailHeader>(trailBase, trail);
        }
        uint randomState = asuint(simulationTiming.x) ^ (index * 0x9e3779b9u);
        const float3 sphereDirection = randomDirection(randomState);
        float3 position = 0.0f;
        if (simulationShape == 0)
            position = sphereDirection * simulationSpeedShape.z * pow(randomUnit(randomState), 1.0f / 3.0f);
        else if (simulationShape == 1)
            position = (float3(randomUnit(randomState), randomUnit(randomState), randomUnit(randomState))
                * 2.0f - 1.0f) * simulationBoxExtents;
        else
        {
            const float angle = randomUnit(randomState) * 6.28318530718f;
            const float radius = simulationSpeedShape.z * sqrt(randomUnit(randomState));
            position = float3(cos(angle) * radius, 0.0f, sin(angle) * radius);
        }

        const float3 axis = normalize(simulationDirection.xyz);
        const float coneCosine = cos(simulationSpeedShape.w);
        const float coneZ = 1.0f - randomUnit(randomState) * (1.0f - coneCosine);
        const float coneAzimuth = randomUnit(randomState) * 6.28318530718f;
        const float coneRadial = sqrt(max(0.0f, 1.0f - coneZ * coneZ));
        const float3 helper = abs(axis.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
        const float3 tangent = normalize(cross(axis, helper));
        const float3 bitangent = cross(axis, tangent);
        const float3 velocityDirection = normalize(axis * coneZ
            + tangent * (coneRadial * cos(coneAzimuth))
            + bitangent * (coneRadial * sin(coneAzimuth)));

        particle.positionAge = float4(position, 0.0f);
        particle.velocityLifetime = float4(velocityDirection
            * lerp(simulationSpeedShape.x, simulationSpeedShape.y, randomUnit(randomState)),
            lerp(simulationTiming.z, simulationTiming.w, randomUnit(randomState)));
        particle.startColor = simulationStartColor;
        if (simulationFlags.x != 0)
        {
            particle.startColor = lerp(simulationInitialColorMin, simulationInitialColorMax,
                float4(randomUnit(randomState), randomUnit(randomState),
                    randomUnit(randomState), randomUnit(randomState)));
        }
        particle.endColor = simulationEndColor;
        const float randomSize = simulationFlags.x != 0
            ? lerp(simulationSizeRange.z, simulationSizeRange.w, randomUnit(randomState))
            : simulationSizeRange.x;
        particle.sizeRotation = float4(randomSize, simulationSizeRange.y,
            lerp(simulationRotation.x, simulationRotation.y, randomUnit(randomState)),
            lerp(simulationRotation.z, simulationRotation.w, randomUnit(randomState)));
        particle.randomData = uint4(hashRandom(randomState), 0, 0, 0);
        if (hasTrails && simulationFlags.z != 0)
        {
            particle.randomData.y = allocateTrail(simulationHeader.x);
            if (particle.randomData.y != 0)
            {
                const uint base = (particle.randomData.y - 1) * trailStride;
                GpuTrailHeader trail = (GpuTrailHeader)0;
                trail.blend = (uint)trailSettings.w;
                trail.lifetime = trailSettings.x;
                trail.width = trailSettings.y;
                trail.minDistance = trailSettings.z;
                updateTrail(base, trail, particleTrailPoint(particle), false);
                simulationTrails.Store<GpuTrailHeader>(base, trail);
            }
        }
        simulationParticles[index] = particle;
        return;
    }

    if (simulationHeader.y != 0)
    {
        simulationParticles[index] = particle;
        return;
    }

    if (particle.positionAge.w >= 0.0f)
    {
        const float timeUntilDeath = max(0.0f, particle.velocityLifetime.w - particle.positionAge.w);
        particle.positionAge.w += simulationTiming.y;
        const bool expired = particle.positionAge.w >= particle.velocityLifetime.w;
        const float step = expired ? min(timeUntilDeath, simulationTiming.y) : simulationTiming.y;
        if (!expired || hasAttachedTrail)
        {
            particle.velocityLifetime.xyz += simulationGravityDrag.xyz * step;
            particle.velocityLifetime.xyz *= exp(-simulationGravityDrag.w * step);
            particle.positionAge.xyz += particle.velocityLifetime.xyz * step;
            particle.sizeRotation.z += particle.sizeRotation.w * step;
        }
        if (hasAttachedTrail && simulationTiming.y > 0.0f)
        {
            GpuTrailHeader trail = simulationTrails.Load<GpuTrailHeader>(trailBase);
            GpuTrailPoint tip = particleTrailPoint(particle);
            tip.positionAge.w = simulationTiming.y - step;
            if (tip.positionAge.w < trail.lifetime && all(isfinite(tip.positionAge)))
                updateTrail(trailBase, trail, tip, expired);
            else if (expired)
                trail.attached = 0;
            simulationTrails.Store<GpuTrailHeader>(trailBase, trail);
        }
        if (expired)
        {
            particle.positionAge.w = -1.0f;
            particle.randomData.y = 0;
        }
        simulationParticles[index] = particle;
    }
}

cbuffer GpuParticleView : register(b6)
{
    row_major float4x4 gpuWorld;
};

cbuffer GpuParticleDraw : register(b7)
{
    uint4 gpuDrawHeader;
    float4 gpuCameraRightWorldScale;
    float4 gpuCameraUpScaleX;
    float4 gpuSizeScale;
};

StructuredBuffer<GpuParticleState> gpuParticles : register(t6);

cbuffer GpuParticleCompaction : register(b8)
{
    uint compactParticleLimit;
};

// D3D12_DRAW_ARGUMENTS followed by uint slot indices, used by ExecuteIndirect and vsGpu.
RWByteAddressBuffer compactOutput : register(u1);
ByteAddressBuffer compactDrawData : register(t8);
static const uint drawArgumentBytes = 16;
groupshared uint compactPrefix[128];

[numthreads(128, 1, 1)]
void csCompact(uint lane : SV_GroupIndex)
{
    // One group per emitter. Contiguous chunks preserve the original slot order.
    const uint chunkSize = (compactParticleLimit + 127u) / 128u;
    const uint begin = lane * chunkSize;
    const uint end = min(begin + chunkSize, compactParticleLimit);
    uint aliveMask = 0u;
    for (uint slot = begin; slot < end; ++slot)
    {
        const GpuParticleState particle = gpuParticles[slot];
        if (particle.positionAge.w >= 0.0f && particle.velocityLifetime.w > 0.0f)
            aliveMask |= 1u << (slot - begin);
    }

    const uint alive = countbits(aliveMask);
    compactPrefix[lane] = alive;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint stride = 1u; stride < 128u; stride <<= 1)
    {
        const uint previous = lane >= stride ? compactPrefix[lane - stride] : 0u;
        GroupMemoryBarrierWithGroupSync();
        compactPrefix[lane] += previous;
        GroupMemoryBarrierWithGroupSync();
    }

    uint destination = compactPrefix[lane] - alive;
    while (aliveMask != 0u)
    {
        const uint slot = begin + (uint)firstbitlow(aliveMask);
        compactOutput.Store(drawArgumentBytes + destination * 4u, slot);
        ++destination;
        aliveMask &= aliveMask - 1u;
    }
    if (lane == 127u)
        compactOutput.Store4(0u, uint4(6u, compactPrefix[lane], 0u, 0u));
}

struct QuadInstance
{
    float3 origin;
    uint isBand;
    float3 rightOrEnd;
    float frameBlend;
    float3 upOrStartSide;
    float3 endSide;
    float2 uvOffset;
    float4 color;
    float4 endColor;
    float4 uvBounds;
    float4 nextUvBounds;
};

StructuredBuffer<QuadInstance> quadInstances : register(t7);

struct PixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
    float4 uvBounds : TEXCOORD1;
    float2 uvOffset : TEXCOORD2;
    float4 nextUvBounds : TEXCOORD3;
    float frameBlend : TEXCOORD4;
};

PixelInput vsGpu(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 corners[6] = {
        float2(-0.5f, -0.5f), float2(-0.5f, 0.5f), float2(0.5f, 0.5f),
        float2(-0.5f, -0.5f), float2(0.5f, 0.5f), float2(0.5f, -0.5f)
    };
    PixelInput output = (PixelInput)0;
    const uint slot = compactDrawData.Load(drawArgumentBytes + instanceId * 4u);
    const GpuParticleState particle = gpuParticles[slot];
    const float life = particle.velocityLifetime.w;
    const float normalizedAge = saturate(particle.positionAge.w / life);
    const float2 corner = corners[vertexId % 6];
    const float cosine = cos(particle.sizeRotation.z);
    const float sine = sin(particle.sizeRotation.z);
    const float2 rotated = float2(corner.x * cosine - corner.y * sine,
        corner.x * sine + corner.y * cosine);
    float3 center = particle.positionAge.xyz;
    if (gpuDrawHeader.y != 0)
        center = mul(float4(center, 1.0f), gpuWorld).xyz;
    const float size = lerp(particle.sizeRotation.x, particle.sizeRotation.y, normalizedAge)
        * (gpuDrawHeader.y != 0 ? gpuCameraRightWorldScale.w : 1.0f);
    const float3 position = center + gpuCameraRightWorldScale.xyz * rotated.x * size * gpuCameraUpScaleX.w
        + gpuCameraUpScaleX.xyz * rotated.y * size * gpuSizeScale.y;
    output.position = mul(float4(position, 1.0f), viewProjection);
    output.color = lerp(particle.startColor, particle.endColor, normalizedAge);
    const uint frameCount = gpuDrawHeader.z * gpuDrawHeader.w;
    const uint frame = min((uint)(normalizedAge * frameCount), frameCount - 1);
    const float2 cell = float2(1.0f / gpuDrawHeader.z, 1.0f / gpuDrawHeader.w);
    const float2 cellOrigin = float2(frame % gpuDrawHeader.z, frame / gpuDrawHeader.z) * cell;
    output.uv = corner + 0.5f;
    output.uvBounds = float4(cellOrigin, cellOrigin + cell);
    output.uvOffset = 0.0f;
    output.nextUvBounds = output.uvBounds;
    output.frameBlend = 0.0f;
    return output;
}

struct MeshVertexInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float3 instancePosition : INSTANCE_POSITION;
    float3 instanceRight : INSTANCE_RIGHT;
    float3 instanceUp : INSTANCE_UP;
    float3 instanceNormal : INSTANCE_NORMAL;
    float4 color : COLOR;
    float4 uvBounds : TEXCOORD1;
    float2 uvOffset : TEXCOORD2;
    float4 nextUvBounds : TEXCOORD3;
    float frameBlend : TEXCOORD4;
};

float4 psGpu(PixelInput input) : SV_TARGET
{
    const uint kind = appearance & 255;
    const uint blendMode = (appearance >> 8) & 255;
    float4 color = input.color;
    if (kind == 1)
        color *= particleTexture.Sample(particleSampler, input.uvBounds.xy + input.uv * (input.uvBounds.zw - input.uvBounds.xy));
    else
    {
        const float2 centeredUv = input.uv * 2.0f - 1.0f;
        color.a *= 1.0f - smoothstep(0.35f, 1.0f, length(centeredUv));
    }
    if (blendMode == 2 || blendMode == 4)
        color.rgb *= color.a;
    else if (blendMode == 3)
        color.rgb = lerp(1.0f, color.rgb, color.a);
    return color;
}

PixelInput expandQuad(uint vertexId, QuadInstance instance)
{
    static const uint cornerIndices[6] = { 0, 1, 2, 2, 1, 3 };
    const uint corner = cornerIndices[vertexId];
    const bool positiveSide = (corner & 1u) != 0;
    const bool upperOrEnd = corner >= 2u;
    const float sideSign = positiveSide ? 1.0f : -1.0f;
    PixelInput output;
    float3 position;
    if (instance.isBand != 0)
    {
        const float3 center = upperOrEnd ? instance.rightOrEnd : instance.origin;
        const float3 side = upperOrEnd ? instance.endSide : instance.upOrStartSide;
        position = center + side * sideSign;
        output.color = upperOrEnd ? instance.endColor : instance.color;
        output.uv = float2(positiveSide ? 1.0f : 0.0f, upperOrEnd ? 1.0f : 0.0f);
    }
    else
    {
        position = instance.origin + instance.rightOrEnd * sideSign
            + instance.upOrStartSide * (upperOrEnd ? 1.0f : -1.0f);
        output.color = instance.color;
        output.uv = float2(positiveSide ? instance.uvBounds.z : instance.uvBounds.x,
            upperOrEnd ? instance.uvBounds.y : instance.uvBounds.w);
    }
    output.position = all(isfinite(position))
        ? mul(float4(position, 1.0f), viewProjection) : float4(2.0f, 2.0f, 2.0f, 1.0f);
    output.uvBounds = instance.uvBounds;
    output.uvOffset = instance.uvOffset;
    output.nextUvBounds = instance.nextUvBounds;
    output.frameBlend = instance.frameBlend;
    return output;
}

PixelInput vsMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    return expandQuad(vertexId, quadInstances[instanceId]);
}

PixelInput vsMesh(MeshVertexInput input)
{
    PixelInput output;
    const float3 position = input.instancePosition
        + input.instanceRight * input.position.x
        + input.instanceUp * input.position.y
        + input.instanceNormal * input.position.z;
    output.position = mul(float4(position, 1.0f), viewProjection);
    output.color = input.color;
    output.uv = lerp(input.uvBounds.xy, input.uvBounds.zw, saturate(input.uv));
    output.uvBounds = input.uvBounds;
    output.uvOffset = input.uvOffset;
    output.nextUvBounds = input.nextUvBounds;
    output.frameBlend = input.frameBlend;
    return output;
}

float4 shadeParticle(PixelInput input, uint packedAppearance, float softness, float distortionStrength,
    float refractionStrength, Texture2D primary, Texture2D secondary, Texture2D mask, Texture2D distortionMap)
{
    const uint kind = packedAppearance & 255;
    const uint textureFlags = packedAppearance >> 16;
    const float2 extent = input.uvBounds.zw - input.uvBounds.xy;
    float2 localUv = float2(
        extent.x > 0.0f ? (input.uv.x - input.uvBounds.x) / extent.x : 0.5f,
        extent.y > 0.0f ? (input.uv.y - input.uvBounds.y) / extent.y : 0.5f);
    float2 offset = input.uvOffset;
    float2 distortion = localUv * 2.0f - 1.0f;
    if ((textureFlags & 4) != 0)
        distortion = distortionMap.Sample(particleSampler, frac(localUv + offset)).rg * 2.0f - 1.0f;
    if ((textureFlags & 4) != 0 && distortionStrength != 0.0f)
        offset += distortion * distortionStrength;
    if (any(offset != 0.0f))
        localUv = frac(localUv + offset);
    else
        localUv = saturate(localUv);
    float4 color = input.color;
    if (kind == 1)
    {
        const float2 uv = input.uvBounds.xy + localUv * extent;
        const float2 nextExtent = input.nextUvBounds.zw - input.nextUvBounds.xy;
        const float2 nextUv = input.nextUvBounds.xy + localUv * nextExtent;
        color *= lerp(primary.Sample(particleSampler, uv),
            primary.Sample(particleSampler, nextUv), input.frameBlend);
    }
    else if (kind == 0)
    {
        const float2 centeredUv = input.uv * 2.0f - 1.0f;
        color.a *= 1.0f - smoothstep(0.35f, 1.0f, length(centeredUv));
    }
    if ((textureFlags & 1) != 0)
        color *= secondary.Sample(particleSampler, localUv);
    if ((textureFlags & 2) != 0)
        color.a *= mask.Sample(particleSampler, localUv).r;
    if (refractionStrength != 0.0f)
    {
        uint width, height;
        sceneColor.GetDimensions(width, height);
        const float2 pixel = clamp(input.position.xy + distortion * refractionStrength,
            depthViewport.xy + 0.5f, depthViewport.xy + depthViewport.zw - 0.5f);
        color.rgb *= sceneColor.Sample(particleSampler, pixel / float2(width, height)).rgb;
    }
    if (softness > 0.0f)
    {
        const float2 ndc = float2(
            (input.position.x - depthViewport.x) / depthViewport.z * 2.0f - 1.0f,
            1.0f - (input.position.y - depthViewport.y) / depthViewport.w * 2.0f);
        const float sceneZ = sceneDepth.Load(int3(int2(input.position.xy), 0));
        const float zScale = depthRange.y - depthRange.x;
        const float4 sceneH = mul(float4(ndc, (sceneZ - depthRange.x) / zScale, 1.0f), inverseViewProjection);
        const float4 particleH = mul(float4(ndc, (input.position.z - depthRange.x) / zScale, 1.0f), inverseViewProjection);
        if (abs(sceneH.w) > 1e-8f && abs(particleH.w) > 1e-8f)
        {
            const float distance = dot(sceneH.xyz / sceneH.w - particleH.xyz / particleH.w, cameraForward.xyz);
            color.a *= saturate(distance / softness);
        }
    }
    return color;
}

float4 psMain(PixelInput input) : SV_TARGET
{
    float4 color = shadeParticle(input, appearance, softDistance, uvDistortion, refractionPixels,
        particleTexture, secondaryTexture, maskTexture, distortionTexture);
    const uint blendMode = (appearance >> 8) & 255;
    if (blendMode == 2 || blendMode == 4)
        color.rgb *= color.a;
    else if (blendMode == 3)
        color.rgb = lerp(1.0f, color.rgb, color.a);
    return color;
}

// Indirect layout: root SRV address, four root constants, DRAW arguments, sort metadata.
struct ParticleCommand
{
    uint2 geometry;
    uint4 selection;
    uint4 draw;
    float depth;
    uint ordinal;
};

struct ParticleMaterial
{
    uint4 textures;
    uint appearance;
    float softness;
    float distortion;
    float refraction;
};

struct ParticleEmitterDraw
{
    row_major float4x4 world;
    float4 sizeScale;
    float4 orientation;
    float4 scrollTiming;
    float4 atlasInset;
    uint4 flipbook;
    uint4 sequence;
};

struct ParticleMeshInstance
{
    float3 position;
    float3 right;
    float3 up;
    float3 normal;
    float4 color;
    float4 uvBounds;
    float2 uvOffset;
    float4 nextUvBounds;
    float blend;
    float padding;
};

struct ParticleMeshVertex { float3 position; float2 uv; };
struct ParticleFrame { uint cell; float duration; };

cbuffer ParticleSelection : register(b9) { uint4 particleSelection; };
cbuffer ParticleCamera : register(b10)
{
    float4 particleRight;
    float4 particleUp;
    float4 particleForward;
    float4 particleEye;
};
cbuffer ParticleTarget : register(b12) { uint particleTargetUnorm; };
ByteAddressBuffer particleGeometry : register(t9);
StructuredBuffer<ParticleMeshInstance> particleMeshInstances : register(t10);
StructuredBuffer<ParticleMaterial> particleMaterials : register(t11);
StructuredBuffer<ParticleEmitterDraw> particleEmitters : register(t12);
StructuredBuffer<ParticleFrame> particleFrames : register(t13);
ByteAddressBuffer sortTrails : register(t14);
Texture2D sortedTextures[] : register(t0, space1);

cbuffer ParticleSortConstants : register(b11)
{
    uint4 sortParameters; // mode, count, offset/stride, material/span
    uint4 sortGeometry;   // GPU address in xy, emitter in z
};
RWStructuredBuffer<ParticleCommand> particleCommands : register(u2);
RWByteAddressBuffer particleCommandCount : register(u3);

struct TrailSegment
{
    GpuTrailPoint previous, start, end, next;
    float width, lifetime;
    uint blend;
};

GpuTrailPoint readTrailPoint(ByteAddressBuffer geometry, uint base, GpuTrailHeader header, uint index,
    uint tipIndex = 0xffffffffu)
{
    const uint slot = index == tipIndex ? trailPointCapacity : (header.first + index) % trailPointCapacity;
    return geometry.Load<GpuTrailPoint>(trailPointAddress(base, slot));
}

bool readTrailSegment(ByteAddressBuffer geometry, uint index, out TrailSegment segment)
{
    segment = (TrailSegment)0;
    const uint base = (index / trailPointCapacity) * trailStride;
    const uint offset = index % trailPointCapacity;
    const GpuTrailHeader header = geometry.Load<GpuTrailHeader>(base);
    uint count = header.count;
    if (count == 0 || offset + 1 > count)
        return false;
    uint tipIndex = 0xffffffffu;
    if (header.attached != 0)
    {
        const GpuTrailPoint tip = geometry.Load<GpuTrailPoint>(trailPointAddress(base, trailPointCapacity));
        const GpuTrailPoint last = readTrailPoint(geometry, base, header, count - 1);
        const float3 movement = tip.positionAge.xyz - last.positionAge.xyz;
        if (dot(movement, movement) <= 1e-10f)
            tipIndex = count - 1;
        else
            tipIndex = count++;
    }
    if (offset + 1 >= count)
        return false;
    segment.start = readTrailPoint(geometry, base, header, offset, tipIndex);
    segment.end = readTrailPoint(geometry, base, header, offset + 1, tipIndex);
    const uint previous = offset == 0 ? 0 : offset - 1;
    const uint next = min(offset + 2, count - 1);
    segment.previous = readTrailPoint(geometry, base, header, previous, tipIndex);
    segment.next = readTrailPoint(geometry, base, header, next, tipIndex);
    segment.width = header.width;
    segment.lifetime = header.lifetime;
    segment.blend = header.blend;
    const float3 delta = segment.end.positionAge.xyz - segment.start.positionAge.xyz;
    return dot(delta, delta) > 1e-10f && all(isfinite(delta))
        && (segment.start.color.a > 0.0f || segment.end.color.a > 0.0f);
}

bool fartherFirst(ParticleCommand a, ParticleCommand b)
{
    if ((a.draw.y != 0) != (b.draw.y != 0))
        return a.draw.y != 0;
    return a.depth == b.depth ? a.ordinal < b.ordinal : a.depth > b.depth;
}

[numthreads(128, 1, 1)]
void csSortParticles(uint index : SV_DispatchThreadID)
{
    if (index >= sortParameters.y)
        return;
    if (sortParameters.x == 0)
    {
        const uint destination = index + sortParameters.z;
        ParticleCommand command = (ParticleCommand)0;
        command.ordinal = destination;
        if (index < compactDrawData.Load(4))
        {
            const uint slot = compactDrawData.Load(drawArgumentBytes + index * 4);
            const GpuParticleState particle = gpuParticles[slot];
            const float3 center = mul(float4(particle.positionAge.xyz, 1.0f),
                particleEmitters[sortGeometry.z].world).xyz;
            command.depth = dot(center - particleEye.xyz, particleForward.xyz);
            if (isfinite(command.depth))
            {
                command.geometry = sortGeometry.xy;
                command.selection = uint4(slot, 2u, sortParameters.w, sortGeometry.z);
                command.draw = uint4(6u, 1u, 0u, 0u);
            }
        }
        particleCommands[destination] = command;
    }
    else if (sortParameters.x == 1)
    {
        const uint partner = index ^ sortParameters.z;
        if (partner <= index)
            return;
        const ParticleCommand a = particleCommands[index];
        const ParticleCommand b = particleCommands[partner];
        const bool descending = (index & sortParameters.w) == 0;
        if (descending ? fartherFirst(b, a) : fartherFirst(a, b))
        {
            particleCommands[index] = b;
            particleCommands[partner] = a;
        }
    }
    else if (sortParameters.x == 2)
    {
        if (index != 0)
            return;
        uint lo = 0, hi = sortParameters.y;
        while (lo < hi)
        {
            const uint mid = lo + (hi - lo) / 2u;
            if (particleCommands[mid].draw.y != 0)
                lo = mid + 1u;
            else
                hi = mid;
        }
        particleCommandCount.Store(0, lo);
    }
    else if (sortParameters.x == 4)
    {
        ParticleCommand command = (ParticleCommand)0;
        const uint destination = index + sortParameters.z;
        command.ordinal = destination;
        TrailSegment segment;
        if (readTrailSegment(sortTrails, index, segment))
        {
            const float3 center = segment.start.positionAge.xyz * 0.5f + segment.end.positionAge.xyz * 0.5f;
            command.depth = dot(center - particleEye.xyz, particleForward.xyz);
            if (isfinite(command.depth))
            {
                command.geometry = sortGeometry.xy;
                command.selection = uint4(index, 3u, sortParameters.w + segment.blend, 0u);
                command.draw = uint4(6u, 1u, 0u, 0u);
            }
        }
        particleCommands[destination] = command;
    }
    else
        particleCommands[index + sortParameters.z] = (ParticleCommand)0;
}

float3 directionOr(float3 value, float3 fallback)
{
    const float squared = dot(value, value);
    return squared < 1e-8f || !isfinite(squared) ? fallback : value * rsqrt(squared);
}

float3 trailSide(float3 tangent)
{
    const float scale = max(abs(tangent.x), max(abs(tangent.y), abs(tangent.z)));
    if (scale < 1e-10f || !isfinite(scale))
        return particleRight.xyz;
    const float3 side = cross(tangent / scale, particleForward.xyz);
    const float lengthSquared = dot(side, side);
    return lengthSquared < 1e-10f ? particleRight.xyz : side * rsqrt(lengthSquared);
}

float3 sampleParticleFlipbook(ParticleEmitterDraw emitter, float age, float normalizedAge, uint seed)
{
    const uint mode = emitter.flipbook.z;
    const uint loops = emitter.flipbook.w;
    const uint count = mode == 2 ? emitter.sequence.y : emitter.flipbook.x * emitter.flipbook.y;
    const uint start = emitter.sequence.z != 0 ? seed % count : 0;
    uint frame = 0;
    float blend = 0;
    bool hold = false;
    if (mode == 2)
    {
        const float duration = emitter.scrollTiming.w;
        const float cycle = floor(age / duration);
        hold = loops != 0 && cycle >= loops;
        float remaining = fmod(age, duration);
        frame = count - 1;
        if (!hold)
        {
            for (uint i = 0; i < count; ++i)
            {
                const float length = particleFrames[emitter.sequence.x + (start + i) % count].duration;
                if (remaining < length || i + 1 == count)
                {
                    frame = i;
                    blend = saturate(remaining / length);
                    break;
                }
                remaining -= length;
            }
            hold = loops != 0 && cycle >= loops - 1 && frame + 1 == count;
        }
    }
    else
    {
        const float phase = mode == 0 ? normalizedAge * count : age * emitter.scrollTiming.z;
        const float cycle = floor(phase / count);
        const bool finished = mode == 0 ? phase >= count : loops != 0 && cycle >= loops;
        frame = finished ? count - 1 : (uint)fmod(phase, count);
        hold = finished || (frame + 1 == count && (mode == 0 || (loops != 0 && cycle >= loops - 1)));
        blend = frac(phase);
    }
    const uint current = (start + frame) % count;
    const uint next = hold ? current : (current + 1) % count;
    return float3(mode == 2 ? particleFrames[emitter.sequence.x + current].cell : current,
        mode == 2 ? particleFrames[emitter.sequence.x + next].cell : next,
        hold || emitter.sequence.w == 0 ? 0.0f : blend);
}

float4 particleAtlasBounds(uint frame, uint2 grid, float2 inset)
{
    const float2 extent = rcp(float2(grid));
    const float2 origin = float2(frame % grid.x, frame / grid.x) * extent;
    return float4(origin + inset, origin + extent - inset);
}

PixelInput vsSorted(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    const uint index = particleSelection.x + instance;
    if (particleSelection.y == 0)
        return expandQuad(vertex, particleGeometry.Load<QuadInstance>(index * 128));
    if (particleSelection.y == 1)
    {
        const ParticleMeshInstance mesh = particleMeshInstances[index];
        const ParticleMeshVertex meshVertex = particleGeometry.Load<ParticleMeshVertex>(vertex * 20);
        MeshVertexInput input;
        input.position = meshVertex.position;
        input.uv = meshVertex.uv;
        input.instancePosition = mesh.position;
        input.instanceRight = mesh.right;
        input.instanceUp = mesh.up;
        input.instanceNormal = mesh.normal;
        input.color = mesh.color;
        input.uvBounds = mesh.uvBounds;
        input.uvOffset = mesh.uvOffset;
        input.nextUvBounds = mesh.nextUvBounds;
        input.frameBlend = mesh.blend;
        return vsMesh(input);
    }
    if (particleSelection.y == 3)
    {
        TrailSegment segment;
        readTrailSegment(particleGeometry, index, segment);
        const float startFade = saturate(1.0f - segment.start.positionAge.w / segment.lifetime);
        const float endFade = saturate(1.0f - segment.end.positionAge.w / segment.lifetime);
        QuadInstance band = (QuadInstance)0;
        band.isBand = 1;
        band.origin = segment.start.positionAge.xyz;
        band.rightOrEnd = segment.end.positionAge.xyz;
        band.upOrStartSide = trailSide(band.rightOrEnd - segment.previous.positionAge.xyz)
            * (segment.width * startFade * 0.5f);
        band.endSide = trailSide(segment.next.positionAge.xyz - band.origin)
            * (segment.width * endFade * 0.5f);
        band.color = float4(segment.start.color.rgb, segment.start.color.a * startFade);
        band.endColor = float4(segment.end.color.rgb, segment.end.color.a * endFade);
        band.uvBounds = band.nextUvBounds = float4(0, 0, 1, 1);
        return expandQuad(vertex, band);
    }
    const GpuParticleState particle = particleGeometry.Load<GpuParticleState>(index * 96);
    const ParticleEmitterDraw emitter = particleEmitters[particleSelection.w];
    const float age = saturate(particle.positionAge.w / particle.velocityLifetime.w);
    const float3 forward = particleForward.xyz;
    float3 right = particleRight.xyz;
    float3 up = particleUp.xyz;
    float stretch = 1.0f;
    const uint orientation = (uint)emitter.orientation.w;
    if (orientation == 1 || orientation == 5)
    {
        const float3 velocity = mul(float4(particle.velocityLifetime.xyz, 0.0f), emitter.world).xyz;
        const float3 projected = velocity - forward * dot(velocity, forward);
        if (dot(projected, projected) > 1e-8f)
        {
            up = normalize(projected);
            right = normalize(cross(up, forward));
        }
        if (orientation == 5)
            stretch += length(velocity) * emitter.sizeScale.w;
    }
    else if (orientation == 2)
    {
        up = normalize(emitter.orientation.xyz);
        right = directionOr(cross(up, forward), normalize(cross(up, right)));
    }
    else if (orientation == 3 || orientation == 4)
    {
        const float3 normal = orientation == 3 ? float3(0, 1, 0) : normalize(emitter.orientation.xyz);
        const float3 fallback = abs(normal.y) > 0.99f ? float3(0, 0, 1) : float3(0, 1, 0);
        right = directionOr(cross(fallback, normal), particleRight.xyz);
        up = normalize(cross(normal, right));
    }
    const float size = lerp(particle.sizeRotation.x, particle.sizeRotation.y, age) * emitter.sizeScale.z * 0.5f;
    float sine, cosine;
    sincos(particle.sizeRotation.z, sine, cosine);
    QuadInstance quad = (QuadInstance)0;
    quad.origin = mul(float4(particle.positionAge.xyz, 1.0f), emitter.world).xyz;
    quad.rightOrEnd = (right * cosine + up * sine) * size * emitter.sizeScale.x;
    quad.upOrStartSide = (up * cosine - right * sine) * size * emitter.sizeScale.y * stretch;
    quad.color = lerp(particle.startColor, particle.endColor, age);
    quad.uvOffset = frac(particle.positionAge.w * emitter.scrollTiming.xy);
    if ((particleMaterials[particleSelection.z].appearance & 255) == 1)
    {
        const float3 frames = sampleParticleFlipbook(emitter, particle.positionAge.w, age, particle.randomData.x);
        const float2 inset = emitter.atlasInset.xy;
        quad.uvBounds = particleAtlasBounds((uint)frames.x, emitter.flipbook.xy, inset);
        quad.nextUvBounds = particleAtlasBounds((uint)frames.y, emitter.flipbook.xy, inset);
        quad.frameBlend = frames.z;
    }
    else
    {
        quad.uvBounds = float4(0, 0, 1, 1);
        quad.nextUvBounds = quad.uvBounds;
    }
    return expandQuad(vertex, quad);
}

struct ParticleBlendOutput { float4 source : SV_Target0; float4 destinationFactor : SV_Target1; };

ParticleBlendOutput psSorted(PixelInput input)
{
    const ParticleMaterial material = particleMaterials[particleSelection.z];
    Texture2D primary = sortedTextures[NonUniformResourceIndex(material.textures.x)];
    Texture2D secondary = sortedTextures[NonUniformResourceIndex(material.textures.y)];
    Texture2D mask = sortedTextures[NonUniformResourceIndex(material.textures.z)];
    Texture2D distortion = sortedTextures[NonUniformResourceIndex(material.textures.w)];
    const float4 color = shadeParticle(input, material.appearance, material.softness, material.distortion,
        material.refraction, primary, secondary, mask, distortion);
    const uint mode = (material.appearance >> 8) & 255;
    const bool normalizedTarget = particleTargetUnorm != 0;
    const float alpha = normalizedTarget ? saturate(color.a) : color.a;
    const float3 weighted = (mode < 2 && normalizedTarget ? saturate(color.rgb) : color.rgb)
        * (mode < 2 ? alpha : color.a);
    ParticleBlendOutput output;
    output.source = float4(normalizedTarget ? saturate(weighted) : weighted, alpha);
    output.destinationFactor = float4((1.0f - alpha).xxx, 0.0f);
    if (mode == 1)
    {
        output.source.a = 0.0f;
        output.destinationFactor = 1.0f;
    }
    else if (mode == 3)
    {
        output.source = 0.0f;
        const float3 factor = lerp(1.0f, color.rgb, color.a);
        output.destinationFactor = float4(normalizedTarget ? saturate(factor) : factor, 1.0f);
    }
    else if (mode == 4)
    {
        output.source.a = 0.0f;
        output.destinationFactor = float4(1.0f - output.source.rgb, 1.0f);
    }
    return output;
}
