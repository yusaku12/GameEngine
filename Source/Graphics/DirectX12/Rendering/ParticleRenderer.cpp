#include "Pch.h"
#include "Graphics\DirectX12\Rendering\ParticleRenderer.h"
#include "Graphics\DirectX12\Diagnostics\GpuProfiler.h"
#include "Graphics\DirectX12\Command\Fence.h"
#include "Graphics\DirectX12\Device\Device.h"
#include "Graphics\Shader\Shader.h"
#include "Graphics\Texture\TextureManager.h"
#include "Core\Containers\String.h"
#include "Core\Logging\Logger.h"
#include "Assets\Model\ModelManager.h"
#include <bit>
#include <cwctype>

namespace Engine
{
    namespace
    {
        constexpr std::uint32_t MAX_GPU_PARTICLES = 4096;
        constexpr std::uint32_t GPU_TRAIL_POINTS = 16;
        constexpr std::uint32_t GPU_TRAIL_STRIDE = 32 + (GPU_TRAIL_POINTS + 1) * 32;
        constexpr std::uint32_t GPU_CURVE_SETS = 4;
        // Each set starts with a smooth flag and ten (key offset, count) descriptors.
        constexpr std::uint32_t GPU_CURVE_SET_WORDS = 11;
        constexpr std::uint32_t GPU_CURVE_TRACK_KEYS = 32;
        // csCompact uses one group of 128 lanes with up to 32 slots per lane.
        static_assert(MAX_GPU_PARTICLES <= 128 * 32);
        // The Trail free-list pass has 64 lanes and uses two masks of 32 slots per lane.
        static_assert(MAX_GPU_PARTICLES <= 64 * 64);
        static_assert(sizeof(D3D12_DRAW_ARGUMENTS) == 16);
        constexpr D3D12_RESOURCE_STATES GPU_DRAW_DATA_READ_STATE =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        constexpr float PARTICLE_PI = 3.14159265358979323846f;

        bool isFinite(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool isFinite(const Vector4& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y)
                && std::isfinite(value.z) && std::isfinite(value.w);
        }

        bool isFinite(const Matrix& value) noexcept
        {
            const std::array elements{
                value._11, value._12, value._13, value._14,
                value._21, value._22, value._23, value._24,
                value._31, value._32, value._33, value._34,
                value._41, value._42, value._43, value._44,
            };
            return std::all_of(elements.begin(), elements.end(),
                [](const float element) { return std::isfinite(element); });
        }

        struct GpuSimulationConstants
        {
            std::array<std::uint32_t, 4> header;
            Vector4 timing;
            Vector4 speedShape;
            std::uint32_t shape;
            Vector3 boxExtents;
            Vector4 direction;
            Vector4 gravityDrag;
            Vector4 sizeRange;
            Vector4 startColor;
            Vector4 endColor;
            Vector4 initialColorMin;
            Vector4 initialColorMax;
            Vector4 rotation;
            std::array<std::uint32_t, 4> flags;
        };
        static_assert(sizeof(GpuSimulationConstants) == 13 * 16);

        struct GpuParticleDrawConstants
        {
            std::array<std::uint32_t, 4> header;
            Vector4 cameraRightWorldScale;
            Vector4 cameraUp;
        };
        static_assert(sizeof(GpuParticleDrawConstants) == 12 * sizeof(std::uint32_t));

        struct GpuParticleState
        {
            Vector4 positionAge;
            Vector4 velocityLifetime;
            Vector4 startColor;
            Vector4 endColor;
            Vector4 sizeRotation;
            std::array<std::uint32_t, 4> randomData;
            Vector4 evaluatedColor;
            Vector4 evaluatedSize;
            Vector4 evaluatedVelocity;
        };
        static_assert(sizeof(GpuParticleState) == 144);

        struct GpuSimulationSettings
        {
            Matrix trailWorld;
            Vector4 trailSettings;
            std::uint32_t curveVersion;
            std::uint32_t curveSetCount;
            Vector2 birthSizeScale;
            Vector4 emission; // active time, fixed rate, cycle duration, complete cycle time
            Vector4 emissionPhases; // start and end, each split into high/low
            Vector2 emissionDelay;
            float emissionTailTime;
            std::uint32_t emissionWraps;
            std::array<std::uint32_t, 4> emissionHeader; // table offset, rate keys, bursts, initial count + flag
            Vector3 motionStartOffset;
            float motionDistance;
            Vector3 motionDelta;
            float distanceSpacing;
            Vector3 emitterVelocity;
            float velocityInheritance;
        };
        static_assert(sizeof(GpuSimulationSettings) == 208);
        constexpr std::uint32_t GPU_SCHEDULER_WORDS = 9;

        Vector2 splitEmissionTime(const double time) noexcept
        {
            const auto high = static_cast<float>(time);
            return Vector2(high, static_cast<float>(time - high));
        }

        constexpr std::size_t GPU_RATE_KEY_WORDS = 2;
        constexpr std::size_t GPU_BURST_WORDS = 6;
        // Randomized bursts are authoring-limited to 8192 events per 1/60 s; deterministic counts saturate.
        constexpr double GPU_MAX_CYCLE_WRAPS = 131072.0;

    }

    bool ParticleRenderer::initialize(DX12Device& device, const DX12Fence& fence)
    {
        if (m_device != nullptr)
        {
            LOG_ERROR("[ParticleEffect] Particle renderer is already initialized.");
            return false;
        }

        m_device = &device;
        m_fence = &fence;
        D3D12_INDIRECT_ARGUMENT_DESC argument{};
        argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        const D3D12_COMMAND_SIGNATURE_DESC signature{
            sizeof(D3D12_DRAW_ARGUMENTS), 1, &argument, 0 };
        const HRESULT result = device.get()->CreateCommandSignature(&signature, nullptr,
            IID_PPV_ARGS(&m_gpuDrawSignature));
        if (FAILED(result))
        {
            LOG_ERROR("[ParticleEffect] Failed to create the indirect draw signature (HRESULT: 0x{:08X}).",
                static_cast<unsigned long>(result));
            static_cast<void>(finalize());
            return false;
        }
        m_quadInstanceCapacities.fill(0);
        m_meshInstanceCapacities.fill(0);
        m_frameRecorded.fill(false);
        m_frameUsesQuadInstanceBuffer.fill(false);
        m_frameUsesMeshInstanceBuffer.fill(false);
        for (DX12CommandList& commandList : m_commandLists)
        {
            if (!commandList.initialize(*device.get(), DX12CommandQueueType::DIRECT))
            {
                static_cast<void>(finalize());
                return false;
            }
        }
        return true;
    }

    bool ParticleRenderer::finalize()
    {
        bool succeeded = true;
        for (DX12UploadBuffer& buffer : m_quadInstanceBuffers)
            succeeded = buffer.finalize() && succeeded;
        for (DX12UploadBuffer& buffer : m_meshInstanceBuffers)
            succeeded = buffer.finalize() && succeeded;
        for (auto& buffer : m_gpuSimulationSettings)
            succeeded = buffer.finalize() && succeeded;
        for (auto& buffer : m_gpuCurveSettings)
            succeeded = buffer.finalize() && succeeded;
        for (auto& frame : m_sortedFrames)
        {
            if (!frame.upload.finalize())
                return false;
            frame.commands.finalize();
            frame.count.finalize();
        }
        if (m_fence != nullptr)
        {
            for (const auto& [id, state] : m_gpuEmitterStates)
            {
                static_cast<void>(id);
                if (state->lastFenceValue != 0 && !m_fence->waitOnCpu(state->lastFenceValue))
                {
                    LOG_ERROR("[ParticleEffect] Cannot release GPU particle state before its draw completes.");
                    return false;
                }
                for (const RetiredGpuBuffer& retired : state->retiredBuffers)
                    if (retired.fenceValue != 0 && !m_fence->waitOnCpu(retired.fenceValue))
                    {
                        LOG_ERROR("[ParticleEffect] Cannot release a retired GPU particle buffer.");
                        return false;
                    }
            }
        }
        if (!succeeded)
        {
            LOG_ERROR("[ParticleEffect] Cannot release particle buffers before GPU completion.");
            return false;
        }
        for (DX12CommandList& commandList : m_commandLists)
            commandList.finalize();
        for (std::size_t index = 0; index < FRAME_COUNT; ++index)
        {
            m_sceneCopies[index].finalize();
            if (m_sceneCopyViews[index])
            {
                if (!TextureManager::instance().getDescriptorHeap().release(m_sceneCopyViews[index]->cpu.index))
                {
                    LOG_ERROR("[ParticleEffect] Failed to release the scene copy descriptor.");
                    succeeded = false;
                }
                m_sceneCopyViews[index].reset();
            }
        }
        for (auto& pipeline : m_forwardPipelines)
            pipeline.finalize();
        for (auto& pipeline : m_deferredPipelines)
            pipeline.finalize();
        for (auto& pipeline : m_forwardMeshPipelines)
            pipeline.finalize();
        for (auto& pipeline : m_deferredMeshPipelines)
            pipeline.finalize();
        for (auto& pipeline : m_forwardGpuPipelines)
            pipeline.finalize();
        for (auto& pipeline : m_deferredGpuPipelines)
            pipeline.finalize();
        m_gpuSimulationPipeline.finalize();
        m_gpuCompactionPipeline.finalize();
        m_sortPipeline.finalize();
        for (auto& pipeline : m_sortedPipelines)
            pipeline.finalize();
        for (auto& signature : m_sortedSignatures)
            signature.Reset();
        m_sortedStaging = {};
        m_gpuDrawSignature.Reset();
        m_gpuEmitterStates.clear();
        m_curveBirthScratch.clear();
        m_curveUploadOffsets.clear();
        m_quadInstanceCapacities.fill(0);
        m_meshInstanceCapacities.fill(0);
        for (auto& usedMeshes : m_usedMeshes)
            usedMeshes.clear();
        m_frameRecorded.fill(false);
        m_frameUsesQuadInstanceBuffer.fill(false);
        m_frameUsesMeshInstanceBuffer.fill(false);
        m_particles.clear();
        m_trails.clear();
        m_ribbons.clear();
        m_gpuEmitterSubmissions.clear();
        m_removedGpuEmitters.clear();
        m_drawItems.clear();
        m_quadInstances.clear();
        m_meshInstances.clear();
        m_batches.clear();
        m_resolvedVisuals.clear();
        m_renderedParticles = 0;
        m_renderedTrails = 0;
        m_renderedRibbons = 0;
        m_gpuEmitterDrawCount = 0;
        m_textureCache.clear();
        m_linearTextureCache.clear();
        for (auto& failures : m_failedTextures)
            failures.clear();
        m_meshCache.clear();
        m_failedMeshes.clear();
        m_frameNumber = 0;
        m_device = nullptr;
        m_fence = nullptr;
        ParticleRenderSubmissionQueue::instance().setGpuSortingSupported(false);
        ParticleRenderSubmissionQueue::instance().clear();
        return succeeded;
    }

