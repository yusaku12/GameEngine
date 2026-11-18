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
        // csCompact uses one group of 128 lanes with up to 32 slots per lane.
        static_assert(MAX_GPU_PARTICLES <= 128 * 32);
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
            Vector4 cameraUpScaleX;
            Vector4 sizeScale;
        };
        static_assert(sizeof(GpuParticleDrawConstants) == 16 * sizeof(std::uint32_t));

        struct GpuParticleState
        {
            Vector4 positionAge;
            Vector4 velocityLifetime;
            Vector4 startColor;
            Vector4 endColor;
            Vector4 sizeRotation;
        };
        static_assert(sizeof(GpuParticleState) == 80);

        bool supportsGpuSimulation(const ParticleEffectSettings& settings) noexcept
        {
            return settings.simulationBackend == ParticleSimulationBackend::GPU
                && settings.localSpace && settings.blendMode == ParticleBlendMode::Additive
                && settings.orientation == ParticleOrientation::Camera && !settings.trailEnabled
                && settings.meshPath.empty() && settings.softDistance == 0.0f
                && settings.refractionPixels == 0.0f && settings.secondaryTexturePath.empty()
                && settings.maskTexturePath.empty() && settings.distortionTexturePath.empty()
                && settings.uvDistortion == 0.0f && settings.uvScroll == Vector2::Zero
                && settings.bursts.empty() && settings.emissionRateKeys.empty()
                && settings.startDelay == 0.0f && !settings.prewarm
                && settings.distancePerParticle == 0.0f && settings.inheritVelocity == 0.0f
                && settings.curves.color.empty() && settings.curves.size.empty()
                && settings.curves.speed.empty() && settings.curves.angularSpeed.empty()
                && settings.curves.width.empty() && settings.curves.height.empty()
                && !settings.curves.smooth && settings.curves.acceleration.empty()
                && settings.curves.velocity.empty() && settings.curves.gravity.empty()
                && settings.curves.drag.empty() && settings.curveVariants.empty()
                && settings.flipbookPlayback == ParticleFlipbookPlayback::Lifetime
                && settings.flipbookLoopCount == 1 && !settings.flipbookRandomStartFrame
                && !settings.flipbookFrameBlending;
        }
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
        m_gpuDrawSignature.Reset();
        m_gpuEmitterStates.clear();
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
        ParticleRenderSubmissionQueue::instance().clear();
        return succeeded;
    }

    bool ParticleRenderer::rebuildPipeline(const DX12Shader& vertexShader,
        const DX12Shader& meshVertexShader, const DX12Shader& pixelShader,
        const DX12Shader& gpuVertexShader, const DX12Shader& gpuPixelShader,
        const DX12Shader& simulationShader, const DX12Shader& compactionShader)
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
        gpuDrawConstants.Constants = { 7, 0, 16 };
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
        const std::array gpuSimulationParameters{ gpuSimulationConstants, gpuSimulationBuffer };
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
        m_usedMeshes[frameIndex].clear();
        m_frameGpuEmitterIds[frameIndex].clear();
        m_gpuEmitterDrawCount = 0;
        m_renderedParticles = 0;
        ++m_frameNumber;
        ParticleRenderSubmissionQueue::instance().consume(m_particles, m_trails, m_ribbons,
            m_gpuEmitterSubmissions, m_removedGpuEmitters);
        for (const std::uint64_t id : m_removedGpuEmitters)
        {
            const auto state = m_gpuEmitterStates.find(id);
            if (state != m_gpuEmitterStates.end())
                state->second->retired = true;
        }
        for (const ParticleGpuEmitterSubmission& emitter : m_gpuEmitterSubmissions)
        {
            if (!supportsGpuSimulation(emitter.settings)
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
            if (!ensureGpuEmitterBuffers(state, emitter.settings.maxParticles))
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
        const auto appendBand = [this, texture = whiteInfo->srv](const ParticleBlendMode blendMode,
            const Vector3& start, const Vector3& end, const Vector3& startSide, const Vector3& endSide,
            const Vector4& startColor, const Vector4& endColor)
        {
            constexpr std::uint32_t bandAppearance = 2;
            if (m_batches.empty() || m_batches.back().mesh != nullptr
                || m_batches.back().appearance != bandAppearance
                || m_batches.back().blendMode != blendMode)
            {
                m_batches.push_back({ .texture = texture, .blendMode = blendMode,
                    .appearance = bandAppearance, .firstInstance = static_cast<std::uint32_t>(m_quadInstances.size()) });
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
                    appendBand(trail.blendMode, trail.start, trail.end, startSide, endSide,
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
                        appendBand(ribbon.blendMode, start.position, end.position,
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
                    const auto resolveTexture = [&](const std::filesystem::path& path, const TextureColorSpace colorSpace,
                        D3D12_GPU_DESCRIPTOR_HANDLE& output, std::uint32_t* width, std::uint32_t* height)
                    {
                        if (path.empty())
                            return true;
                        const bool srgb = colorSpace == TextureColorSpace::SRGB;
                        const auto kind = srgb ? ParticleResourceKind::ColorTexture : ParticleResourceKind::LinearTexture;
                        auto& failures = m_failedTextures[srgb ? 0 : 1];
                        const auto failed = failures.find(path);
                        if (failed == failures.end() || m_frameNumber - failed->second >= 120)
                        {
                            TextureHandle handle;
                            auto& cache = srgb ? m_textureCache : m_linearTextureCache;
                            if (const auto cached = cache.find(path); cached != cache.end())
                                handle = cached->second;
                            if (!textureManager.get(handle))
                            {
                                TextureLoadDesc desc;
                                desc.colorSpace = colorSpace;
                                desc.generateMips = false;
                                handle = textureManager.load(path, desc);
                            }
                            const Texture* const texture = textureManager.get(handle);
                            const TextureResourceInfo* const info = texture ? texture->getResourceInfo() : nullptr;
                            if (info && info->type == TextureType::Texture2D && info->width && info->height)
                            {
                                output = info->srv;
                                if (width) *width = info->width;
                                if (height) *height = info->height;
                                cache[path] = handle;
                                failures.erase(path);
                                ParticleRenderSubmissionQueue::instance().setResourceFailed(path, kind, false);
                                return true;
                            }
                            LOG_ERROR("[ParticleEffect] Particle texture could not be loaded as a 2D texture: {}",
                                String::fromWide(path.c_str()).c_str());
                            failures[path] = m_frameNumber;
                        }
                        ParticleRenderSubmissionQueue::instance().setResourceFailed(path, kind, true);
                        return false;
                    };
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
                if (m_batches.empty() || m_batches.back().texture.ptr != resolved->texture.ptr
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
                        .firstInstance = static_cast<std::uint32_t>(m_quadInstances.size()) };
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
            [](const DrawBatch& batch) { return batch.softDistance > 0.0f; });
        const bool usesRefraction = std::any_of(m_batches.begin(), m_batches.end(),
            [](const DrawBatch& batch) { return batch.refractionPixels != 0.0f; });
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
        if (!m_gpuEmitterSubmissions.empty())
        {
            if (!m_gpuSimulationPipeline.bind(commandList))
                return false;
            for (const ParticleGpuEmitterSubmission& emitter : m_gpuEmitterSubmissions)
            {
                GpuEmitterState& state = *m_gpuEmitterStates.at(emitter.id);
                auto& particles = state.buffers->particles;
                if (!particles.transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
                    return false;
                GpuSimulationConstants constants{
                    { emitter.settings.maxParticles, (emitter.clear || state.needsClear) ? 1u : 0u,
                        emitter.spawnCursor, emitter.spawnCount },
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
                    { emitter.settings.randomInitial ? 1u : 0u, 0u, 0u, 0u },
                };
                native->SetComputeRoot32BitConstants(0, 52, &constants, 0);
                native->SetComputeRootUnorderedAccessView(1, particles.get()->GetGPUVirtualAddress());
                native->Dispatch((emitter.settings.maxParticles + 63) / 64, 1, 1);
                if (!particles.transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
                    return false;
                state.needsClear = false;
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
                if (!resolveGpuTexture(emitter.settings.texturePath, texture))
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
                    Vector4(up.x, up.y, up.z, emitter.settings.initialSizeScale.x),
                    Vector4(0.0f, emitter.settings.initialSizeScale.y, 0.0f, 0.0f),
                };
                native->SetGraphicsRootDescriptorTable(1, texture);
                native->SetGraphicsRoot32BitConstants(2, 1, &appearance, 0);
                native->SetGraphicsRootShaderResourceView(3, buffers.particles.get()->GetGPUVirtualAddress());
                native->SetGraphicsRoot32BitConstants(4, 16, &emitter.worldMatrix._11, 0);
                native->SetGraphicsRoot32BitConstants(5, 16, &drawConstants, 0);
                native->SetGraphicsRootShaderResourceView(6, buffers.drawData.get()->GetGPUVirtualAddress());
                native->ExecuteIndirect(m_gpuDrawSignature.Get(), 1, buffers.drawData.get(), 0, nullptr, 0);
                ++m_gpuEmitterDrawCount;
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

    bool ParticleRenderer::resolveGpuTexture(
        const std::filesystem::path& path, D3D12_GPU_DESCRIPTOR_HANDLE& texture)
    {
        if (path.empty())
            return true;
        const auto failed = m_failedTextures[0].find(path);
        if (failed != m_failedTextures[0].end() && m_frameNumber - failed->second < 120)
        {
            ParticleRenderSubmissionQueue::instance().setResourceFailed(path, ParticleResourceKind::ColorTexture, true);
            return false;
        }
        auto cacheEntry = m_textureCache.find(path);
        TextureHandle handle = cacheEntry == m_textureCache.end() ? TextureHandle{} : cacheEntry->second;
        auto& manager = TextureManager::instance();
        if (!manager.get(handle))
        {
            TextureLoadDesc description;
            description.colorSpace = TextureColorSpace::SRGB;
            description.generateMips = false;
            handle = manager.load(path, description);
        }
        const Texture* const loaded = manager.get(handle);
        const TextureResourceInfo* const info = loaded ? loaded->getResourceInfo() : nullptr;
        if (info == nullptr || info->type != TextureType::Texture2D || info->width == 0 || info->height == 0)
        {
            LOG_ERROR("[ParticleEffect] GPU particle texture could not be loaded as a 2D texture: {}",
                String::fromWide(path.c_str()).c_str());
            m_failedTextures[0][path] = m_frameNumber;
            ParticleRenderSubmissionQueue::instance().setResourceFailed(path, ParticleResourceKind::ColorTexture, true);
            return false;
        }
        texture = info->srv;
        m_textureCache[path] = handle;
        m_failedTextures[0].erase(path);
        ParticleRenderSubmissionQueue::instance().setResourceFailed(path, ParticleResourceKind::ColorTexture, false);
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
