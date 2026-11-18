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
};

RWStructuredBuffer<GpuParticleState> simulationParticles : register(u0);

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

[numthreads(64, 1, 1)]
void csSimulate(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= simulationHeader.x)
        return;

    GpuParticleState particle = simulationParticles[index];
    if (simulationHeader.y != 0)
        particle.positionAge.w = -1.0f;
    const uint spawnOffset = (index + simulationHeader.x - simulationHeader.z)
        % simulationHeader.x;
    if (spawnOffset < simulationHeader.w)
    {
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
        simulationParticles[index] = particle;
        return;
    }

    if (simulationHeader.y != 0)
    {
        simulationParticles[index] = particle;
        return;
    }

    if (particle.positionAge.w < 0.0f)
        return;
    particle.positionAge.w += simulationTiming.y;
    if (particle.positionAge.w >= particle.velocityLifetime.w)
        particle.positionAge.w = -1.0f;
    else
    {
        particle.velocityLifetime.xyz += simulationGravityDrag.xyz * simulationTiming.y;
        particle.velocityLifetime.xyz *= exp(-simulationGravityDrag.w * simulationTiming.y);
        particle.positionAge.xyz += particle.velocityLifetime.xyz * simulationTiming.y;
        particle.sizeRotation.z += particle.sizeRotation.w * simulationTiming.y;
    }
    simulationParticles[index] = particle;
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
    if (instanceId >= gpuDrawHeader.x)
    {
        output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
        return output;
    }
    const GpuParticleState particle = gpuParticles[instanceId];
    const float life = particle.velocityLifetime.w;
    if (particle.positionAge.w < 0.0f || life <= 0.0f)
    {
        output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
        return output;
    }
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

PixelInput vsMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const uint cornerIndices[6] = { 0, 1, 2, 2, 1, 3 };
    const uint corner = cornerIndices[vertexId];
    const bool positiveSide = (corner & 1u) != 0;
    const bool upperOrEnd = corner >= 2u;
    const float sideSign = positiveSide ? 1.0f : -1.0f;
    const QuadInstance instance = quadInstances[instanceId];
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

float4 psMain(PixelInput input) : SV_TARGET
{
    const uint kind = appearance & 255;
    const uint blendMode = (appearance >> 8) & 255;
    const uint textureFlags = appearance >> 16;
    const float2 extent = input.uvBounds.zw - input.uvBounds.xy;
    float2 localUv = float2(
        extent.x > 0.0f ? (input.uv.x - input.uvBounds.x) / extent.x : 0.5f,
        extent.y > 0.0f ? (input.uv.y - input.uvBounds.y) / extent.y : 0.5f);
    float2 offset = input.uvOffset;
    float2 distortion = localUv * 2.0f - 1.0f;
    if ((textureFlags & 4) != 0)
        distortion = distortionTexture.Sample(particleSampler, frac(localUv + offset)).rg * 2.0f - 1.0f;
    if ((textureFlags & 4) != 0 && uvDistortion != 0.0f)
        offset += distortion * uvDistortion;
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
        color *= lerp(particleTexture.Sample(particleSampler, uv),
            particleTexture.Sample(particleSampler, nextUv), input.frameBlend);
    }
    else if (kind == 0)
    {
        const float2 centeredUv = input.uv * 2.0f - 1.0f;
        color.a *= 1.0f - smoothstep(0.35f, 1.0f, length(centeredUv));
    }
    if ((textureFlags & 1) != 0)
        color *= secondaryTexture.Sample(particleSampler, localUv);
    if ((textureFlags & 2) != 0)
        color.a *= maskTexture.Sample(particleSampler, localUv).r;
    if (refractionPixels != 0.0f)
    {
        uint width, height;
        sceneColor.GetDimensions(width, height);
        const float2 pixel = clamp(input.position.xy + distortion * refractionPixels,
            depthViewport.xy + 0.5f, depthViewport.xy + depthViewport.zw - 0.5f);
        color.rgb *= sceneColor.Sample(particleSampler, pixel / float2(width, height)).rgb;
    }
    if (softDistance > 0.0f)
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
            color.a *= saturate(distance / softDistance);
        }
    }
    if (blendMode == 2 || blendMode == 4)
        color.rgb *= color.a;
    else if (blendMode == 3)
        color.rgb = lerp(1.0f, color.rgb, color.a);
    return color;
}