    bool ParticleRenderer::rebuildPipeline(const DX12Shader& vertexShader,
        const DX12Shader& meshVertexShader, const DX12Shader& pixelShader,
        const DX12Shader& gpuVertexShader, const DX12Shader& gpuPixelShader,
        const DX12Shader& simulationShader, const DX12Shader& compactionShader,
        const DX12Shader& sortShader, const DX12Shader& sortedVertexShader, const DX12Shader& sortedPixelShader)
    {
        if (m_device == nullptr || m_device->get() == nullptr)
        {
            LOG_ERROR("[ParticleEffect] Cannot build pipelines before renderer initialization.");
            return false;
        }

        static_assert(sizeof(QuadInstance) == 128 && offsetof(QuadInstance, isBand) == 12
            && offsetof(QuadInstance, rightOrEnd) == 16 && offsetof(QuadInstance, frameBlend) == 28
            && offsetof(QuadInstance, upOrStartSide) == 32 && offsetof(QuadInstance, endSide) == 44
            && offsetof(QuadInstance, uvOffset) == 56 && offsetof(QuadInstance, color) == 64
            && offsetof(QuadInstance, endColor) == 80 && offsetof(QuadInstance, uvBounds) == 96
            && offsetof(QuadInstance, nextUvBounds) == 112);
        constexpr std::array meshInputLayout{
            D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
                0, static_cast<UINT>(offsetof(MeshVertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
                0, static_cast<UINT>(offsetof(MeshVertex, uv)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "INSTANCE_POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
                1, static_cast<UINT>(offsetof(MeshInstance, position)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "INSTANCE_RIGHT", 0, DXGI_FORMAT_R32G32B32_FLOAT,
                1, static_cast<UINT>(offsetof(MeshInstance, right)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "INSTANCE_UP", 0, DXGI_FORMAT_R32G32B32_FLOAT,
                1, static_cast<UINT>(offsetof(MeshInstance, up)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "INSTANCE_NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,
                1, static_cast<UINT>(offsetof(MeshInstance, normal)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
                1, static_cast<UINT>(offsetof(MeshInstance, color)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
                1, static_cast<UINT>(offsetof(MeshInstance, uvBounds)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT,
                1, static_cast<UINT>(offsetof(MeshInstance, uvOffset)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,
                1, static_cast<UINT>(offsetof(MeshInstance, nextUvBounds)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT,
                1, static_cast<UINT>(offsetof(MeshInstance, frameBlend)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        };
        static_assert(sizeof(MeshInstance) == 112 && offsetof(MeshInstance, position) == 0
            && offsetof(MeshInstance, right) == 12 && offsetof(MeshInstance, up) == 24
            && offsetof(MeshInstance, normal) == 36 && offsetof(MeshInstance, color) == 48
            && offsetof(MeshInstance, uvBounds) == 64 && offsetof(MeshInstance, uvOffset) == 80
            && offsetof(MeshInstance, nextUvBounds) == 88 && offsetof(MeshInstance, frameBlend) == 104);
        D3D12_ROOT_PARAMETER viewProjectionConstants{};
        viewProjectionConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        viewProjectionConstants.Constants = { 0, 0, 16 };
        viewProjectionConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_DESCRIPTOR_RANGE textureRange{};
        textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        textureRange.NumDescriptors = 1;
        textureRange.BaseShaderRegister = 0;
        textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER textureTable{};
        textureTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        textureTable.DescriptorTable = { 1, &textureRange };
        textureTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_PARAMETER appearanceConstants{};
        appearanceConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        appearanceConstants.Constants = { 1, 0, 1 };
        appearanceConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_DESCRIPTOR_RANGE depthRange{};
        depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        depthRange.NumDescriptors = 1;
        depthRange.BaseShaderRegister = 1;
        depthRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER depthTable{};
        depthTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        depthTable.DescriptorTable = { 1, &depthRange };
        depthTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_PARAMETER depthConstants{};
        depthConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        depthConstants.Constants = { 2, 0, 28 };
        depthConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_PARAMETER softConstants{};
        softConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        softConstants.Constants = { 3, 0, 1 };
        softConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        std::array<D3D12_DESCRIPTOR_RANGE, 3> auxiliaryRanges{};
        std::array<D3D12_ROOT_PARAMETER, 3> auxiliaryTables{};
        for (std::size_t index = 0; index < auxiliaryTables.size(); ++index)
        {
            auxiliaryRanges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            auxiliaryRanges[index].NumDescriptors = 1;
            auxiliaryRanges[index].BaseShaderRegister = static_cast<UINT>(index + 2);
            auxiliaryRanges[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            auxiliaryTables[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            auxiliaryTables[index].DescriptorTable = { 1, &auxiliaryRanges[index] };
            auxiliaryTables[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_ROOT_PARAMETER distortionConstants{};
        distortionConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        distortionConstants.Constants = { 4, 0, 1 };
        distortionConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_DESCRIPTOR_RANGE sceneRange{};
        sceneRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        sceneRange.NumDescriptors = 1;
        sceneRange.BaseShaderRegister = 5;
        sceneRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER sceneTable{};
        sceneTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        sceneTable.DescriptorTable = { 1, &sceneRange };
        sceneTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_PARAMETER refractionConstants{};
        refractionConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        refractionConstants.Constants = { 5, 0, 1 };
        refractionConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_PARAMETER quadInstanceBuffer{};
        quadInstanceBuffer.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        quadInstanceBuffer.Descriptor.ShaderRegister = 7;
        quadInstanceBuffer.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        const std::array rootParameters{ viewProjectionConstants, textureTable, appearanceConstants,
            depthTable, depthConstants, softConstants, auxiliaryTables[0], auxiliaryTables[1],
            auxiliaryTables[2], distortionConstants, sceneTable, refractionConstants, quadInstanceBuffer };
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD = 0.0f;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        const std::array samplers{ sampler };
        const DX12GraphicsPipelineConfig config{
            .vertexShader = &vertexShader,
            .pixelShader = &pixelShader,
            .inputLayout = {},
            .rootParameters = rootParameters,
            .staticSamplers = samplers,
            .depthStencilFormat = DXGI_FORMAT_D32_FLOAT,
            .enableAlphaBlend = true,
            .enableDepthWrite = false,
            .cullMode = D3D12_CULL_MODE_NONE,
        };
        DX12GraphicsPipelineConfig meshConfig = config;
        meshConfig.vertexShader = &meshVertexShader;
        meshConfig.inputLayout = meshInputLayout;
        meshConfig.rootParameters = std::span(rootParameters).first(rootParameters.size() - 1);

        D3D12_ROOT_PARAMETER gpuTextureTable = textureTable;
        D3D12_ROOT_PARAMETER gpuParticleBuffer{};
        gpuParticleBuffer.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        gpuParticleBuffer.Descriptor.ShaderRegister = 6;
        gpuParticleBuffer.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_PARAMETER gpuWorldConstants{};
        gpuWorldConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        gpuWorldConstants.Constants = { 6, 0, 16 };
        gpuWorldConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_PARAMETER gpuDrawConstants{};
        gpuDrawConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        gpuDrawConstants.Constants = { 7, 0, 12 };
        gpuDrawConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_PARAMETER gpuDrawData = gpuParticleBuffer;
        gpuDrawData.Descriptor.ShaderRegister = 8;
        const std::array gpuRootParameters{
            viewProjectionConstants, gpuTextureTable, appearanceConstants, gpuParticleBuffer,
            gpuWorldConstants, gpuDrawConstants, gpuDrawData };
        DX12GraphicsPipelineConfig gpuConfig = config;
        gpuConfig.vertexShader = &gpuVertexShader;
        gpuConfig.pixelShader = &gpuPixelShader;
        gpuConfig.inputLayout = {};
        gpuConfig.rootParameters = gpuRootParameters;

        std::array<DX12GraphicsPipeline, BLEND_MODE_COUNT> forward;
        std::array<DX12GraphicsPipeline, BLEND_MODE_COUNT> deferred;
        std::array<DX12GraphicsPipeline, BLEND_MODE_COUNT> forwardMesh;
        std::array<DX12GraphicsPipeline, BLEND_MODE_COUNT> deferredMesh;
        std::array<DX12GraphicsPipeline, BLEND_MODE_COUNT> forwardGpu;
        std::array<DX12GraphicsPipeline, BLEND_MODE_COUNT> deferredGpu;
        DX12ComputePipeline gpuSimulation;
        const D3D12_ROOT_PARAMETER gpuSimulationConstants{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
            .Constants = { 0, 0, 52 },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
        };
        D3D12_ROOT_PARAMETER gpuSimulationBuffer{};
        gpuSimulationBuffer.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        gpuSimulationBuffer.Descriptor.ShaderRegister = 0;
        D3D12_ROOT_PARAMETER gpuTrailOutput = gpuSimulationBuffer;
        gpuTrailOutput.Descriptor.ShaderRegister = 4;
        D3D12_ROOT_PARAMETER gpuTrailConstants{};
        gpuTrailConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        gpuTrailConstants.Descriptor.ShaderRegister = 13;
        D3D12_ROOT_PARAMETER gpuCurveInput{};
        gpuCurveInput.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        gpuCurveInput.Descriptor.ShaderRegister = 15;
        D3D12_ROOT_PARAMETER gpuScheduler = gpuSimulationBuffer;
        gpuScheduler.Descriptor.ShaderRegister = 5;
        const std::array gpuSimulationParameters{ gpuSimulationConstants, gpuSimulationBuffer,
            gpuTrailOutput, gpuTrailConstants, gpuCurveInput, gpuScheduler };
        if (!gpuSimulation.initialize(*m_device->get(), simulationShader, gpuSimulationParameters))
        {
            LOG_ERROR("[ParticleEffect] Failed to build the particle simulation compute pipeline.");
            return false;
        }
        DX12ComputePipeline gpuCompaction;
        const D3D12_ROOT_PARAMETER gpuCompactionConstants{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
            .Constants = { 8, 0, 1 },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
        };
        D3D12_ROOT_PARAMETER gpuCompactionInput = gpuParticleBuffer;
        gpuCompactionInput.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_PARAMETER gpuCompactionOutput = gpuSimulationBuffer;
        gpuCompactionOutput.Descriptor.ShaderRegister = 1;
        const std::array gpuCompactionParameters{
            gpuCompactionConstants, gpuCompactionInput, gpuCompactionOutput };
        if (!gpuCompaction.initialize(*m_device->get(), compactionShader, gpuCompactionParameters))
        {
            LOG_ERROR("[ParticleEffect] Failed to build the particle compaction compute pipeline.");
            return false;
        }

        DX12ComputePipeline sortPipeline;
        std::array<DX12GraphicsPipeline, 2> sortedPipelines;
        std::array<Microsoft::WRL::ComPtr<ID3D12CommandSignature>, 2> sortedSignatures;
        const bool supportsSorting = m_device->getCapabilities().resourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2;
        if (supportsSorting)
        {
            const auto rootDescriptor = [](const D3D12_ROOT_PARAMETER_TYPE type, const UINT shaderRegister,
                const D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL)
            {
                D3D12_ROOT_PARAMETER parameter{};
                parameter.ParameterType = type;
                parameter.Descriptor.ShaderRegister = shaderRegister;
                parameter.ShaderVisibility = visibility;
                return parameter;
            };
            const auto constants = [](const UINT shaderRegister, const UINT count)
            {
                D3D12_ROOT_PARAMETER parameter{};
                parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                parameter.Constants = { shaderRegister, 0, count };
                return parameter;
            };
            const auto srv = D3D12_ROOT_PARAMETER_TYPE_SRV;
            const auto uav = D3D12_ROOT_PARAMETER_TYPE_UAV;
            const std::array sortRoots{ constants(11, 8), constants(10, 16),
                rootDescriptor(srv, 6), rootDescriptor(srv, 8), rootDescriptor(srv, 12),
                rootDescriptor(uav, 2), rootDescriptor(uav, 3), rootDescriptor(srv, 14) };
            if (!sortPipeline.initialize(*m_device->get(), sortShader, sortRoots))
                return false;

            const D3D12_DESCRIPTOR_RANGE sortedTextureRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 1, 0 };
            D3D12_ROOT_PARAMETER sortedTextureTable = textureTable;
            sortedTextureTable.DescriptorTable.pDescriptorRanges = &sortedTextureRange;
            const std::array sortedRoots{ viewProjectionConstants, depthTable, sceneTable,
                rootDescriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, 2, D3D12_SHADER_VISIBILITY_PIXEL),
                sortedTextureTable, constants(10, 16), rootDescriptor(srv, 9, D3D12_SHADER_VISIBILITY_VERTEX),
                constants(9, 4), rootDescriptor(srv, 10), rootDescriptor(srv, 11),
                rootDescriptor(srv, 12), rootDescriptor(srv, 13), constants(12, 1) };
            DX12GraphicsPipelineConfig sortedConfig = config;
            sortedConfig.vertexShader = &sortedVertexShader;
            sortedConfig.pixelShader = &sortedPixelShader;
            sortedConfig.rootParameters = sortedRoots;
            sortedConfig.overrideBlend = true;
            // Both channels use source + destination * factor; the shader selects the material's equation.
            sortedConfig.blendOverride = { TRUE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_SRC1_COLOR,
                D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_SRC1_ALPHA, D3D12_BLEND_OP_ADD,
                D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL };
            std::array<D3D12_INDIRECT_ARGUMENT_DESC, 3> arguments{};
            arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
            arguments[0].ShaderResourceView.RootParameterIndex = 6;
            arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            arguments[1].Constant = { 7, 0, 4 };
            arguments[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
            const D3D12_COMMAND_SIGNATURE_DESC signature{ sizeof(ParticleCommand),
                static_cast<UINT>(arguments.size()), arguments.data(), 0 };
            for (std::size_t index = 0; index < sortedPipelines.size(); ++index)
            {
                sortedConfig.renderTargetFormat = index == 0 ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
                if (!sortedPipelines[index].initialize(*m_device->get(), sortedConfig))
                    return false;
                const HRESULT result = m_device->get()->CreateCommandSignature(&signature,
                    sortedPipelines[index].getRootSignature(), IID_PPV_ARGS(&sortedSignatures[index]));
                if (FAILED(result))
                {
                    LOG_ERROR("[ParticleEffect] Failed to create the sorted indirect draw signature (HRESULT: 0x{:08X}).",
                        static_cast<unsigned long>(result));
                    return false;
                }
            }
        }

        DX12GraphicsPipelineConfig forwardConfig = config;
        forwardConfig.renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        DX12GraphicsPipelineConfig deferredConfig = config;
        deferredConfig.renderTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        DX12GraphicsPipelineConfig forwardMeshConfig = meshConfig;
        forwardMeshConfig.renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        DX12GraphicsPipelineConfig deferredMeshConfig = meshConfig;
        deferredMeshConfig.renderTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        DX12GraphicsPipelineConfig forwardGpuConfig = gpuConfig;
        forwardGpuConfig.renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        DX12GraphicsPipelineConfig deferredGpuConfig = gpuConfig;
        deferredGpuConfig.renderTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        for (std::size_t index = 0; index < BLEND_MODE_COUNT; ++index)
        {
            const auto mode = static_cast<ParticleBlendMode>(index);
            forwardConfig.enableAdditiveBlend = mode == ParticleBlendMode::Additive;
            forwardConfig.overrideBlend = mode >= ParticleBlendMode::PremultipliedAlpha;
            if (forwardConfig.overrideBlend)
            {
                D3D12_RENDER_TARGET_BLEND_DESC blend{};
                blend.BlendEnable = TRUE;
                blend.SrcBlend = mode == ParticleBlendMode::Multiply ? D3D12_BLEND_DEST_COLOR : D3D12_BLEND_ONE;
                blend.DestBlend = mode == ParticleBlendMode::PremultipliedAlpha ? D3D12_BLEND_INV_SRC_ALPHA
                    : mode == ParticleBlendMode::Screen ? D3D12_BLEND_INV_SRC_COLOR : D3D12_BLEND_ZERO;
                blend.BlendOp = D3D12_BLEND_OP_ADD;
                blend.SrcBlendAlpha = mode == ParticleBlendMode::PremultipliedAlpha ? D3D12_BLEND_ONE : D3D12_BLEND_ZERO;
                blend.DestBlendAlpha = mode == ParticleBlendMode::PremultipliedAlpha ? D3D12_BLEND_ZERO : D3D12_BLEND_ONE;
                blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                blend.LogicOp = D3D12_LOGIC_OP_NOOP;
                blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                forwardConfig.blendOverride = blend;
            }
            deferredConfig.enableAdditiveBlend = forwardConfig.enableAdditiveBlend;
            deferredConfig.overrideBlend = forwardConfig.overrideBlend;
            deferredConfig.blendOverride = forwardConfig.blendOverride;
            forwardMeshConfig.enableAdditiveBlend = forwardConfig.enableAdditiveBlend;
            forwardMeshConfig.overrideBlend = forwardConfig.overrideBlend;
            forwardMeshConfig.blendOverride = forwardConfig.blendOverride;
            deferredMeshConfig.enableAdditiveBlend = deferredConfig.enableAdditiveBlend;
            deferredMeshConfig.overrideBlend = deferredConfig.overrideBlend;
            deferredMeshConfig.blendOverride = deferredConfig.blendOverride;
            forwardGpuConfig.enableAdditiveBlend = forwardConfig.enableAdditiveBlend;
            forwardGpuConfig.overrideBlend = forwardConfig.overrideBlend;
            forwardGpuConfig.blendOverride = forwardConfig.blendOverride;
            deferredGpuConfig.enableAdditiveBlend = deferredConfig.enableAdditiveBlend;
            deferredGpuConfig.overrideBlend = deferredConfig.overrideBlend;
            deferredGpuConfig.blendOverride = deferredConfig.blendOverride;
            if (!forward[index].initialize(*m_device->get(), forwardConfig)
                || !deferred[index].initialize(*m_device->get(), deferredConfig)
                || !forwardMesh[index].initialize(*m_device->get(), forwardMeshConfig)
                || !deferredMesh[index].initialize(*m_device->get(), deferredMeshConfig)
                || !forwardGpu[index].initialize(*m_device->get(), forwardGpuConfig)
                || !deferredGpu[index].initialize(*m_device->get(), deferredGpuConfig))
            {
                LOG_ERROR("[ParticleEffect] Failed to build particle graphics pipelines for blend mode {}.", index);
                return false;
            }
        }
        for (std::size_t index = 0; index < BLEND_MODE_COUNT; ++index)
        {
            m_forwardPipelines[index].swap(forward[index]);
            m_deferredPipelines[index].swap(deferred[index]);
            m_forwardMeshPipelines[index].swap(forwardMesh[index]);
            m_deferredMeshPipelines[index].swap(deferredMesh[index]);
            m_forwardGpuPipelines[index].swap(forwardGpu[index]);
            m_deferredGpuPipelines[index].swap(deferredGpu[index]);
        }
        m_gpuSimulationPipeline.swap(gpuSimulation);
        m_gpuCompactionPipeline.swap(gpuCompaction);
        m_sortPipeline.swap(sortPipeline);
        for (std::size_t index = 0; index < m_sortedPipelines.size(); ++index)
            m_sortedPipelines[index].swap(sortedPipelines[index]);
        m_sortedSignatures.swap(sortedSignatures);
        ParticleRenderSubmissionQueue::instance().setGpuSortingSupported(supportsSorting);
        return true;
    }

    bool ParticleRenderer::record(const std::uint32_t frameIndex, const Matrix& viewProjection,
        const Matrix& inverseView, const Vector3& cameraPosition, const Vector3& cameraForward,
        const D3D12_CPU_DESCRIPTOR_HANDLE colorTarget, const D3D12_CPU_DESCRIPTOR_HANDLE depthTarget,
        const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor, const DXGI_FORMAT targetFormat,
        const bool enabled, DX12GpuProfiler* const profiler, const std::uint64_t profileFrameId,
        const ParticleSceneDepth* const sceneDepth, DX12Resource* const sceneColor)
    {
        if (frameIndex >= FRAME_COUNT)
        {
            LOG_ERROR("[ParticleEffect] Invalid frame index {}.", frameIndex);
            return false;
        }

        m_frameRecorded[frameIndex] = false;
        m_frameUsesQuadInstanceBuffer[frameIndex] = false;
        m_frameUsesMeshInstanceBuffer[frameIndex] = false;
        m_frameUsesSort[frameIndex] = false;
        m_usedMeshes[frameIndex].clear();
        m_frameGpuEmitterIds[frameIndex].clear();
        m_gpuEmitterDrawCount = 0;
        m_renderedParticles = 0;
        ++m_frameNumber;
        auto& submissions = ParticleRenderSubmissionQueue::instance();
        submissions.consume(m_particles, m_trails, m_ribbons,
            m_gpuEmitterSubmissions, m_removedGpuEmitters);
        const bool supportsSorting = m_sortedSignatures[0] != nullptr;
        const bool sorted = enabled && supportsSorting && !m_gpuEmitterSubmissions.empty();
        for (const std::uint64_t id : m_removedGpuEmitters)
        {
            const auto state = m_gpuEmitterStates.find(id);
            if (state != m_gpuEmitterStates.end())
                state->second->retired = true;
        }
        for (const ParticleGpuEmitterSubmission& emitter : m_gpuEmitterSubmissions)
        {
            if (submissions.gpuSimulationFallbackReason(emitter.settings) != nullptr
                || emitter.settings.maxParticles > MAX_GPU_PARTICLES)
            {
                LOG_ERROR("[ParticleEffect] Unsupported settings reached the GPU particle renderer.");
                return false;
            }
            auto state = m_gpuEmitterStates.find(emitter.id);
            if (state == m_gpuEmitterStates.end())
            {
                try
                {
                    auto created = std::make_unique<GpuEmitterState>();
                    state = m_gpuEmitterStates.emplace(emitter.id, std::move(created)).first;
                }
                catch (const std::bad_alloc&)
                {
                    LOG_ERROR("[ParticleEffect] Failed to allocate GPU emitter state.");
                    return false;
                }
                catch (const std::length_error&)
                {
                    LOG_ERROR("[ParticleEffect] GPU emitter state map exceeds capacity.");
                    return false;
                }
            }
            state->second->retired = false;
        }
        if (!enabled)
        {
            m_particles.clear();
            m_trails.clear();
            m_ribbons.clear();
        }
        for (auto state = m_gpuEmitterStates.begin(); state != m_gpuEmitterStates.end();)
        {
            auto& retired = state->second->retiredBuffers;
            std::erase_if(retired, [this](const RetiredGpuBuffer& buffer)
            {
                return buffer.fenceValue == 0 || m_fence == nullptr || m_fence->isComplete(buffer.fenceValue);
            });
            if (state->second->retired
                && (state->second->lastFenceValue == 0 || m_fence == nullptr
                    || m_fence->isComplete(state->second->lastFenceValue))
                && retired.empty())
            {
                state = m_gpuEmitterStates.erase(state);
            }
            else
                ++state;
        }
        if (m_particles.empty() && m_trails.empty() && m_ribbons.empty() && m_gpuEmitterSubmissions.empty())
            return true;

        if (!isFinite(viewProjection) || !isFinite(cameraPosition) || !isFinite(cameraForward))
        {
            LOG_ERROR("[ParticleEffect] Camera data is invalid for particle rendering.");
            return false;
        }
        const std::size_t totalPrimitives = m_particles.size() + m_trails.size() + m_ribbons.size();
        if (m_particles.size() > (std::numeric_limits<std::size_t>::max)() / 6
            || m_trails.size() > (std::numeric_limits<std::size_t>::max)() / 6)
        {
            LOG_ERROR("[ParticleEffect] Particle snapshot exceeds DirectX 12 draw limits.");
            return false;
        }
        std::size_t minimumVertexCount = (m_particles.size() + m_trails.size()) * 6;
        for (const ParticleRenderRibbon& ribbon : m_ribbons)
        {
            if (ribbon.points.size() < 2
                || ribbon.points.size() - 1 > ((std::numeric_limits<std::size_t>::max)() - minimumVertexCount) / 6)
            {
                LOG_ERROR("[ParticleEffect] Ribbon snapshot exceeds DirectX 12 draw limits.");
                return false;
            }
            minimumVertexCount += (ribbon.points.size() - 1) * 6;
        }
        if (minimumVertexCount > (std::numeric_limits<UINT>::max)())
        {
            LOG_ERROR("[ParticleEffect] Particle snapshot exceeds DirectX 12 draw limits.");
            return false;
        }
        for (const ParticleGpuEmitterSubmission& emitter : m_gpuEmitterSubmissions)
        {
            GpuEmitterState& state = *m_gpuEmitterStates.at(emitter.id);
            if (state.particleLimit != emitter.settings.maxParticles)
            {
                state.particleLimit = emitter.settings.maxParticles;
                state.needsClear = true;
            }
            if (!ensureGpuEmitterBuffers(state, emitter.settings.maxParticles)
                || (emitter.settings.trailEnabled && !ensureGpuTrailBuffer(state)))
                return false;
        }
        for (const ParticleRenderParticle& particle : m_particles)
            if (!std::isfinite(particle.position.x) || !std::isfinite(particle.position.y)
                || !std::isfinite(particle.position.z) || !std::isfinite(particle.size)
                || particle.size <= 0.0f || !std::isfinite(particle.rotation)
                || !std::isfinite(particle.sizeScale.x) || particle.sizeScale.x <= 0.0f
                || !std::isfinite(particle.sizeScale.y) || particle.sizeScale.y <= 0.0f
                || !std::isfinite(particle.color.x) || !std::isfinite(particle.color.y)
                || !std::isfinite(particle.color.z) || !std::isfinite(particle.color.w))
            {
                LOG_ERROR("[ParticleEffect] Particle snapshot contains invalid render data.");
                return false;
            }
        if (targetFormat != DXGI_FORMAT_R16G16B16A16_FLOAT
            && targetFormat != DXGI_FORMAT_R8G8B8A8_UNORM)
        {
            LOG_ERROR("[ParticleEffect] Unsupported particle render target format {}.", static_cast<int>(targetFormat));
            return false;
        }

        const float cameraForwardLengthSquared = cameraForward.LengthSquared();
        if (!std::isfinite(cameraForwardLengthSquared) || cameraForwardLengthSquared < 1e-8f)
        {
            LOG_ERROR("[ParticleEffect] Camera forward vector is invalid for particle sorting.");
            return false;
        }
        const Vector3 normalizedCameraForward = cameraForward / std::sqrt(cameraForwardLengthSquared);
        Vector3 right = Vector3::TransformNormal(Vector3::UnitX, inverseView);
        Vector3 up = Vector3::TransformNormal(Vector3::UnitY, inverseView);
        if (!std::isfinite(right.LengthSquared()) || right.LengthSquared() < 1e-8f
            || !std::isfinite(up.LengthSquared()) || up.LengthSquared() < 1e-8f)
        {
            LOG_ERROR("[ParticleEffect] Camera basis is invalid for billboard rendering.");
            return false;
        }
        right.Normalize();
        up -= right * right.Dot(up);
        if (up.LengthSquared() < 1e-8f)
        {
            LOG_ERROR("[ParticleEffect] Camera billboard basis is degenerate.");
            return false;
        }
        up.Normalize();

        try
        {
            m_drawItems.resize(totalPrimitives);
            for (std::size_t index = 0; index < m_particles.size(); ++index)
            {
                const float depth = (m_particles[index].position - cameraPosition).Dot(normalizedCameraForward);
                if (!std::isfinite(depth))
                {
                    LOG_ERROR("[ParticleEffect] Particle depth is invalid for sorting.");
                    return false;
                }
                m_drawItems[index] = { index, depth, DrawItem::Kind::Particle };
            }
            for (std::size_t index = 0; index < m_trails.size(); ++index)
            {
                const auto& trail = m_trails[index];
                const Vector3 midpoint = trail.start * 0.5f + trail.end * 0.5f;
                const float depth = (midpoint - cameraPosition).Dot(normalizedCameraForward);
                if (!std::isfinite(depth))
                {
                    LOG_ERROR("[ParticleEffect] Trail depth is invalid for sorting.");
                    return false;
                }
                m_drawItems[m_particles.size() + index] = { index, depth, DrawItem::Kind::Trail };
            }
            for (std::size_t index = 0; index < m_ribbons.size(); ++index)
            {
                const auto& ribbon = m_ribbons[index];
                Vector3 center = Vector3::Zero;
                for (std::size_t pointIndex = 0; pointIndex < ribbon.points.size(); ++pointIndex)
                    center += (ribbon.points[pointIndex].position - center)
                        / static_cast<float>(pointIndex + 1);
                const float depth = (center - cameraPosition).Dot(normalizedCameraForward);
                if (!std::isfinite(depth))
                {
                    LOG_ERROR("[ParticleEffect] Ribbon depth is invalid for sorting.");
                    return false;
                }
                m_drawItems[m_particles.size() + m_trails.size() + index] =
                    { index, depth, DrawItem::Kind::Ribbon };
            }
            if (!sorted)
                std::sort(m_drawItems.begin(), m_drawItems.end(),
                    [](const DrawItem& left, const DrawItem& rightItem) { return left.depth > rightItem.depth; });
            m_quadInstances.clear();
            m_quadInstances.reserve(std::min(totalPrimitives, MAX_QUAD_INSTANCES));
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ParticleEffect] Failed to allocate draw items or quad instances.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ParticleEffect] Draw item or quad instance count exceeds the maximum capacity.");
            return false;
        }

        auto& textureManager = TextureManager::instance();
        const Texture* const white = textureManager.get(textureManager.getWhiteTexture());
        const TextureResourceInfo* const whiteInfo = white ? white->getResourceInfo() : nullptr;
        if (whiteInfo == nullptr)
        {
            LOG_ERROR("[ParticleEffect] Default particle texture is unavailable.");
            return false;
        }
        m_batches.clear();
        m_resolvedVisuals.clear();
        m_meshInstances.clear();
        const auto appendBand = [this, sorted, texture = whiteInfo->srv](const ParticleBlendMode blendMode, const float depth,
            const Vector3& start, const Vector3& end, const Vector3& startSide, const Vector3& endSide,
            const Vector4& startColor, const Vector4& endColor)
        {
            constexpr std::uint32_t bandAppearance = 2;
            if (m_batches.empty() || m_batches.back().mesh != nullptr
                || m_batches.back().appearance != bandAppearance
                || m_batches.back().blendMode != blendMode || (sorted && m_batches.back().depth != depth))
            {
                m_batches.push_back({ .texture = texture, .blendMode = blendMode,
                    .appearance = bandAppearance, .firstInstance = static_cast<std::uint32_t>(m_quadInstances.size()),
                    .depth = depth });
            }
            m_quadInstances.push_back({ .origin = start, .isBand = 1, .rightOrEnd = end,
                .upOrStartSide = startSide, .endSide = endSide, .color = startColor, .endColor = endColor });
            ++m_batches.back().instanceCount;
        };
        m_renderedParticles = 0;
        m_renderedTrails = 0;
        m_renderedRibbons = 0;
        try
        {
            for (const DrawItem& draw : m_drawItems)
            {
                if (draw.kind == DrawItem::Kind::Trail)
                {
                    if (m_quadInstances.size() >= MAX_QUAD_INSTANCES)
                    {
                        LOG_ERROR("[ParticleEffect] Particle vertex budget of {} was exceeded.", MAX_RENDER_VERTICES);
                        return false;
                    }
                    const ParticleRenderTrailSegment& trail = m_trails[draw.index];
                    const Vector3 startTangent = trail.end - trail.previous;
                    const Vector3 endTangent = trail.next - trail.start;
                    if (!isFinite(startTangent) || !isFinite(endTangent))
                    {
                        LOG_ERROR("[ParticleEffect] Trail tangent is invalid.");
                        return false;
                    }
                    const auto sideFor = [&normalizedCameraForward, &right](const Vector3& tangent)
                    {
                        const float scale = std::max({ std::abs(tangent.x), std::abs(tangent.y), std::abs(tangent.z) });
                        if (scale < 1e-10f)
                            return right;
                        Vector3 side = (tangent / scale).Cross(normalizedCameraForward);
                        const float lengthSquared = side.LengthSquared();
                        if (lengthSquared < 1e-10f)
                            return right;
                        return side / std::sqrt(lengthSquared);
                    };
                    const Vector3 startSide = sideFor(startTangent) * (trail.startWidth * 0.5f);
                    const Vector3 endSide = sideFor(endTangent) * (trail.endWidth * 0.5f);
                    if (!isFinite(startSide) || !isFinite(endSide))
                    {
                        LOG_ERROR("[ParticleEffect] Trail side vector is invalid.");
                        return false;
                    }
                    appendBand(trail.blendMode, draw.depth, trail.start, trail.end, startSide, endSide,
                        trail.startColor, trail.endColor);
                    ++m_renderedTrails;
                    continue;
                }
                if (draw.kind == DrawItem::Kind::Ribbon)
                {
                    const ParticleRenderRibbon& ribbon = m_ribbons[draw.index];
                    const std::size_t segmentCount = ribbon.points.size() - 1;
                    if (segmentCount > MAX_QUAD_INSTANCES - m_quadInstances.size())
                    {
                        LOG_ERROR("[ParticleEffect] Ribbon vertex budget of {} was exceeded.", MAX_RENDER_VERTICES);
                        return false;
                    }
                    std::array<Vector3, 256> sides{};
                    for (std::size_t index = 0; index < ribbon.points.size(); ++index)
                    {
                        const std::size_t previous = index == 0 ? 0 : index - 1;
                        const std::size_t next = std::min(index + 1, ribbon.points.size() - 1);
                        const Vector3 tangent = ribbon.points[next].position - ribbon.points[previous].position;
                        if (!isFinite(tangent))
                        {
                            LOG_ERROR("[ParticleEffect] Ribbon tangent is invalid.");
                            return false;
                        }
                        Vector3 side = tangent.Cross(normalizedCameraForward);
                        const float sideLengthSquared = side.LengthSquared();
                        if (!std::isfinite(sideLengthSquared))
                        {
                            LOG_ERROR("[ParticleEffect] Ribbon side vector is invalid.");
                            return false;
                        }
                        if (sideLengthSquared < 1e-10f)
                            side = right;
                        else
                            side /= std::sqrt(sideLengthSquared);
                        sides[index] = side * (ribbon.points[index].width * 0.5f);
                    }
                    for (std::size_t segment = 0; segment < segmentCount; ++segment)
                    {
                        const auto& start = ribbon.points[segment];
                        const auto& end = ribbon.points[segment + 1];
                        appendBand(ribbon.blendMode, draw.depth, start.position, end.position,
                            sides[segment], sides[segment + 1], start.color, end.color);
                    }
                    ++m_renderedRibbons;
                    continue;
                }
                const ParticleRenderParticle& particle = m_particles[draw.index];
                const ParticleRenderVisual* const visual = particle.visual.get();
                auto resolved = std::find_if(m_resolvedVisuals.begin(), m_resolvedVisuals.end(),
                    [visual](const ResolvedVisual& item) { return item.visual == visual; });
                if (resolved == m_resolvedVisuals.end())
                {
                    ResolvedVisual item{ visual, whiteInfo->srv, 1, 1, false, true };
                    item.auxiliaryTextures.fill(whiteInfo->srv);
                    if (visual)
                    {
                        if (!visual->meshPath.empty())
                        {
                            item.mesh = resolveMesh(visual->meshPath);
                            item.valid = item.mesh != nullptr;
                        }
                        item.textured = !visual->texturePath.empty();
                        const std::array paths{ &visual->secondaryTexturePath, &visual->maskTexturePath,
                            &visual->distortionTexturePath };
                        const bool primaryValid = resolveTexture(visual->texturePath, TextureColorSpace::SRGB,
                            item.texture, &item.width, &item.height);
                        item.valid = primaryValid && item.valid;
                        for (std::size_t index = 0; index < paths.size(); ++index)
                        {
                            if (!paths[index]->empty())
                                item.textureFlags |= 1u << index;
                            const bool valid = resolveTexture(*paths[index],
                                index == 0 ? TextureColorSpace::SRGB : TextureColorSpace::Linear,
                                item.auxiliaryTextures[index], nullptr, nullptr);
                            item.valid = valid && item.valid;
                        }
                    }
                    m_resolvedVisuals.push_back(item);
                    resolved = std::prev(m_resolvedVisuals.end());
                }
                if (!resolved->valid)
                    continue;
                const ParticleBlendMode blendMode = visual ? visual->blendMode : ParticleBlendMode::Alpha;
                const std::uint32_t appearance = resolved->textured ? 1u : (resolved->mesh ? 2u : 0u);
                const float softDistance = visual ? visual->softDistance : 0.0f;
                const float uvDistortion = visual ? visual->uvDistortion : 0.0f;
                const float refractionPixels = visual ? visual->refractionPixels : 0.0f;
                if (m_batches.empty() || (sorted && m_batches.back().depth != draw.depth)
                    || m_batches.back().texture.ptr != resolved->texture.ptr
                    || m_batches.back().blendMode != blendMode || m_batches.back().appearance != appearance
                    || m_batches.back().softDistance != softDistance
                    || m_batches.back().textureFlags != resolved->textureFlags
                    || m_batches.back().uvDistortion != uvDistortion
                    || m_batches.back().refractionPixels != refractionPixels
                    || m_batches.back().mesh != resolved->mesh
                    || !std::equal(m_batches.back().auxiliaryTextures.begin(),
                        m_batches.back().auxiliaryTextures.end(), resolved->auxiliaryTextures.begin(),
                        [](auto a, auto b) { return a.ptr == b.ptr; }))
                {
                    DrawBatch batch{ .texture = resolved->texture, .blendMode = blendMode,
                        .appearance = appearance, .softDistance = softDistance,
                        .firstInstance = static_cast<std::uint32_t>(m_quadInstances.size()), .depth = draw.depth };
                    batch.auxiliaryTextures = resolved->auxiliaryTextures;
                    batch.textureFlags = resolved->textureFlags;
                    batch.uvDistortion = uvDistortion;
                    batch.refractionPixels = refractionPixels;
                    batch.mesh = resolved->mesh;
                    if (batch.mesh != nullptr)
                    {
                        if (batch.mesh->vertices.size() > (std::numeric_limits<std::uint32_t>::max)())
                        {
                            LOG_ERROR("[ParticleEffect] Mesh vertex count exceeds DirectX 12 draw limits.");
                            return false;
                        }
                        batch.vertexCount = static_cast<std::uint32_t>(batch.mesh->vertices.size());
                        batch.firstInstance = static_cast<std::uint32_t>(m_meshInstances.size());
                    }
                    m_batches.push_back(batch);
                }
                Vector3 particleRight = right;
                Vector3 particleUp = up;
                float heightScale = 1.0f;
                if (visual && (visual->orientation == ParticleOrientation::Velocity
                    || visual->orientation == ParticleOrientation::Stretched))
                {
                    const Vector3 projected = particle.velocity
                        - normalizedCameraForward * particle.velocity.Dot(normalizedCameraForward);
                    const float lengthSquared = projected.LengthSquared();
                    if (!std::isfinite(lengthSquared))
                    {
                        LOG_ERROR("[ParticleEffect] Particle orientation velocity is invalid.");
                        return false;
                    }
                    if (lengthSquared > 1e-8f)
                    {
                        particleUp = projected / std::sqrt(lengthSquared);
                        particleRight = particleUp.Cross(normalizedCameraForward);
                        particleRight.Normalize();
                    }
                    if (visual->orientation == ParticleOrientation::Stretched)
                        heightScale += particle.velocity.Length() * visual->stretchBySpeed;
                }
                else if (visual && visual->orientation == ParticleOrientation::Axis)
                {
                    particleUp = visual->orientationDirection;
                    particleUp.Normalize();
                    particleRight = particleUp.Cross(normalizedCameraForward);
                    if (particleRight.LengthSquared() < 1e-8f)
                        particleRight = particleUp.Cross(right);
                    particleRight.Normalize();
                }
                else if (visual && (visual->orientation == ParticleOrientation::Horizontal
                    || visual->orientation == ParticleOrientation::Direction))
                {
                    Vector3 normal = visual->orientation == ParticleOrientation::Horizontal
                        ? Vector3::Up : visual->orientationDirection;
                    normal.Normalize();
                    const Vector3 reference = std::abs(normal.Dot(Vector3::Up)) > 0.99f
                        ? Vector3::UnitZ : Vector3::Up;
                    particleRight = reference.Cross(normal);
                    particleRight.Normalize();
                    particleUp = normal.Cross(particleRight);
                    particleUp.Normalize();
                }
                if (!std::isfinite(heightScale))
                {
                    LOG_ERROR("[ParticleEffect] Particle stretch is invalid.");
                    return false;
                }
                const float cosine = std::cos(particle.rotation);
                const float sine = std::sin(particle.rotation);
                const Vector3 rotatedRight = (particleRight * cosine + particleUp * sine)
                    * (particle.size * particle.sizeScale.x * 0.5f);
                const Vector3 rotatedUp = (particleUp * cosine - particleRight * sine)
                    * (particle.size * particle.sizeScale.y * heightScale * 0.5f);
                if (!isFinite(rotatedRight) || !isFinite(rotatedUp))
                {
                    LOG_ERROR("[ParticleEffect] Particle quad basis is invalid.");
                    return false;
                }
                Vector2 uvMin = resolved->textured ? particle.uvMin : Vector2::Zero;
                Vector2 uvMax = resolved->textured ? particle.uvMax : Vector2::One;
                if (resolved->textured)
                {
                    const Vector2 halfTexel(0.5f / static_cast<float>(resolved->width),
                        0.5f / static_cast<float>(resolved->height));
                    const Vector2 inset(std::min(halfTexel.x, (uvMax.x - uvMin.x) * 0.5f),
                        std::min(halfTexel.y, (uvMax.y - uvMin.y) * 0.5f));
                    uvMin += inset;
                    uvMax -= inset;
                }
                const Vector4 bounds(uvMin.x, uvMin.y, uvMax.x, uvMax.y);
                const Vector4 nextBounds(particle.nextUvMin.x, particle.nextUvMin.y,
                    particle.nextUvMax.x, particle.nextUvMax.y);
                if (resolved->mesh)
                {
                    if (resolved->mesh->vertexBuffer == nullptr
                        || m_meshInstances.size() >= ParticleRenderSubmissionQueue::MAX_RENDER_PARTICLES
                        || m_batches.back().instanceCount == (std::numeric_limits<std::uint32_t>::max)())
                    {
                        LOG_ERROR("[ParticleEffect] Mesh particle instance data is unavailable or exceeds its limit.");
                        return false;
                    }
                    Vector3 normal = particleRight.Cross(particleUp);
                    normal.Normalize();
                    MeshInstance instance{ particle.position, rotatedRight * 2.0f, rotatedUp * 2.0f,
                        normal * particle.size, particle.color, bounds, particle.uvOffset, nextBounds,
                        particle.frameBlend, 0.0f };
                    if (!isFinite(instance.position) || !isFinite(instance.right) || !isFinite(instance.up)
                        || !isFinite(instance.normal) || !isFinite(instance.color)
                        || !std::isfinite(instance.frameBlend))
                    {
                        LOG_ERROR("[ParticleEffect] Mesh particle instance contains invalid transform data.");
                        return false;
                    }
                    m_meshInstances.push_back(instance);
                    ++m_batches.back().instanceCount;
                    if (std::find(m_usedMeshes[frameIndex].begin(), m_usedMeshes[frameIndex].end(),
                        resolved->mesh) == m_usedMeshes[frameIndex].end())
                        m_usedMeshes[frameIndex].push_back(resolved->mesh);
                    ++m_renderedParticles;
                    continue;
                }
                if (m_quadInstances.size() >= MAX_QUAD_INSTANCES)
                {
                    LOG_ERROR("[ParticleEffect] Particle vertex budget of {} was exceeded.", MAX_RENDER_VERTICES);
                    return false;
                }
                m_quadInstances.push_back({ .origin = particle.position, .rightOrEnd = rotatedRight,
                    .frameBlend = particle.frameBlend, .upOrStartSide = rotatedUp,
                    .uvOffset = particle.uvOffset, .color = particle.color,
                    .uvBounds = bounds, .nextUvBounds = nextBounds });
                ++m_batches.back().instanceCount;
                ++m_renderedParticles;
            }
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ParticleEffect] Failed to allocate particle draw batches.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ParticleEffect] Particle draw batches exceed the maximum capacity.");
            return false;
        }
        if (m_quadInstances.empty() && m_meshInstances.empty() && m_gpuEmitterSubmissions.empty())
            return true;

        const bool usesSoftParticles = std::any_of(m_batches.begin(), m_batches.end(),
            [](const DrawBatch& batch) { return batch.softDistance > 0.0f; })
            || (sorted && std::any_of(m_gpuEmitterSubmissions.begin(), m_gpuEmitterSubmissions.end(),
                [](const auto& emitter) { return emitter.settings.softDistance > 0.0f; }));
        const bool usesRefraction = std::any_of(m_batches.begin(), m_batches.end(),
            [](const DrawBatch& batch) { return batch.refractionPixels != 0.0f; })
            || (sorted && std::any_of(m_gpuEmitterSubmissions.begin(), m_gpuEmitterSubmissions.end(),
                [](const auto& emitter) { return emitter.settings.refractionPixels != 0.0f; }));
        if (usesSoftParticles && (!sceneDepth || !sceneDepth->resource || !sceneDepth->resource->get()
            || sceneDepth->resource->getCurrentState() != D3D12_RESOURCE_STATE_DEPTH_WRITE
            || sceneDepth->readOnlyDsv.ptr == 0 || sceneDepth->srv.ptr == 0
            || !isFinite(sceneDepth->inverseViewProjection)
            || !std::isfinite(viewport.TopLeftX) || !std::isfinite(viewport.TopLeftY)
            || !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height)
            || viewport.Width <= 0.0f || viewport.Height <= 0.0f
            || !std::isfinite(viewport.MinDepth) || !std::isfinite(viewport.MaxDepth)
            || viewport.MaxDepth <= viewport.MinDepth))
        {
            LOG_ERROR("[ParticleEffect] Soft particles require a valid scene depth resource and camera.");
            return false;
        }
        if (usesRefraction && (!sceneColor || !sceneColor->get()
            || sceneColor->getCurrentState() != D3D12_RESOURCE_STATE_RENDER_TARGET
            || sceneColor->getDescription().Format != targetFormat
            || !std::isfinite(viewport.TopLeftX) || !std::isfinite(viewport.TopLeftY)
            || !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height)
            || viewport.Width < 1.0f || viewport.Height < 1.0f))
        {
            LOG_ERROR("[ParticleEffect] Refraction requires a valid scene color target.");
            return false;
        }
        if ((!m_quadInstances.empty() && !ensureQuadInstanceCapacity(frameIndex, m_quadInstances.size()))
            || (!m_meshInstances.empty() && !ensureMeshInstanceCapacity(frameIndex, m_meshInstances.size())))
            return false;
        try
        {
            m_frameGpuEmitterIds[frameIndex].reserve(m_gpuEmitterSubmissions.size());
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ParticleEffect] Failed to retain GPU emitter frame references.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ParticleEffect] GPU emitter frame references exceed vector capacity.");
            return false;
        }
        DX12CommandList& commandList = m_commandLists[frameIndex];
        DX12UploadBuffer& quadInstanceBuffer = m_quadInstanceBuffers[frameIndex];
        const auto quadInstanceBytes = std::as_bytes(std::span<const QuadInstance>(m_quadInstances));
        const auto meshInstanceBytes = std::as_bytes(std::span<const MeshInstance>(m_meshInstances));
        if (!commandList.begin(*m_fence)
            || (!quadInstanceBytes.empty() && !quadInstanceBuffer.write(quadInstanceBytes))
            || (!meshInstanceBytes.empty() && !m_meshInstanceBuffers[frameIndex].write(meshInstanceBytes)))
        {
            LOG_ERROR("[ParticleEffect] Failed to prepare particle draw resources.");
            return false;
        }
        m_frameUsesQuadInstanceBuffer[frameIndex] = !quadInstanceBytes.empty();
        m_frameUsesMeshInstanceBuffer[frameIndex] = !meshInstanceBytes.empty();
        if (usesRefraction && !ensureSceneCopy(frameIndex, *sceneColor))
            return false;
        if (profiler != nullptr && !profiler->beginFrame(frameIndex, profileFrameId, commandList))
            return false;

        ID3D12GraphicsCommandList* const native = commandList.getForRecording();
        ID3D12DescriptorHeap* const heap = textureManager.getDescriptorHeap().get();
        if (native == nullptr || heap == nullptr)
        {
            LOG_ERROR("[ParticleEffect] Failed to bind particle pipeline.");
            return false;
        }

        if (usesRefraction)
        {
            D3D12_RESOURCE_BARRIER barriers[2]{};
            for (auto& barrier : barriers)
            {
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            }
            barriers[0].Transition.pResource = sceneColor->get();
            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barriers[1].Transition.pResource = m_sceneCopies[frameIndex].get();
            barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            native->ResourceBarrier(2, barriers);
            native->CopyResource(m_sceneCopies[frameIndex].get(), sceneColor->get());
            for (auto& barrier : barriers)
                std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            native->ResourceBarrier(2, barriers);
        }
        D3D12_RESOURCE_BARRIER depthBarrier{};
        if (usesSoftParticles)
        {
            depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            depthBarrier.Transition.pResource = sceneDepth->resource->get();
            depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            depthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            native->ResourceBarrier(1, &depthBarrier);
        }
        const auto activeDepthTarget = usesSoftParticles ? sceneDepth->readOnlyDsv : depthTarget;
        native->OMSetRenderTargets(1, &colorTarget, FALSE, &activeDepthTarget);
        native->RSSetViewports(1, &viewport);
        native->RSSetScissorRects(1, &scissor);
        native->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        native->SetDescriptorHeaps(1, &heap);
        struct CurveRecordingGuard
        {
            std::span<const ParticleGpuEmitterSubmission> submissions;
            decltype(m_gpuEmitterStates)& states;
            bool committed = false;

            ~CurveRecordingGuard()
            {
                if (!committed)
                    for (const auto& emitter : submissions)
                    {
                        auto& state = *states.find(emitter.id)->second;
                        state.needsClear = true;
                        state.needsSchedulerClear = true;
                    }
            }
        } curveRecording{ m_gpuEmitterSubmissions, m_gpuEmitterStates };
        if (!m_gpuEmitterSubmissions.empty())
        {
            if (!prepareGpuCurves(frameIndex) || !m_gpuSimulationPipeline.bind(commandList))
                return false;
            auto& settingsUpload = m_gpuSimulationSettings[frameIndex];
            constexpr auto alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
            const auto required = std::bit_ceil(static_cast<std::uint64_t>(m_gpuEmitterSubmissions.size()) * alignment);
            if (settingsUpload.getSize() < required && (!settingsUpload.finalize()
                || !settingsUpload.initialize(*m_device->get(), *m_fence, required)))
                return false;
            std::uint64_t settingsOffset = 0;
            std::size_t emitterIndex = 0;
            for (const ParticleGpuEmitterSubmission& emitter : m_gpuEmitterSubmissions)
            {
                GpuEmitterState& state = *m_gpuEmitterStates.at(emitter.id);
                auto& particles = state.buffers->particles;
                auto& trails = state.buffers->trails;
                auto& scheduler = state.scheduler;
                if (!particles.transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
                    return false;
                const auto& window = emitter.emission;
                const auto phase = splitEmissionTime(window.phase);
                const auto endPhase = splitEmissionTime(window.endPhase);
                const auto& settings = emitter.settings;
                const auto wraps = static_cast<std::uint32_t>(std::min(window.wraps, GPU_MAX_CYCLE_WRAPS));
                const GpuSimulationSettings gpuSettings{ emitter.worldMatrix,
                    Vector4(emitter.settings.trailLifetime, emitter.settings.trailWidth, emitter.settings.trailMinDistance,
                        static_cast<float>(emitter.settings.blendMode)),
                    state.birthCurveVersion, 1u + static_cast<std::uint32_t>(emitter.settings.curveVariants.size()),
                    emitter.settings.initialSizeScale,
                    Vector4(window.activeTime, settings.emissionRate, settings.duration,
                        static_cast<float>(window.completeCycleTime)),
                    Vector4(phase.x, phase.y, endPhase.x, endPhase.y),
                    splitEmissionTime(window.delay),
                    wraps != 0 ? static_cast<float>(settings.duration - window.phase) : window.activeTime,
                    wraps,
                    { static_cast<std::uint32_t>(state.packedCurves.size() * sizeof(std::uint32_t)),
                        static_cast<std::uint32_t>(settings.emissionRateKeys.size()),
                        static_cast<std::uint32_t>(settings.bursts.size()),
                        window.initial ? (0x80000000u | settings.burstCount) : 0u },
                    emitter.motion.segmentStartOffset, emitter.motion.distance, emitter.motion.segmentDelta,
                    settings.distancePerParticle, emitter.motion.localVelocity, settings.inheritVelocity };
                if (!settingsUpload.write(std::as_bytes(std::span(&gpuSettings, 1)), settingsOffset)
                    || (trails.get() && !trails.transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)))
                    return false;
                GpuSimulationConstants constants{
                    { emitter.settings.maxParticles, (emitter.clear || state.needsClear) ? 1u : 0u,
                        (emitter.clear || state.needsSchedulerClear) ? 1u : 0u,
                        emitter.motion.resetDistance ? 1u : 0u },
                    Vector4(std::bit_cast<float>(emitter.spawnSeed), emitter.deltaTime,
                        emitter.settings.lifetimeMin, emitter.settings.lifetimeMax),
                    Vector4(emitter.settings.speedMin, emitter.settings.speedMax,
                        emitter.settings.shapeRadius,
                        emitter.settings.spreadAngleDegrees * (PARTICLE_PI / 180.0f)),
                    static_cast<std::uint32_t>(emitter.settings.shape),
                    emitter.settings.boxExtents,
                    Vector4(emitter.settings.direction.x, emitter.settings.direction.y,
                        emitter.settings.direction.z, 0.0f),
                    Vector4(emitter.settings.gravity.x, emitter.settings.gravity.y,
                        emitter.settings.gravity.z, emitter.settings.drag),
                    Vector4(emitter.settings.startSize, emitter.settings.endSize,
                        emitter.settings.initialSizeMin, emitter.settings.initialSizeMax),
                    emitter.settings.startColor,
                    emitter.settings.endColor,
                    emitter.settings.initialColorMin,
                    emitter.settings.initialColorMax,
                    Vector4(emitter.settings.initialRotationMin, emitter.settings.initialRotationMax,
                        emitter.settings.angularVelocityMin, emitter.settings.angularVelocityMax),
                    { emitter.settings.randomInitial ? 1u : 0u, trails.get() ? 1u : 0u,
                        emitter.settings.trailEnabled ? 1u : 0u, state.needsTrailClear ? 1u : 0u },
                };
                native->SetComputeRoot32BitConstants(0, 52, &constants, 0);
                native->SetComputeRootUnorderedAccessView(1, particles.get()->GetGPUVirtualAddress());
                native->SetComputeRootUnorderedAccessView(2, (trails.get() ? trails.get() : particles.get())->GetGPUVirtualAddress());
                native->SetComputeRootConstantBufferView(3, settingsUpload.getGpuVirtualAddress() + settingsOffset);
                native->SetComputeRootShaderResourceView(4,
                    m_gpuCurveSettings[frameIndex].getGpuVirtualAddress() + m_curveUploadOffsets[emitterIndex++]);
                native->SetComputeRootUnorderedAccessView(5, scheduler.get()->GetGPUVirtualAddress());
                D3D12_RESOURCE_BARRIER schedulerBarrier{};
                schedulerBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                schedulerBarrier.UAV.pResource = scheduler.get();
                native->ResourceBarrier(1, &schedulerBarrier);
                constexpr std::uint32_t schedulerPass = 4;
                native->SetComputeRoot32BitConstant(0, constants.flags[3] | schedulerPass, 51);
                native->Dispatch(1, 1, 1);
                native->ResourceBarrier(1, &schedulerBarrier);
                native->SetComputeRoot32BitConstant(0, constants.flags[3], 51);
                if (trails.get())
                {
                    constexpr std::uint32_t ageTrailPass = 2;
                    native->SetComputeRoot32BitConstant(0, constants.flags[3] | ageTrailPass, 51);
                    native->Dispatch(1, 1, 1);
                    D3D12_RESOURCE_BARRIER trailBarrier{};
                    trailBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    trailBarrier.UAV.pResource = trails.get();
                    native->ResourceBarrier(1, &trailBarrier);
                    native->SetComputeRoot32BitConstant(0, constants.flags[3], 51);
                }
                native->Dispatch((emitter.settings.maxParticles + 63) / 64, 1, 1);
                if (!particles.transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                    || (trails.get() && !trails.transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)))
                    return false;
                settingsOffset += alignment;
                state.needsClear = false;
                state.needsSchedulerClear = false;
                state.needsTrailClear = false;
                m_frameGpuEmitterIds[frameIndex].push_back(emitter.id);
            }
            if (enabled)
            {
                if (!m_gpuCompactionPipeline.bind(commandList))
                    return false;
                for (const ParticleGpuEmitterSubmission& emitter : m_gpuEmitterSubmissions)
                {
                    GpuEmitterBuffers& buffers = *m_gpuEmitterStates.at(emitter.id)->buffers;
                    if (!buffers.drawData.transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
                        return false;
                    native->SetComputeRoot32BitConstant(0, emitter.settings.maxParticles, 0);
                    native->SetComputeRootShaderResourceView(1, buffers.particles.get()->GetGPUVirtualAddress());
                    native->SetComputeRootUnorderedAccessView(2, buffers.drawData.get()->GetGPUVirtualAddress());
                    native->Dispatch(1, 1, 1);
                    if (!buffers.drawData.transition(commandList, GPU_DRAW_DATA_READ_STATE))
                        return false;
                }
            }
        }
        struct DepthConstants
        {
            Matrix inverseViewProjection;
            Vector4 viewport;
            Vector4 depthRange;
            Vector4 forward;
        };
        const DepthConstants depthConstants{
            usesSoftParticles ? sceneDepth->inverseViewProjection : Matrix::Identity,
            Vector4(viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height),
            Vector4(viewport.MinDepth, viewport.MaxDepth, 0, 0),
            Vector4(normalizedCameraForward.x, normalizedCameraForward.y, normalizedCameraForward.z, 0),
        };
        static_assert(sizeof(DepthConstants) == 28 * sizeof(float));
        if (sorted)
        {
            const std::array camera{ Vector4(right.x, right.y, right.z, 0), Vector4(up.x, up.y, up.z, 0),
                depthConstants.forward, Vector4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 0) };
            if (!recordSorted(frameIndex, targetFormat, viewProjection, std::as_bytes(std::span(&depthConstants, 1)),
                camera, whiteInfo->srv, usesSoftParticles ? sceneDepth->srv : whiteInfo->srv,
                usesRefraction ? m_sceneCopyViews[frameIndex]->gpu->native : whiteInfo->srv))
                return false;
        }
        else
        {
            const DX12GraphicsPipeline* boundPipeline = nullptr;
            for (const DrawBatch& batch : m_batches)
            {
                const std::size_t blendIndex = static_cast<std::size_t>(batch.blendMode);
                const bool meshBatch = batch.mesh != nullptr;
                const DX12GraphicsPipeline* const pipeline = meshBatch
                    ? (targetFormat == DXGI_FORMAT_R16G16B16A16_FLOAT
                        ? &m_deferredMeshPipelines[blendIndex] : &m_forwardMeshPipelines[blendIndex])
                    : (targetFormat == DXGI_FORMAT_R16G16B16A16_FLOAT
                        ? &m_deferredPipelines[blendIndex] : &m_forwardPipelines[blendIndex]);
                if (pipeline != boundPipeline)
                {
                    if (!pipeline->bind(commandList))
                        return false;
                    native->SetGraphicsRoot32BitConstants(0, 16, &viewProjection._11, 0);
                    native->SetGraphicsRootDescriptorTable(3, usesSoftParticles ? sceneDepth->srv : whiteInfo->srv);
                    native->SetGraphicsRoot32BitConstants(4, 28, &depthConstants, 0);
                    native->SetGraphicsRootDescriptorTable(10, usesRefraction
                        ? m_sceneCopyViews[frameIndex]->gpu->native : whiteInfo->srv);
                    boundPipeline = pipeline;
                }
                native->SetGraphicsRootDescriptorTable(1, batch.texture);
                const std::uint32_t appearance = batch.appearance | (static_cast<std::uint32_t>(batch.blendMode) << 8)
                    | (batch.textureFlags << 16);
                native->SetGraphicsRoot32BitConstants(2, 1, &appearance, 0);
                native->SetGraphicsRoot32BitConstants(5, 1, &batch.softDistance, 0);
                for (std::size_t index = 0; index < batch.auxiliaryTextures.size(); ++index)
                    native->SetGraphicsRootDescriptorTable(static_cast<UINT>(6 + index),
                        batch.auxiliaryTextures[index].ptr ? batch.auxiliaryTextures[index] : whiteInfo->srv);
                native->SetGraphicsRoot32BitConstants(9, 1, &batch.uvDistortion, 0);
                native->SetGraphicsRoot32BitConstants(11, 1, &batch.refractionPixels, 0);
                if (meshBatch)
                {
                    if (!batch.mesh->vertexBuffer || batch.instanceCount == 0)
                    {
                        LOG_ERROR("[ParticleEffect] Mesh particle batch has no geometry or instances.");
                        return false;
                    }
                    const auto meshBytes = batch.mesh->vertices.size() * sizeof(MeshVertex);
                    const D3D12_VERTEX_BUFFER_VIEW meshViews[]{
                        { batch.mesh->vertexBuffer->getGpuVirtualAddress(),
                            static_cast<UINT>(meshBytes), sizeof(MeshVertex) },
                        { m_meshInstanceBuffers[frameIndex].getGpuVirtualAddress(),
                            static_cast<UINT>(meshInstanceBytes.size()), sizeof(MeshInstance) },
                    };
                    native->IASetVertexBuffers(0, 2, meshViews);
                    native->DrawInstanced(batch.vertexCount, batch.instanceCount, 0, batch.firstInstance);
                }
                else
                {
                    const auto instanceAddress = quadInstanceBuffer.getGpuVirtualAddress()
                        + static_cast<std::uint64_t>(batch.firstInstance) * sizeof(QuadInstance);
                    native->SetGraphicsRootShaderResourceView(12, instanceAddress);
                    native->DrawInstanced(QUAD_VERTEX_COUNT, batch.instanceCount, 0, 0);
                }
            }
            if (enabled)
            {
                const DX12GraphicsPipeline* boundGpuPipeline = nullptr;
                for (const ParticleGpuEmitterSubmission& emitter : m_gpuEmitterSubmissions)
                {
                    const GpuEmitterBuffers& buffers = *m_gpuEmitterStates.at(emitter.id)->buffers;
                    D3D12_GPU_DESCRIPTOR_HANDLE texture = whiteInfo->srv;
                    if (!resolveTexture(emitter.settings.texturePath, TextureColorSpace::SRGB, texture))
                        continue;
                    const std::size_t blendIndex = static_cast<std::size_t>(emitter.settings.blendMode);
                    const DX12GraphicsPipeline* const pipeline = targetFormat == DXGI_FORMAT_R16G16B16A16_FLOAT
                        ? &m_deferredGpuPipelines[blendIndex] : &m_forwardGpuPipelines[blendIndex];
                    if (pipeline != boundGpuPipeline)
                    {
                        if (!pipeline->bind(commandList))
                            return false;
                        native->SetGraphicsRoot32BitConstants(0, 16, &viewProjection._11, 0);
                        boundGpuPipeline = pipeline;
                    }
                    const std::uint32_t appearance = (emitter.settings.texturePath.empty() ? 0u : 1u)
                        | (static_cast<std::uint32_t>(emitter.settings.blendMode) << 8);
                    const auto& world = emitter.worldMatrix;
                    const Vector3 scaleX(world._11, world._12, world._13);
                    const Vector3 scaleY(world._21, world._22, world._23);
                    const Vector3 scaleZ(world._31, world._32, world._33);
                    const float worldScale = std::max({ scaleX.Length(), scaleY.Length(), scaleZ.Length() });
                    if (!std::isfinite(worldScale))
                    {
                        LOG_ERROR("[ParticleEffect] GPU emitter world scale is invalid.");
                        return false;
                    }
                    const GpuParticleDrawConstants drawConstants{
                        { 0u, 1u, emitter.settings.flipbookColumns,
                            emitter.settings.flipbookRows },
                        Vector4(right.x, right.y, right.z, worldScale),
                        Vector4(up.x, up.y, up.z, 0.0f),
                    };
                    native->SetGraphicsRootDescriptorTable(1, texture);
                    native->SetGraphicsRoot32BitConstants(2, 1, &appearance, 0);
                    native->SetGraphicsRootShaderResourceView(3, buffers.particles.get()->GetGPUVirtualAddress());
                    native->SetGraphicsRoot32BitConstants(4, 16, &emitter.worldMatrix._11, 0);
                    native->SetGraphicsRoot32BitConstants(5, 12, &drawConstants, 0);
                    native->SetGraphicsRootShaderResourceView(6, buffers.drawData.get()->GetGPUVirtualAddress());
                    native->ExecuteIndirect(m_gpuDrawSignature.Get(), 1, buffers.drawData.get(), 0, nullptr, 0);
                    ++m_gpuEmitterDrawCount;
                }
            }
        }
        if (usesSoftParticles)
        {
            std::swap(depthBarrier.Transition.StateBefore, depthBarrier.Transition.StateAfter);
            native->ResourceBarrier(1, &depthBarrier);
        }
        if (profiler != nullptr && !profiler->endFrame(frameIndex, commandList))
            return false;
        if (!commandList.close())
        {
            LOG_ERROR("[ParticleEffect] Failed to close particle command list.");
            return false;
        }
        curveRecording.committed = true;
        m_frameRecorded[frameIndex] = true;
        return true;
    }

    ID3D12CommandList* ParticleRenderer::getForExecution(const std::uint32_t frameIndex) const noexcept
    {
        return frameIndex < FRAME_COUNT && m_frameRecorded[frameIndex]
            ? m_commandLists[frameIndex].getForExecution() : nullptr;
    }

    bool ParticleRenderer::markSubmitted(const std::uint32_t frameIndex, const std::uint64_t fenceValue)
    {
        if (frameIndex >= FRAME_COUNT)
        {
            LOG_ERROR("[ParticleEffect] Invalid frame index while marking particle resources.");
            return false;
        }
        if (!m_frameRecorded[frameIndex])
            return true;
        bool succeeded = m_commandLists[frameIndex].markSubmitted(fenceValue);
        if (m_frameUsesQuadInstanceBuffer[frameIndex])
            succeeded = m_quadInstanceBuffers[frameIndex].markUsed(fenceValue) && succeeded;
        if (m_frameUsesMeshInstanceBuffer[frameIndex])
            succeeded = m_meshInstanceBuffers[frameIndex].markUsed(fenceValue) && succeeded;
        if (m_frameUsesSort[frameIndex])
            succeeded = m_sortedFrames[frameIndex].upload.markUsed(fenceValue) && succeeded;
        if (!m_frameGpuEmitterIds[frameIndex].empty())
        {
            succeeded = m_gpuSimulationSettings[frameIndex].markUsed(fenceValue) && succeeded;
            succeeded = m_gpuCurveSettings[frameIndex].markUsed(fenceValue) && succeeded;
        }
        for (const MeshData* mesh : m_usedMeshes[frameIndex])
            succeeded = mesh->vertexBuffer->markUsed(fenceValue) && succeeded;
        for (const std::uint64_t id : m_frameGpuEmitterIds[frameIndex])
        {
            const auto state = m_gpuEmitterStates.find(id);
            if (state != m_gpuEmitterStates.end())
                state->second->lastFenceValue = fenceValue;
        }
        return succeeded;
    }

    bool ParticleRenderer::recordSorted(const std::uint32_t frameIndex, const DXGI_FORMAT format,
        const Matrix& viewProjection, const std::span<const std::byte> depthConstants,
        const std::array<Vector4, 4>& camera, const D3D12_GPU_DESCRIPTOR_HANDLE white,
        const D3D12_GPU_DESCRIPTOR_HANDLE depth, const D3D12_GPU_DESCRIPTOR_HANDLE scene)
    {
        // The signature consumes the first 40 bytes. Compute uses the remaining bytes as the stable sort key.
        static_assert(sizeof(ParticleCommand) == 48 && offsetof(ParticleCommand, draw) == 24
            && offsetof(ParticleCommand, selection) == 8 && offsetof(ParticleCommand, depth) == 40);
        static_assert(sizeof(ParticleMaterial) == 32 && sizeof(ParticleEmitterDraw) == 160
            && sizeof(MeshVertex) == 20 && sizeof(SortedFlipbookFrame) == 8);
        auto& frame = m_sortedFrames[frameIndex];
        auto& list = m_commandLists[frameIndex];
        auto& native = *list.getForRecording();
        auto& device = *m_device->get();
        auto& heap = TextureManager::instance().getDescriptorHeap();
        const auto heapStart = heap.get()->GetGPUDescriptorHandleForHeapStart();
        const auto increment = device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const auto descriptorIndex = [&](const D3D12_GPU_DESCRIPTOR_HANDLE handle)
        {
            return static_cast<std::uint32_t>(((handle.ptr ? handle.ptr : white.ptr) - heapStart.ptr) / increment);
        };
        const auto materialFor = [&](const DrawBatch& batch) -> ParticleMaterial
        {
            return { { descriptorIndex(batch.texture), descriptorIndex(batch.auxiliaryTextures[0]),
                descriptorIndex(batch.auxiliaryTextures[1]), descriptorIndex(batch.auxiliaryTextures[2]) },
                batch.appearance | (static_cast<std::uint32_t>(batch.blendMode) << 8) | (batch.textureFlags << 16),
                batch.softDistance, batch.uvDistortion, batch.refractionPixels };
        };
        auto& [commands, materials, emitters, ranges, frames] = m_sortedStaging;
        commands.clear();
        materials.clear();
        emitters.clear();
        ranges.clear();
        frames.clear();
        auto count = static_cast<std::uint32_t>(m_batches.size());
        try
        {
            commands.reserve(count);
            materials.reserve(count + m_gpuEmitterSubmissions.size());
            emitters.reserve(m_gpuEmitterSubmissions.size());
            ranges.reserve(m_gpuEmitterSubmissions.size());
            for (const DrawBatch& batch : m_batches)
            {
                const auto ordinal = static_cast<std::uint32_t>(commands.size());
                const auto geometry = batch.mesh ? batch.mesh->vertexBuffer->getGpuVirtualAddress()
                    : m_quadInstanceBuffers[frameIndex].getGpuVirtualAddress();
                commands.push_back({ geometry, { batch.firstInstance, batch.mesh ? 1u : 0u, ordinal, 0 },
                    { batch.mesh ? batch.vertexCount : QUAD_VERTEX_COUNT, batch.instanceCount, 0, 0 },
                    batch.depth, ordinal });
                materials.push_back(materialFor(batch));
            }
            for (const ParticleGpuEmitterSubmission& emitter : m_gpuEmitterSubmissions)
            {
                const auto& settings = emitter.settings;
                DrawBatch batch{ .texture = white, .blendMode = settings.blendMode,
                    .appearance = settings.texturePath.empty() ? 0u : 1u, .softDistance = settings.softDistance };
                batch.auxiliaryTextures.fill(white);
                std::uint32_t width = 1, height = 1;
                bool valid = resolveTexture(settings.texturePath, TextureColorSpace::SRGB, batch.texture, &width, &height);
                const std::array paths{ &settings.secondaryTexturePath, &settings.maskTexturePath,
                    &settings.distortionTexturePath };
                for (std::size_t index = 0; index < paths.size(); ++index)
                {
                    if (!paths[index]->empty())
                        batch.textureFlags |= 1u << index;
                    valid = resolveTexture(*paths[index], index == 0 ? TextureColorSpace::SRGB : TextureColorSpace::Linear,
                        batch.auxiliaryTextures[index]) && valid;
                }
                batch.uvDistortion = settings.uvDistortion;
                batch.refractionPixels = settings.refractionPixels;
                const auto& world = emitter.worldMatrix;
                const float worldScale = std::max({ Vector3(world._11, world._12, world._13).Length(),
                    Vector3(world._21, world._22, world._23).Length(), Vector3(world._31, world._32, world._33).Length() });
                if (!std::isfinite(worldScale))
                {
                    LOG_ERROR("[ParticleEffect] GPU emitter world scale is invalid.");
                    return false;
                }
                const auto sequenceOffset = static_cast<std::uint32_t>(frames.size());
                double duration = 0.0;
                if (settings.flipbookPlayback == ParticleFlipbookPlayback::Custom)
                    for (const auto& frameDescription : settings.flipbookFrames)
                    {
                        frames.push_back({ frameDescription.frame, frameDescription.duration });
                        duration += frameDescription.duration;
                    }
                const Vector4 atlasInset = settings.texturePath.empty() ? Vector4::Zero : Vector4(
                    std::min(0.5f / static_cast<float>(width), 0.5f / static_cast<float>(settings.flipbookColumns)),
                    std::min(0.5f / static_cast<float>(height), 0.5f / static_cast<float>(settings.flipbookRows)),
                    0.0f, 0.0f);
                emitters.push_back({ world, Vector4(settings.initialSizeScale.x, settings.initialSizeScale.y,
                    worldScale, settings.stretchBySpeed),
                    Vector4(settings.orientationDirection.x, settings.orientationDirection.y, settings.orientationDirection.z,
                        static_cast<float>(settings.orientation)),
                    Vector4(settings.uvScroll.x, settings.uvScroll.y, settings.flipbookFramesPerSecond,
                        static_cast<float>(duration)),
                    atlasInset,
                    { settings.flipbookColumns, settings.flipbookRows, static_cast<std::uint32_t>(settings.flipbookPlayback),
                        settings.flipbookLoopCount },
                    { sequenceOffset, static_cast<std::uint32_t>(frames.size()) - sequenceOffset,
                        settings.flipbookRandomStartFrame ? 1u : 0u, settings.flipbookFrameBlending ? 1u : 0u } });
                const auto* buffers = m_gpuEmitterStates.at(emitter.id)->buffers.get();
                ranges.push_back({ buffers, valid ? settings.maxParticles : 0u, count,
                    static_cast<std::uint32_t>(materials.size()), count + (valid ? settings.maxParticles : 0u),
                    static_cast<std::uint32_t>(materials.size() + 1) });
                materials.push_back(materialFor(batch));
                count += ranges.back().count;
                if (buffers->trails.get())
                {
                    for (std::size_t mode = 0; mode < BLEND_MODE_COUNT; ++mode)
                        materials.push_back(materialFor({ .texture = white, .blendMode = static_cast<ParticleBlendMode>(mode),
                            .appearance = 2 }));
                    count += settings.maxParticles * GPU_TRAIL_POINTS;
                }
            }
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ParticleEffect] Failed to allocate staging data for sorted particles.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ParticleEffect] Sorted particle staging data exceeds capacity.");
            return false;
        }
        if (count == 0)
            return true;

        constexpr std::uint32_t sortThreads = 128;
        const std::uint32_t capacity = std::bit_ceil(count);
        const auto commandBytes = std::as_bytes(std::span(commands));
        const auto materialBytes = std::as_bytes(std::span(materials));
        const auto emitterBytes = std::as_bytes(std::span(emitters));
        const auto frameBytes = std::as_bytes(std::span(frames));
        constexpr std::uint64_t materialsOffset = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        const auto align16 = [](const std::uint64_t bytes) { return (bytes + 15) & ~std::uint64_t{15}; };
        const auto emittersOffset = align16(materialsOffset + materialBytes.size());
        const auto framesOffset = align16(emittersOffset + emitterBytes.size());
        const auto commandsOffset = align16(framesOffset + frameBytes.size());
        const auto uploadSize = std::bit_ceil(commandsOffset + commandBytes.size());
        if (frame.upload.getSize() < uploadSize && (!frame.upload.finalize()
            || !frame.upload.initialize(device, *m_fence, uploadSize)))
            return false;
        const auto initializeUav = [&](DX12Resource& buffer, const std::uint64_t bytes)
        {
            if (buffer.get() && buffer.getDescription().Width >= bytes)
                return true;
            buffer.finalize();
            D3D12_RESOURCE_DESC description{};
            description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            description.Width = bytes;
            description.Height = description.DepthOrArraySize = description.MipLevels = 1;
            description.SampleDesc.Count = 1;
            description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return buffer.initialize(device, { .description = description,
                .initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS });
        };
        if (!initializeUav(frame.commands, static_cast<std::uint64_t>(capacity) * sizeof(ParticleCommand))
            || !initializeUav(frame.count, sizeof(std::uint32_t))
            || !frame.upload.write(depthConstants)
            || !frame.upload.write(materialBytes, materialsOffset)
            || (!emitterBytes.empty() && !frame.upload.write(emitterBytes, emittersOffset))
            || (!frameBytes.empty() && !frame.upload.write(frameBytes, framesOffset))
            || (!commandBytes.empty() && !frame.upload.write(commandBytes, commandsOffset)))
            return false;
        const auto uploadAddress = frame.upload.getGpuVirtualAddress();
        if (!commandBytes.empty())
        {
            if (!frame.commands.transition(list, D3D12_RESOURCE_STATE_COPY_DEST))
                return false;
            native.CopyBufferRegion(frame.commands.get(), 0, frame.upload.get(), commandsOffset, commandBytes.size());
        }
        if (!frame.commands.transition(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            || !frame.count.transition(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            || !m_sortPipeline.bind(list))
            return false;
        native.SetComputeRoot32BitConstants(1, 16, camera.data(), 0);
        native.SetComputeRootShaderResourceView(2, uploadAddress);
        native.SetComputeRootShaderResourceView(3, uploadAddress);
        native.SetComputeRootShaderResourceView(4, emitterBytes.empty() ? uploadAddress : uploadAddress + emittersOffset);
        native.SetComputeRootUnorderedAccessView(5, frame.commands.get()->GetGPUVirtualAddress());
        native.SetComputeRootUnorderedAccessView(6, frame.count.get()->GetGPUVirtualAddress());
        native.SetComputeRootShaderResourceView(7, uploadAddress);
        const auto dispatch = [&](const std::array<std::uint32_t, 4>& parameters,
            const std::array<std::uint32_t, 4>& geometry = {})
        {
            native.SetComputeRoot32BitConstants(0, 4, parameters.data(), 0);
            native.SetComputeRoot32BitConstants(0, 4, geometry.data(), 4);
            native.Dispatch((parameters[1] + sortThreads - 1) / sortThreads, 1, 1);
        };
        const auto orderCommandWrites = [&]()
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = frame.commands.get();
            native.ResourceBarrier(1, &barrier);
        };
        for (std::uint32_t index = 0; index < ranges.size(); ++index)
        {
            const auto& range = ranges[index];
            const auto address = range.buffers->particles.get()->GetGPUVirtualAddress();
            native.SetComputeRootShaderResourceView(2, address);
            native.SetComputeRootShaderResourceView(3, range.buffers->drawData.get()->GetGPUVirtualAddress());
            if (range.count != 0)
                dispatch({ 0, range.count, range.offset, range.material },
                    { static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32), index, 0 });
            if (range.buffers->trails.get())
            {
                const auto trailAddress = range.buffers->trails.get()->GetGPUVirtualAddress();
                native.SetComputeRootShaderResourceView(7, trailAddress);
                dispatch({ 4, m_gpuEmitterSubmissions[index].settings.maxParticles * GPU_TRAIL_POINTS,
                    range.trailOffset, range.trailMaterial },
                    { static_cast<std::uint32_t>(trailAddress), static_cast<std::uint32_t>(trailAddress >> 32), 0, 0 });
            }
        }
        if (capacity > count)
            dispatch({ 3, capacity - count, count, 0 });
        orderCommandWrites();
        for (std::uint32_t span = 2; span <= capacity; span *= 2)
            for (std::uint32_t stride = span / 2; stride != 0; stride /= 2)
            {
                dispatch({ 1, capacity, stride, span });
                orderCommandWrites();
            }
        const std::array<std::uint32_t, 8> countParameters{ 2, capacity };
        native.SetComputeRoot32BitConstants(0, 8, countParameters.data(), 0);
        native.Dispatch(1, 1, 1);
        if (!frame.commands.transition(list, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)
            || !frame.count.transition(list, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT))
            return false;

        const std::size_t pipeline = format == DXGI_FORMAT_R8G8B8A8_UNORM ? 0 : 1;
        if (!m_sortedPipelines[pipeline].bind(list))
            return false;
        native.SetGraphicsRoot32BitConstants(0, 16, &viewProjection._11, 0);
        native.SetGraphicsRootDescriptorTable(1, depth);
        native.SetGraphicsRootDescriptorTable(2, scene);
        native.SetGraphicsRootConstantBufferView(3, uploadAddress);
        native.SetGraphicsRootDescriptorTable(4, heapStart);
        native.SetGraphicsRoot32BitConstants(5, 16, camera.data(), 0);
        const auto meshAddress = m_meshInstances.empty() ? uploadAddress
            : m_meshInstanceBuffers[frameIndex].getGpuVirtualAddress();
        native.SetGraphicsRootShaderResourceView(8, meshAddress);
        native.SetGraphicsRootShaderResourceView(9, uploadAddress + materialsOffset);
        native.SetGraphicsRootShaderResourceView(10, emitterBytes.empty() ? uploadAddress : uploadAddress + emittersOffset);
        native.SetGraphicsRootShaderResourceView(11, frameBytes.empty() ? uploadAddress : uploadAddress + framesOffset);
        native.SetGraphicsRoot32BitConstant(12, pipeline == 0 ? 1u : 0u, 0);
        native.ExecuteIndirect(m_sortedSignatures[pipeline].Get(), capacity, frame.commands.get(), 0, frame.count.get(), 0);
        m_frameUsesSort[frameIndex] = true;
        m_batches.clear();
        ++m_gpuEmitterDrawCount;
        return true;
    }

    bool ParticleRenderer::updateGpuCurves(GpuEmitterState& state, const ParticleGpuEmitterSubmission& emitter)
    {
        const auto& settings = emitter.settings;
        bool repack = emitter.clear || state.needsClear;
        if (repack)
        {
            state.curveVersions.clear();
            state.birthCurveVersion = 0;
        }
        else
        {
            for (auto& version : state.curveVersions)
            {
                if (version.remainingLifetime <= 0.0)
                {
                    if (!version.words.empty())
                    {
                        version.words.clear();
                        version.maximumLifetime = 0.0f;
                        repack = true;
                    }
                    continue;
                }
                // GPU particle ages are floats; account for the maximum rounding loss per update.
                const double rounding = static_cast<double>(version.maximumLifetime)
                    * (static_cast<double>(std::numeric_limits<float>::epsilon()) * 0.5);
                const double advance = std::max(0.0, static_cast<double>(emitter.deltaTime) - rounding);
                version.remainingLifetime -= advance;
            }
        }
        state.birthCurveVersion = 0;
        if (emitter.emission.maySpawn(settings) || emitter.motion.distance > 0.0f)
        {
            if (settings.curveVariants.size() >= GPU_CURVE_SETS)
                return false;
            auto& words = m_curveBirthScratch;
            words.assign(GPU_CURVE_SETS * GPU_CURVE_SET_WORDS, 0);
            const auto packSet = [&](const ParticleLifetimeCurves& curves, const std::size_t set)
            {
                std::size_t descriptor = set * GPU_CURVE_SET_WORDS;
                words[descriptor++] = curves.smooth ? 1u : 0u;
                const auto packTrack = [&](const auto& keys)
                {
                    if (keys.size() > GPU_CURVE_TRACK_KEYS)
                        return false;
                    const auto track = descriptor++;
                    if (keys.empty())
                        return true;
                    words[track] = (static_cast<std::uint32_t>(words.size()) << 8)
                        | static_cast<std::uint32_t>(keys.size());
                    for (const auto& key : keys)
                    {
                        words.push_back(std::bit_cast<std::uint32_t>(key.time));
                        using Value = std::remove_cvref_t<decltype(key.value)>;
                        if constexpr (std::is_same_v<Value, float>)
                            words.insert(words.end(), { std::bit_cast<std::uint32_t>(key.value), 0, 0, 0 });
                        else
                        {
                            words.push_back(std::bit_cast<std::uint32_t>(key.value.x));
                            words.push_back(std::bit_cast<std::uint32_t>(key.value.y));
                            words.push_back(std::bit_cast<std::uint32_t>(key.value.z));
                            if constexpr (std::is_same_v<Value, Vector4>)
                                words.push_back(std::bit_cast<std::uint32_t>(key.value.w));
                            else
                                words.push_back(0);
                        }
                    }
                    return true;
                };
                return packTrack(curves.color) && packTrack(curves.size) && packTrack(curves.width)
                    && packTrack(curves.height) && packTrack(curves.speed) && packTrack(curves.angularSpeed)
                    && packTrack(curves.acceleration) && packTrack(curves.velocity)
                    && packTrack(curves.gravity) && packTrack(curves.drag);
            };
            if (!packSet(settings.curves, 0))
                return false;
            for (std::size_t i = 0; i < settings.curveVariants.size(); ++i)
                if (!packSet(settings.curveVariants[i], i + 1))
                    return false;

            const bool hasKeys = words.size() > GPU_CURVE_SETS * GPU_CURVE_SET_WORDS;
            if (hasKeys)
            {
                auto version = std::find_if(state.curveVersions.begin(), state.curveVersions.end(),
                    [&words](const auto& candidate) { return !candidate.words.empty() && candidate.words == words; });
                if (version == state.curveVersions.end())
                {
                    version = std::find_if(state.curveVersions.begin(), state.curveVersions.end(),
                        [](const auto& candidate) { return candidate.words.empty(); });
                    if (version == state.curveVersions.end())
                    {
                        state.curveVersions.emplace_back();
                        version = std::prev(state.curveVersions.end());
                    }
                    version->words.assign(words.begin(), words.end());
                    version->maximumLifetime = 0.0f;
                    repack = true;
                }
                state.birthCurveVersion = 1u + static_cast<std::uint32_t>(version - state.curveVersions.begin());
                const float lifetimeBound = std::nextafter(settings.lifetimeMax,
                    (std::numeric_limits<float>::infinity)());
                version->remainingLifetime = std::max(version->remainingLifetime, static_cast<double>(lifetimeBound));
                version->maximumLifetime = std::max(version->maximumLifetime, lifetimeBound);
            }
        }
        if (repack)
        {
            auto& packed = state.packedCurves;
            packed.assign(state.curveVersions.size(), 0);
            for (std::size_t i = 0; i < state.curveVersions.size(); ++i)
            {
                auto& version = state.curveVersions[i];
                if (version.words.empty())
                    continue;
                constexpr auto maxWords = (std::numeric_limits<std::uint32_t>::max)() / sizeof(std::uint32_t);
                if (packed.size() > maxWords || version.words.size() > maxWords - packed.size())
                {
                    LOG_ERROR("[ParticleEffect] GPU lifetime curve input exceeds the shader address range.");
                    return false;
                }
                packed[i] = static_cast<std::uint32_t>(packed.size());
                packed.insert(packed.end(), version.words.begin(), version.words.end());
            }
        }
        return true;
    }

    bool ParticleRenderer::prepareGpuCurves(const std::uint32_t frameIndex)
    {
        auto& upload = m_gpuCurveSettings[frameIndex];
        // Keep a fence-protected copy per frame so edits never overwrite inputs of an in-flight dispatch.
        std::uint64_t bytes = 16; // Valid binding for emitters with no curve data.
        try
        {
            m_curveUploadOffsets.clear();
            m_curveUploadOffsets.reserve(m_gpuEmitterSubmissions.size());
            for (const auto& emitter : m_gpuEmitterSubmissions)
            {
                auto& state = *m_gpuEmitterStates.at(emitter.id);
                if (!updateGpuCurves(state, emitter))
                {
                    LOG_ERROR("[ParticleEffect] GPU lifetime curves exceeded the supported key or set limit.");
                    return false;
                }
                const auto size = (state.packedCurves.size()
                    + emitter.settings.emissionRateKeys.size() * GPU_RATE_KEY_WORDS
                    + emitter.settings.bursts.size() * GPU_BURST_WORDS) * sizeof(std::uint32_t);
                if (size > (std::numeric_limits<std::uint32_t>::max)())
                {
                    LOG_ERROR("[ParticleEffect] GPU particle input exceeds the shader address range.");
                    return false;
                }
                m_curveUploadOffsets.push_back(size == 0 ? 0 : bytes);
                bytes += (size + 15) & ~std::uint64_t{15};
            }
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ParticleEffect] Failed to stage GPU lifetime curve versions.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ParticleEffect] GPU lifetime curve staging exceeded capacity.");
            return false;
        }
        if (upload.getSize() < bytes && (!upload.finalize()
            || !upload.initialize(*m_device->get(), *m_fence, std::bit_ceil(bytes))))
        {
            LOG_ERROR("[ParticleEffect] Failed to allocate GPU lifetime curve input.");
            return false;
        }
        for (std::size_t i = 0; i < m_gpuEmitterSubmissions.size(); ++i)
        {
            const auto& emitter = m_gpuEmitterSubmissions[i];
            const auto& packed = m_gpuEmitterStates.at(emitter.id)->packedCurves;
            auto offset = m_curveUploadOffsets[i];
            if (!packed.empty() && !upload.write(std::as_bytes(std::span(packed)), offset))
                return false;
            offset += packed.size() * sizeof(std::uint32_t);
            std::array<std::uint32_t, 32 * (GPU_RATE_KEY_WORDS + GPU_BURST_WORDS)> emissionWords;
            std::size_t count = 0;
            for (const auto& key : emitter.settings.emissionRateKeys)
            {
                emissionWords[count++] = std::bit_cast<std::uint32_t>(key.time);
                emissionWords[count++] = std::bit_cast<std::uint32_t>(key.value);
            }
            for (const auto& burst : emitter.settings.bursts)
            {
                emissionWords[count++] = std::bit_cast<std::uint32_t>(burst.time);
                emissionWords[count++] = std::bit_cast<std::uint32_t>(burst.repeatInterval);
                emissionWords[count++] = std::bit_cast<std::uint32_t>(burst.probability);
                emissionWords[count++] = burst.repeatCount;
                emissionWords[count++] = burst.randomCount ? burst.countMin : burst.count;
                emissionWords[count++] = burst.randomCount ? burst.countMax : burst.count;
            }
            if (count != 0 && !upload.write(std::as_bytes(std::span(emissionWords).first(count)), offset))
                return false;
        }
        return true;
    }

    bool ParticleRenderer::ensureGpuEmitterBuffers(GpuEmitterState& state, const std::uint32_t requestedCapacity)
    {
        if (requestedCapacity == 0 || requestedCapacity > MAX_GPU_PARTICLES)
        {
            LOG_ERROR("[ParticleEffect] Invalid GPU emitter buffer capacity {}.", requestedCapacity);
            return false;
        }
        if (state.buffers && requestedCapacity <= state.capacity)
            return true;
        if (m_device == nullptr || m_device->get() == nullptr)
        {
            LOG_ERROR("[ParticleEffect] Cannot allocate GPU particle state before device initialization.");
            return false;
        }

        const std::uint32_t capacity = state.capacity == 0 ? requestedCapacity
            : std::max(requestedCapacity, std::min(state.capacity * 2, MAX_GPU_PARTICLES));
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = static_cast<std::uint64_t>(capacity) * sizeof(GpuParticleState);
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.SampleDesc = { 1, 0 };
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        std::unique_ptr<GpuEmitterBuffers> replacement;
        try
        {
            replacement = std::make_unique<GpuEmitterBuffers>();
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ParticleEffect] Failed to allocate GPU particle buffer wrappers.");
            return false;
        }
        if (!replacement->particles.initialize(*m_device->get(), {
            .description = description,
            .initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS }))
        {
            LOG_ERROR("[ParticleEffect] Failed to allocate the GPU particle state buffer.");
            return false;
        }
        description.Width = sizeof(D3D12_DRAW_ARGUMENTS) + static_cast<std::uint64_t>(capacity) * sizeof(std::uint32_t);
        if (!replacement->drawData.initialize(*m_device->get(), {
            .description = description,
            .initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS }))
        {
            LOG_ERROR("[ParticleEffect] Failed to allocate the GPU particle indirect draw data buffer.");
            return false;
        }
        description.Width = GPU_SCHEDULER_WORDS * sizeof(std::uint32_t);
        if (!state.scheduler.get() && !state.scheduler.initialize(*m_device->get(), {
            .description = description, .initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS }))
        {
            LOG_ERROR("[ParticleEffect] Failed to allocate the GPU emission scheduler.");
            return false;
        }
        if (state.buffers && state.lastFenceValue != 0 && m_fence != nullptr
            && !m_fence->isComplete(state.lastFenceValue))
        {
            try
            {
                state.retiredBuffers.reserve(state.retiredBuffers.size() + 1);
            }
            catch (const std::bad_alloc&)
            {
                LOG_ERROR("[ParticleEffect] Failed to retain GPU particle buffers during resize.");
                return false;
            }
            catch (const std::length_error&)
            {
                LOG_ERROR("[ParticleEffect] Retired GPU particle buffer list exceeds capacity.");
                return false;
            }
            state.retiredBuffers.push_back({ std::move(state.buffers), state.lastFenceValue });
        }
        state.buffers = std::move(replacement);
        state.capacity = capacity;
        return true;
    }

    bool ParticleRenderer::ensureGpuTrailBuffer(GpuEmitterState& state)
    {
        auto& trails = state.buffers->trails;
        if (trails.get())
            return true;
        auto description = state.buffers->particles.getDescription();
        description.Width = static_cast<std::uint64_t>(state.capacity) * (GPU_TRAIL_STRIDE + sizeof(std::uint32_t))
            + 2 * sizeof(std::uint32_t);
        if (!trails.initialize(*m_device->get(), { .description = description,
            .initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS }))
        {
            LOG_ERROR("[ParticleEffect] Failed to allocate GPU trail history.");
            return false;
        }
        state.needsTrailClear = true;
        return true;
    }

    bool ParticleRenderer::resolveTexture(const std::filesystem::path& path, const TextureColorSpace colorSpace,
        D3D12_GPU_DESCRIPTOR_HANDLE& texture, std::uint32_t* const width, std::uint32_t* const height)
    {
        if (path.empty())
            return true;
        const bool srgb = colorSpace == TextureColorSpace::SRGB;
        const auto kind = srgb ? ParticleResourceKind::ColorTexture : ParticleResourceKind::LinearTexture;
        auto& failures = m_failedTextures[srgb ? 0 : 1];
        auto& cache = srgb ? m_textureCache : m_linearTextureCache;
        const auto failed = failures.find(path);
        if (failed != failures.end() && m_frameNumber - failed->second < 120)
        {
            ParticleRenderSubmissionQueue::instance().setResourceFailed(path, kind, true);
            return false;
        }
        const auto cacheEntry = cache.find(path);
        TextureHandle handle = cacheEntry == cache.end() ? TextureHandle{} : cacheEntry->second;
        auto& manager = TextureManager::instance();
        if (!manager.get(handle))
        {
            TextureLoadDesc description;
            description.colorSpace = colorSpace;
            description.generateMips = false;
            handle = manager.load(path, description);
        }
        const Texture* const loaded = manager.get(handle);
        const TextureResourceInfo* const info = loaded ? loaded->getResourceInfo() : nullptr;
        if (info == nullptr || info->type != TextureType::Texture2D || info->width == 0 || info->height == 0)
        {
            LOG_ERROR("[ParticleEffect] Particle texture could not be loaded as a 2D texture: {}",
                String::fromWide(path.c_str()).c_str());
            failures[path] = m_frameNumber;
            ParticleRenderSubmissionQueue::instance().setResourceFailed(path, kind, true);
            return false;
        }
        texture = info->srv;
        if (width) *width = info->width;
        if (height) *height = info->height;
        cache[path] = handle;
        failures.erase(path);
        ParticleRenderSubmissionQueue::instance().setResourceFailed(path, kind, false);
        return true;
    }

    const ParticleRenderer::MeshData* ParticleRenderer::resolveMesh(const std::filesystem::path& path)
    {
        auto& manager = ModelManager::instance();
        const auto cached = m_meshCache.find(path);
        if (cached != m_meshCache.end() && manager.get(cached->second.handle) == cached->second.source)
        {
            ParticleRenderSubmissionQueue::instance().setResourceFailed(path, ParticleResourceKind::Mesh, false);
            return &cached->second;
        }
        const auto failed = m_failedMeshes.find(path);
        if (failed != m_failedMeshes.end() && m_frameNumber - failed->second < 120)
        {
            ParticleRenderSubmissionQueue::instance().setResourceFailed(path, ParticleResourceKind::Mesh, true);
            return nullptr;
        }

        auto extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](const wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
        const ModelHandle handle = extension == L".model" || extension == L".mdl"
            ? manager.load(path) : ModelHandle{};
        const auto model = manager.get(handle);
        const auto fail = [&]()
        {
            LOG_ERROR("[ParticleEffect] Static particle mesh is invalid or unavailable: {}",
                String::fromWide(path.c_str()).c_str());
            m_failedMeshes[path] = m_frameNumber;
            ParticleRenderSubmissionQueue::instance().setResourceFailed(path, ParticleResourceKind::Mesh, true);
            return static_cast<const MeshData*>(nullptr);
        };
        if (!model)
            return fail();

        MeshData result;
        result.handle = handle;
        result.source = model;
        std::vector<Matrix> nodeTransforms(model->nodes.size(), Matrix::Identity);
        for (std::size_t index = 0; index < model->nodes.size(); ++index)
        {
            const auto& node = model->nodes[index];
            if (node.parentIndex >= 0 && static_cast<std::size_t>(node.parentIndex) >= index)
                return fail();
            nodeTransforms[index] = node.localTransform;
            if (node.parentIndex >= 0)
                nodeTransforms[index] *= nodeTransforms[static_cast<std::size_t>(node.parentIndex)];
            if (!isFinite(nodeTransforms[index]))
                return fail();
        }
        for (std::size_t meshIndex = 0; meshIndex < model->meshes.size(); ++meshIndex)
        {
            const auto& mesh = model->meshes[meshIndex];
            Matrix transform = Matrix::Identity;
            if (!mesh.verticesInModelSpace)
                for (std::size_t index = 0; index < model->nodes.size(); ++index)
                    if (std::find(model->nodes[index].meshIndices.begin(), model->nodes[index].meshIndices.end(), meshIndex)
                        != model->nodes[index].meshIndices.end())
                    {
                        transform = nodeTransforms[index];
                        break;
                    }
            const auto appendRange = [&](const std::size_t start, const std::size_t count)
            {
                if (count == 0 || count % 3 != 0 || start > mesh.indices.size()
                    || count > mesh.indices.size() - start || count > MAX_RENDER_VERTICES - result.vertices.size())
                    return false;
                for (std::size_t index = start; index < start + count; ++index)
                {
                    if (mesh.indices[index] >= mesh.vertices.size())
                        return false;
                    const auto& vertex = mesh.vertices[mesh.indices[index]];
                    if (vertex.boneWeights != Vector4::Zero || !isFinite(vertex.position)
                        || !std::isfinite(vertex.texCoord.x) || !std::isfinite(vertex.texCoord.y))
                        return false;
                    const Vector3 position = Vector3::Transform(vertex.position, transform);
                    if (!isFinite(position))
                        return false;
                    result.vertices.push_back({ position, vertex.texCoord });
                }
                return true;
            };
            if (mesh.subMeshes.empty())
            {
                if (!appendRange(0, mesh.indices.size()))
                    return fail();
            }
            else
                for (const auto& subMesh : mesh.subMeshes)
                    if (!appendRange(subMesh.indexStart, subMesh.indexCount))
                        return fail();
        }
        if (result.vertices.empty())
            return fail();
        if (m_device == nullptr || m_device->get() == nullptr || m_fence == nullptr
            || result.vertices.size() > (std::numeric_limits<std::size_t>::max)() / sizeof(MeshVertex))
            return fail();
        result.vertexBuffer = std::make_unique<DX12UploadBuffer>();
        const auto vertexBytes = std::as_bytes(std::span<const MeshVertex>(result.vertices));
        if (!result.vertexBuffer->initialize(*m_device->get(), *m_fence, vertexBytes.size())
            || !result.vertexBuffer->write(vertexBytes))
            return fail();
        auto [entry, inserted] = m_meshCache.insert_or_assign(path, std::move(result));
        static_cast<void>(inserted);
        m_failedMeshes.erase(path);
        ParticleRenderSubmissionQueue::instance().setResourceFailed(path, ParticleResourceKind::Mesh, false);
        return &entry->second;
    }

    bool ParticleRenderer::ensureSceneCopy(const std::uint32_t frameIndex, const DX12Resource& source)
    {
        if (frameIndex >= FRAME_COUNT || !m_device || !m_device->get() || !source.get())
        {
            LOG_ERROR("[ParticleEffect] Cannot create a scene copy without a valid device and source.");
            return false;
        }
        const auto& sourceDesc = source.getDescription();
        if (sourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D
            || sourceDesc.DepthOrArraySize != 1 || sourceDesc.MipLevels != 1
            || sourceDesc.SampleDesc.Count != 1)
        {
            LOG_ERROR("[ParticleEffect] Unsupported scene color resource for refraction.");
            return false;
        }
        auto& copy = m_sceneCopies[frameIndex];
        const auto& existing = copy.getDescription();
        if (copy.get() && existing.Width == sourceDesc.Width && existing.Height == sourceDesc.Height
            && existing.Format == sourceDesc.Format)
            return true;
        if (!m_sceneCopyViews[frameIndex])
            m_sceneCopyViews[frameIndex] = TextureManager::instance().getDescriptorHeap().allocate();
        if (!m_sceneCopyViews[frameIndex] || !m_sceneCopyViews[frameIndex]->gpu)
        {
            LOG_ERROR("[ParticleEffect] Failed to allocate a scene copy SRV.");
            return false;
        }
        copy.finalize();
        auto description = sourceDesc;
        description.Flags = D3D12_RESOURCE_FLAG_NONE;
        const DX12ResourceConfig config{ .description = description,
            .initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
        if (!copy.initialize(*m_device->get(), config))
        {
            LOG_ERROR("[ParticleEffect] Failed to allocate a scene copy.");
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = description.Format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        m_device->get()->CreateShaderResourceView(copy.get(), &srv, m_sceneCopyViews[frameIndex]->cpu.native);
        return true;
    }

    bool ParticleRenderer::ensureQuadInstanceCapacity(
        const std::uint32_t frameIndex, const std::size_t instanceCount)
    {
        if (frameIndex >= FRAME_COUNT || instanceCount == 0 || instanceCount > MAX_QUAD_INSTANCES)
        {
            LOG_ERROR("[ParticleEffect] Invalid quad instance buffer size.");
            return false;
        }
        if (instanceCount <= m_quadInstanceCapacities[frameIndex])
            return true;
        if (m_device == nullptr || m_device->get() == nullptr || m_fence == nullptr)
            return false;

        const std::size_t capacity = std::min(MAX_QUAD_INSTANCES,
            std::max(std::size_t{ 1024 }, std::bit_ceil(instanceCount)));
        if (!m_quadInstanceBuffers[frameIndex].finalize())
            return false;
        m_quadInstanceCapacities[frameIndex] = 0;
        if (!m_quadInstanceBuffers[frameIndex].initialize(*m_device->get(), *m_fence, capacity * sizeof(QuadInstance)))
            return false;
        m_quadInstanceCapacities[frameIndex] = capacity;
        return true;
    }

    bool ParticleRenderer::ensureMeshInstanceCapacity(
        const std::uint32_t frameIndex, const std::size_t instanceCount)
    {
        if (frameIndex >= FRAME_COUNT || instanceCount == 0
            || instanceCount > ParticleRenderSubmissionQueue::MAX_RENDER_PARTICLES
            || instanceCount > (std::numeric_limits<std::size_t>::max)() / sizeof(MeshInstance))
        {
            LOG_ERROR("[ParticleEffect] Invalid mesh particle instance buffer size.");
            return false;
        }
        if (instanceCount <= m_meshInstanceCapacities[frameIndex])
            return true;
        if (m_device == nullptr || m_device->get() == nullptr || m_fence == nullptr)
        {
            LOG_ERROR("[ParticleEffect] Cannot allocate mesh instance data before renderer initialization.");
            return false;
        }

        std::size_t capacity = 4096;
        while (capacity < instanceCount)
        {
            if (capacity > (std::numeric_limits<std::size_t>::max)() / 2)
            {
                LOG_ERROR("[ParticleEffect] Mesh instance buffer capacity overflow.");
                return false;
            }
            capacity *= 2;
        }
        if (!m_meshInstanceBuffers[frameIndex].finalize())
            return false;
        m_meshInstanceCapacities[frameIndex] = 0;
        if (!m_meshInstanceBuffers[frameIndex].initialize(
            *m_device->get(), *m_fence, capacity * sizeof(MeshInstance)))
            return false;
        m_meshInstanceCapacities[frameIndex] = capacity;
        return true;
    }
}
