#include "Pch.h"
#include "Terrain\Generation\TerrainGpuNoise.h"
#include "Terrain\Generation\TerrainGeneration.h"
#include "Terrain\Geometry\TerrainLod.h"
#include "UI\RuntimeUI.h"
#include "Graphics\DirectX12\Rendering\Renderer.h"
#include "Assets\Material\MaterialManager.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include "Graphics\Camera\CameraUtils.h"
#include "Graphics\Renderer\ModelRenderSubmission.h"
#include "Graphics\Texture\TextureManager.h"
#include "Core\Profiling\FrameProfiler.h"
#include <imgui.h>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\Math\Transform.h"
#include "Core\Math\Geometry.h"

namespace Engine
{
    namespace
    {
        struct CachedSkinningVertex
        {
            ModelVertex vertex;
            Vector4 previousPosition;
            float positionW;
        };
        static_assert(sizeof(ModelVertex) == 96 && offsetof(ModelVertex, position) == 0
            && offsetof(ModelVertex, normal) == 12
            && offsetof(ModelVertex, tangent) == 24 && offsetof(ModelVertex, bitangent) == 36
            && offsetof(ModelVertex, boneIndices) == 72 && offsetof(ModelVertex, boneWeights) == 80);
        static_assert(offsetof(CachedSkinningVertex, previousPosition) == 96
            && offsetof(CachedSkinningVertex, positionW) == 112 && sizeof(CachedSkinningVertex) == 116);

        /**
         * @brief モデル頂点の入力レイアウト
         * @details
         * - POSITION: float3
         * - NORMAL: float3
         * - TANGENT: float3
         * - BITANGENT: float3
         * - COLOR: float4
         * - TEXCOORD: float2
         * - BLENDINDICES: uint4 (16bit each)
         * - BLENDWEIGHT: float4
         */
        constexpr std::array MODEL_INPUT_LAYOUT = {
            D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(ModelVertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(ModelVertex, normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(ModelVertex, tangent)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(ModelVertex, bitangent)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(ModelVertex, color)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(ModelVertex, texCoord)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, static_cast<UINT>(offsetof(ModelVertex, boneIndices)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(ModelVertex, boneWeights)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 5, DXGI_FORMAT_R32_FLOAT, 1, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        bool colorsEqual(const Color& left, const Color& right) noexcept
        {
            return left.x == right.x && left.y == right.y
                && left.z == right.z && left.w == right.w;
        }

        float halton(const std::uint32_t index, const std::uint32_t base) noexcept
        {
            float value = 0.0f;
            float fraction = 1.0f;
            std::uint32_t remaining = index;
            while (remaining > 0)
            {
                fraction /= static_cast<float>(base);
                value += fraction * static_cast<float>(remaining % base);
                remaining /= base;
            }
            return value;
        }
    }

    bool DX12Renderer::initialize(const HWND hwnd, const std::uint32_t width, const std::uint32_t height,
        const ExecutionMode mode, const bool enableRuntimeUi)
    {
        if (hwnd == nullptr || width == 0 || height == 0)
            return false;

        if (!finalize())
            return false;
        m_mode = mode;
        m_editorOverlayEnabled = mode == ExecutionMode::Editor;
        m_runtimeUiEnabled = enableRuntimeUi;

        const auto rollbackInitialization = [this]
            {
                if (!finalize())
                    LOG_CRITICAL("[DX12] Renderer初期化失敗後のリソース解放に失敗しました");
            };

        DX12DeviceConfig deviceConfig{};
#if defined(_DEBUG)
        deviceConfig.enableDebugLayer = true;
        deviceConfig.enableGpuBasedValidation = true;
        deviceConfig.enableDxgiDebug = true;
#endif
        if (!m_device.initialize(deviceConfig))
            return false;

        if (!m_directQueue.initialize(*m_device.get(), DX12CommandQueueType::DIRECT)
            || !m_directFence.initialize(*m_device.get())
            || !TextureManager::instance().initialize(m_device, m_directQueue, m_directFence)
            || !m_modelGpuCache.initialize(m_device, m_directFence)
            || !m_materialGpuCache.initialize(m_device, m_directFence)
            || !DebugPrimitive::instance().initialize(m_device, m_directFence))
        {
            rollbackInitialization();
            return false;
        }
        if (!m_postProcess.initialize(*m_device.get(), m_directFence,
            TextureManager::instance().getDescriptorHeap()))
        {
            rollbackInitialization();
            return false;
        }

        if (!m_gpuProfiler.initialize(*m_device.get(), *m_directQueue.get(), m_directFence))
        {
            rollbackInitialization();
            return false;
        }

        DX12SwapChainConfig swapChainConfig{};
        swapChainConfig.bufferCount = FRAME_COUNT;
        if (!m_swapChain.initialize(
            *m_device.get(),
            *m_device.getFactory(),
            *m_directQueue.get(),
            hwnd,
            width,
            height,
            swapChainConfig))
        {
            rollbackInitialization();
            return false;
        }

        if (!m_gameRtvHeap.initialize(*m_device.get(), {
                .type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                .capacity = 7,
                .shaderVisible = false,
            })
            || !m_dsvHeap.initialize(*m_device.get(), {
                .type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                .capacity = 2,
                .shaderVisible = false,
                })
                || !createGameRenderTarget(width, height, m_cameraClearColor)
            || !createDepthBuffer(width, height)
                || !createDeferredRenderTargets(width, height)
                || !createShadowMap())
        {
            rollbackInitialization();
            return false;
        }

        for (std::uint32_t frame = 0; frame < FRAME_COUNT; ++frame)
        {
            if (!m_presentationLists[frame].initialize(*m_device.get(), DX12CommandQueueType::DIRECT))
            {
                rollbackInitialization();
                return false;
            }
            if (!m_lightingBuffers[frame].initialize(*m_device.get(), m_directFence,
                (sizeof(LightingConstants) + 255u) & ~std::size_t{255}))
            {
                rollbackInitialization();
                return false;
            }
            if (!m_deferredConstantsBuffers[frame].initialize(*m_device.get(), m_directFence,
                (sizeof(DeferredPassConstants) + 255u) & ~std::size_t{255}))
            {
                rollbackInitialization();
                return false;
            }
            if (!m_commandLists[frame].initialize(*m_device.get(), DX12CommandQueueType::DIRECT)
                || !m_finishCommandLists[frame].initialize(*m_device.get(), DX12CommandQueueType::DIRECT)
                || !m_queryCommandLists[frame].initialize(*m_device.get(), DX12CommandQueueType::DIRECT)
                || !m_deferredLightingCommandLists[frame].initialize(*m_device.get(), DX12CommandQueueType::DIRECT))
            {
                rollbackInitialization();
                return false;
            }
        }

        if (enableRuntimeUi && !m_runtimeUi.initialize(*m_device.get(), *m_directQueue.get()))
        {
            rollbackInitialization();
            return false;
        }
        if (mode == ExecutionMode::Editor)
        {
            m_imguiSystem = std::make_unique<ImGuiSystem>();
            if (!m_imguiSystem->initialize(*m_device.get(), *m_directQueue.get(), hwnd)
                || !m_imguiSystem->updateGameTextureView(*m_device.get(), *m_gameRenderTarget.get()))
            {
                rollbackInitialization();
                return false;
            }
        }

        if (!m_shaderManager.initialize(
            mode == ExecutionMode::Runtime ? ShaderMode::Runtime : ShaderMode::Development,
            "Assets/Shaders",
            [this](ShaderID /*id*/) { m_psoRebuildPending = true; }))
        {
            LOG_ERROR("[DX12] ShaderManager の初期化に失敗しました");
            rollbackInitialization();
            return false;
        }

        const ShaderCompileDesc modelVertexShaderDesc{
            .sourcePath = "Assets/Shaders/Model.hlsl",
            .outputPath = "Assets/Shaders/Compiled/Model_vsMain_vs.cso",
            .entryPoint = "vsMain",
            .stage = ShaderStage::Vertex,
            .shaderModel = ShaderModel::SM_6_0,
            .languageVersion = HlslLanguageVersion::Hlsl2021,
            .debug = true,
            .optimize = false,
        };
        const ShaderCompileDesc modelPixelShaderDesc{
            .sourcePath = "Assets/Shaders/Model.hlsl",
            .outputPath = "Assets/Shaders/Compiled/Model_psMain_ps.cso",
            .entryPoint = "psMain",
            .stage = ShaderStage::Pixel,
            .shaderModel = ShaderModel::SM_6_0,
            .languageVersion = HlslLanguageVersion::Hlsl2021,
            .debug = true,
            .optimize = false,
        };
        m_modelVertexShaderID = m_shaderManager.registerShader(modelVertexShaderDesc);
        ShaderCompileDesc skinningComputeShaderDesc = modelVertexShaderDesc;
        skinningComputeShaderDesc.outputPath = "Assets/Shaders/Compiled/Model_csSkinning_cs.cso";
        skinningComputeShaderDesc.entryPoint = "csSkinning";
        skinningComputeShaderDesc.stage = ShaderStage::Compute;
        m_skinningComputeShaderID = m_shaderManager.registerShader(skinningComputeShaderDesc);
        m_modelPixelShaderID = m_shaderManager.registerShader(modelPixelShaderDesc);
        ShaderCompileDesc alphaTestPixelShaderDesc = modelPixelShaderDesc;
        alphaTestPixelShaderDesc.outputPath = "Assets/Shaders/Compiled/Model_psAlphaTest_ps.cso";
        alphaTestPixelShaderDesc.entryPoint = "psAlphaTest";
        m_alphaTestPixelShaderID = m_shaderManager.registerShader(alphaTestPixelShaderDesc);
        ShaderCompileDesc gbufferPixelShaderDesc = modelPixelShaderDesc;
        gbufferPixelShaderDesc.outputPath = "Assets/Shaders/Compiled/Model_psGBuffer_ps.cso";
        gbufferPixelShaderDesc.entryPoint = "psGBuffer";
        m_gbufferPixelShaderID = m_shaderManager.registerShader(gbufferPixelShaderDesc);
        gbufferPixelShaderDesc.outputPath = "Assets/Shaders/Compiled/Model_psGBufferAlphaTest_ps.cso";
        gbufferPixelShaderDesc.entryPoint = "psGBufferAlphaTest";
        m_gbufferAlphaTestPixelShaderID = m_shaderManager.registerShader(gbufferPixelShaderDesc);
        ShaderCompileDesc depthAlphaTestPixelShaderDesc = modelPixelShaderDesc;
        depthAlphaTestPixelShaderDesc.outputPath = "Assets/Shaders/Compiled/Model_psDepthAlphaTest_ps.cso";
        depthAlphaTestPixelShaderDesc.entryPoint = "psDepthAlphaTest";
        m_depthAlphaTestPixelShaderID = m_shaderManager.registerShader(depthAlphaTestPixelShaderDesc);
        const ShaderCompileDesc deferredVertexShaderDesc{
            .sourcePath = "Assets/Shaders/Deferred.hlsl",
            .outputPath = "Assets/Shaders/Compiled/Deferred_vsMain_vs.cso",
            .entryPoint = "vsMain",
            .stage = ShaderStage::Vertex,
            .shaderModel = ShaderModel::SM_6_0,
            .languageVersion = HlslLanguageVersion::Hlsl2021,
            .debug = true,
            .optimize = false,
        };
        m_deferredVertexShaderID = m_shaderManager.registerShader(deferredVertexShaderDesc);
        const ShaderCompileDesc deferredComputeShaderDesc{
            .sourcePath = "Assets/Shaders/Deferred.hlsl",
            .outputPath = "Assets/Shaders/Compiled/Deferred_csLighting_cs.cso",
            .entryPoint = "csLighting",
            .stage = ShaderStage::Compute,
            .shaderModel = ShaderModel::SM_6_0,
            .languageVersion = HlslLanguageVersion::Hlsl2021,
            .debug = true,
            .optimize = false,
        };
        m_deferredLightingComputeShaderID = m_shaderManager.registerShader(deferredComputeShaderDesc);
        ShaderCompileDesc deferredPixelShaderDesc = deferredComputeShaderDesc;
        deferredPixelShaderDesc.outputPath = "Assets/Shaders/Compiled/Deferred_psDebug_ps.cso";
        deferredPixelShaderDesc.entryPoint = "psDebug";
        deferredPixelShaderDesc.stage = ShaderStage::Pixel;
        m_deferredDebugPixelShaderID = m_shaderManager.registerShader(deferredPixelShaderDesc);
        ShaderCompileDesc postProcessDesc = deferredVertexShaderDesc;
        postProcessDesc.sourcePath = "Assets/Shaders/PostProcess.hlsl";
        postProcessDesc.outputPath = "Assets/Shaders/Compiled/PostProcess_vsMain_vs.cso";
        m_postProcessVertexShaderID = m_shaderManager.registerShader(postProcessDesc);
        postProcessDesc.stage = ShaderStage::Pixel;
        postProcessDesc.entryPoint = "psToneMapping";
        postProcessDesc.outputPath = "Assets/Shaders/Compiled/PostProcess_psToneMapping_ps.cso";
        m_postProcessToneMappingShaderID = m_shaderManager.registerShader(postProcessDesc);
        postProcessDesc.entryPoint = "psOutput";
        postProcessDesc.outputPath = "Assets/Shaders/Compiled/PostProcess_psOutput_ps.cso";
        m_postProcessOutputShaderID = m_shaderManager.registerShader(postProcessDesc);
        postProcessDesc.sourcePath = "Assets/Shaders/PostProcessEffects.hlsl";
        for (std::size_t index = 0; index < POST_PROCESS_BUILTIN_ENTRIES.size(); ++index)
        {
            postProcessDesc.entryPoint = POST_PROCESS_BUILTIN_ENTRIES[index];
            postProcessDesc.outputPath = std::filesystem::path("Assets\\Shaders\\Compiled")
                / (std::string("PostProcessEffects_") + POST_PROCESS_BUILTIN_ENTRIES[index] + "_ps.cso");
            m_postProcessBuiltinShaderIDs[index] = m_shaderManager.registerShader(postProcessDesc);
        }
        const ShaderCompileDesc debugVertexShaderDesc{
            .sourcePath = "Assets/Shaders/DebugPrimitive.hlsl",
            .outputPath = "Assets/Shaders/Compiled/DebugPrimitive_vsMain_vs.cso",
            .entryPoint = "vsMain",
            .stage = ShaderStage::Vertex,
            .shaderModel = ShaderModel::SM_6_0,
            .languageVersion = HlslLanguageVersion::Hlsl2021,
            .debug = true,
            .optimize = false,
        };
        const ShaderCompileDesc debugPixelShaderDesc{
            .sourcePath = "Assets/Shaders/DebugPrimitive.hlsl",
            .outputPath = "Assets/Shaders/Compiled/DebugPrimitive_psMain_ps.cso",
            .entryPoint = "psMain",
            .stage = ShaderStage::Pixel,
            .shaderModel = ShaderModel::SM_6_0,
            .languageVersion = HlslLanguageVersion::Hlsl2021,
            .debug = true,
            .optimize = false,
        };
        m_debugVertexShaderID = m_shaderManager.registerShader(debugVertexShaderDesc);
        ShaderCompileDesc debugShapeVertexShaderDesc = debugVertexShaderDesc;
        debugShapeVertexShaderDesc.outputPath = "Assets/Shaders/Compiled/DebugPrimitive_vsShape_vs.cso";
        debugShapeVertexShaderDesc.entryPoint = "vsShape";
        m_debugShapeVertexShaderID = m_shaderManager.registerShader(debugShapeVertexShaderDesc);
        m_debugPixelShaderID = m_shaderManager.registerShader(debugPixelShaderDesc);
        const ShaderCompileDesc hizShaderDesc{
            .sourcePath = "Assets/Shaders/HiZ.hlsl",
            .outputPath = "Assets/Shaders/Compiled/HiZ_csMain_cs.cso",
            .entryPoint = "csMain",
            .stage = ShaderStage::Compute,
            .debug = true,
            .optimize = true,
        };
        m_hizShaderID = m_shaderManager.registerShader(hizShaderDesc);
        ShaderCompileDesc occlusionShaderDesc = hizShaderDesc;
        occlusionShaderDesc.sourcePath = "Assets/Shaders/Occlusion.hlsl";
        occlusionShaderDesc.outputPath = "Assets/Shaders/Compiled/Occlusion_csMain_cs.cso";
        m_occlusionShaderID = m_shaderManager.registerShader(occlusionShaderDesc);

        if (!m_shaderManager.loadAll())
        {
            LOG_ERROR("[DX12] Shader CSO のロードに失敗しました");
            rollbackInitialization();
            return false;
        }

        if (!rebuildGraphicsPipelines())
        {
            rollbackInitialization();
            return false;
        }
        if (!TerrainGpuNoise::instance().initialize(*m_device.get()))
        {
            rollbackInitialization();
            return false;
        }

        m_frameFenceValues.fill(0);
        m_lastSubmittedFenceValue = 0;
        m_renderWidth = width;
        m_renderHeight = height;
        m_requestedGameWidth = width;
        m_requestedGameHeight = height;
        return true;
    }

    bool DX12Renderer::finalize()
    {
        TerrainGenerationQueue::instance().shutdown();
        if (!TerrainGpuNoise::instance().shutdown())
            return false;
        if (m_lastSubmittedFenceValue != 0 && !m_directFence.waitOnCpu(m_lastSubmittedFenceValue))
            return false;
        if (!m_postProcess.finalize())
            return false;
        m_postProcessRecorded = false;
        m_runtimeUi.finalize();
        for (auto& list : m_presentationLists)
            list.finalize();

        if (m_imguiSystem != nullptr)
        {
            m_imguiSystem->finalize();
            m_imguiSystem.reset();
        }

        if (!m_swapChain.finalize(m_directFence, m_lastSubmittedFenceValue))
            return false;
        if (!m_gpuProfiler.finalize())
            return false;

        m_depthBuffer.finalize();
        for (DX12Resource& target : m_gbufferTargets)
            target.finalize();
        m_deferredLightingTarget.finalize();
        for (auto& buffer : m_lightingBuffers)
            if (!buffer.finalize())
                return false;
        for (auto& buffer : m_deferredConstantsBuffers)
            if (!buffer.finalize())
                return false;
        for (auto& view : m_gbufferShaderViews)
        {
            if (view)
            {
                if (!TextureManager::instance().getDescriptorHeap().release(view->cpu.index))
                    LOG_ERROR("[Renderer] Failed to release a G-Buffer SRV descriptor.");
                view.reset();
            }
        }
        if (m_depthShaderView)
        {
            if (!TextureManager::instance().getDescriptorHeap().release(m_depthShaderView->cpu.index))
                LOG_ERROR("[Renderer] Failed to release the depth SRV descriptor.");
            m_depthShaderView.reset();
        }
        if (m_deferredLightingShaderView)
        {
            if (!TextureManager::instance().getDescriptorHeap().release(m_deferredLightingShaderView->cpu.index))
                LOG_ERROR("[Renderer] Failed to release the HDR lighting SRV descriptor.");
            m_deferredLightingShaderView.reset();
        }
        if (m_deferredLightingUnorderedView)
        {
            if (!TextureManager::instance().getDescriptorHeap().release(m_deferredLightingUnorderedView->cpu.index))
                LOG_ERROR("[Renderer] Failed to release the HDR lighting UAV descriptor.");
            m_deferredLightingUnorderedView.reset();
        }
        if (m_shadowSrv)
        {
            TextureManager::instance().getDescriptorHeap().release(m_shadowSrv->cpu.index);
            m_shadowSrv.reset();
        }
        m_gameRenderTarget.finalize();
        m_shadowMap.finalize();
        m_gameRtvHeap.finalize();
        m_dsvHeap.finalize();
        m_gameRenderTargetView = {};
        m_gbufferRenderTargetViews = {};
        m_deferredLightingRenderTargetView = {};
        m_depthStencilView = {};
        m_shadowDepthStencilView = {};

        ModelRenderSubmissionQueue::instance().clear();
        m_modelRenderQueue.clear();
        if (!DebugPrimitive::instance().finalize())
            return false;
        if (!m_modelGpuCache.finalize())
            return false;
        for (auto& frameBuffers : m_skinningPaletteBuffers)
        {
            for (auto& buffer : frameBuffers)
                if (!buffer->finalize())
                    return false;
            frameBuffers.clear();
        }
        m_skinningPaletteBufferCursors.fill(0);
        for (auto& frameBuffers : m_cachedSkinningBuffers)
            frameBuffers.clear();
        m_cachedSkinningBufferCursors.fill(0);
        for (DX12UploadBuffer& buffer : m_modelInstanceBuffers)
            if (!buffer.finalize())
                return false;
        m_modelInstanceConstants.clear();
        m_previousObjectWorldMatrices.clear();
        m_currentObjectWorldMatrices.clear();
        m_previousSkinningPalettes.clear();
        m_currentSkinningPalettes.clear();
        m_animatedMeshBounds.clear();
        for (DX12GpuOcclusionCulling& culling : m_occlusionCulling)
            if (!culling.finalize())
                return false;
        if (!m_materialGpuCache.finalize())
            return false;
        if (!TextureManager::instance().finalize())
            return false;

        m_transparentModelPipeline.finalize();
        m_deferredTransparentModelPipeline.finalize();
        m_gbufferAlphaTestModelPipeline.finalize();
        m_gbufferModelPipeline.finalize();
        m_deferredLightingPipeline.finalize();
        m_deferredDebugPipeline.finalize();
        m_alphaTestModelPipeline.finalize();
        m_depthOnlyModelPipeline.finalize();
        m_depthAlphaTestModelPipeline.finalize();
        m_modelPipeline.finalize();
        m_skinningPipeline.finalize();
        m_occlusionPipeline.finalize();
        m_hizPipeline.finalize();
        m_shaderManager.shutdown();
        m_modelVertexShaderID = 0;
        m_skinningComputeShaderID = 0;
        m_modelPixelShaderID = 0;
        m_alphaTestPixelShaderID = 0;
        m_depthAlphaTestPixelShaderID = 0;
        m_gbufferPixelShaderID = 0;
        m_gbufferAlphaTestPixelShaderID = 0;
        m_deferredVertexShaderID = 0;
        m_deferredLightingComputeShaderID = 0;
        m_deferredDebugPixelShaderID = 0;
        m_postProcessVertexShaderID = 0;
        m_postProcessToneMappingShaderID = 0;
        m_postProcessOutputShaderID = 0;
        m_postProcessBuiltinShaderIDs.fill(0);
        m_debugVertexShaderID = 0;
        m_debugShapeVertexShaderID = 0;
        m_debugPixelShaderID = 0;
        m_occlusionShaderID = 0;
        m_hizShaderID = 0;
        m_psoRebuildPending = false;
        for (DX12CommandList& commandList : m_commandLists)
            commandList.finalize();
        for (DX12CommandList& commandList : m_finishCommandLists)
            commandList.finalize();
        for (DX12CommandList& commandList : m_queryCommandLists)
            commandList.finalize();
        for (DX12CommandList& commandList : m_deferredLightingCommandLists)
            commandList.finalize();
        for (DX12ModelCommandRecorder& recorder : m_modelCommandRecorders)
            recorder.finalize();
        m_preparedModelDraws.clear();
        m_executionLists.clear();
        m_queryListRecorded = false;
        m_renderFailed = false;

        m_frameFenceValues.fill(0);
        m_lastSubmittedFenceValue = 0;
        m_renderWidth = 0;
        m_renderHeight = 0;
        m_deferredFrameActive = false;
        m_deferredPassRecorded = false;
        m_postProcessRecorded = false;
        m_deferredDebugView = 0;
        m_cameraPostProcess = {};
        m_frustum.reset();
        m_shadowViewProjection.reset();
        m_viewProjection = Matrix::Identity;
        m_inverseViewProjection = Matrix::Identity;
        m_previousViewProjection = Matrix::Identity;
        m_lastSubmittedViewProjection = Matrix::Identity;
        m_previousRenderCamera = {};
        m_currentRenderCamera = {};
        m_currentRenderSize = Vector2::Zero;
        m_previousRenderSize = Vector2::Zero;
        m_temporalAAJitterIndex = 0;
        m_hasPreviousRenderView = false;
        m_temporalAAActive = false;
        m_cameraPosition = Vector3::Zero;
        m_cameraNearClip = 0.1f;
        m_cameraFarClip = 1000.0f;
        m_cameraForward = Vector3::UnitZ;
        m_cameraViewport = {};
        m_cameraClearMode = CameraClearMode::SolidColor;
        m_cameraClearColor = Color(0.08f, 0.16f, 0.24f, 1.0f);
        m_gameRenderTargetClearColor = m_cameraClearColor;
        m_cameraCullingMask = UINT32_MAX;
        CameraRenderSubmissionQueue::instance().clear();
        m_statistics = {};
        m_frameStatistics = {};
        m_directFence.finalize();
        m_directQueue.finalize();
        m_device.finalize();
        return true;
    }

    bool DX12Renderer::render()
    {
        if (m_renderFailed)
        {
            LOG_ERROR("[Renderer] Rendering stopped after a failed frame; reinitialize the renderer before reuse.");
            return false;
        }
        m_renderFailed = true;
        const bool succeeded = renderFrame();
        m_renderFailed = !succeeded;
        return succeeded;
    }

    bool DX12Renderer::renderFrame()
    {
        if (m_device.get() == nullptr
            || (m_mode == ExecutionMode::Editor && (m_imguiSystem == nullptr || !m_imguiSystem->isInitialized())))
            return false;

        RenderView submittedView;
        const bool hasCameraView = CameraRenderSubmissionQueue::instance().consume(submittedView);
        if (hasCameraView)
        {
            setRenderView(submittedView);
        }
        else
        {
            m_viewProjection = Matrix::Identity;
            m_inverseViewProjection = Matrix::Identity;
            m_cameraPosition = Vector3::Zero;
            m_cameraForward = Vector3::UnitZ;
            m_cameraNearClip = 0.1f;
            m_cameraFarClip = 1000.0f;
            m_frustum.reset();
            m_shadowViewProjection.reset();
            m_cameraViewport = {};
            m_cameraClearMode = CameraClearMode::SolidColor;
            m_cameraClearColor = Color(0.08f, 0.16f, 0.24f, 1.0f);
            m_cameraCullingMask = 0;
            m_cameraPostProcess = {};
            m_currentRenderCamera = {};
            m_currentRenderSize = Vector2::Zero;
            m_previousViewProjection = Matrix::Identity;
            m_hasPreviousRenderView = false;
            m_temporalAAActive = false;
            m_temporalAAJitterIndex = 0;
            m_previousObjectWorldMatrices.clear();
            m_previousSkinningPalettes.clear();
            m_currentSkinningPalettes.clear();
        }

        m_deferredFrameActive = m_deferredRenderingEnabled
            && m_cameraClearMode != CameraClearMode::Nothing;
        m_deferredPassRecorded = false;
        m_postProcessRecorded = false;
        m_postProcess.resetStatistics();
        const bool clearsColor = m_cameraClearMode == CameraClearMode::SolidColor
            || m_cameraClearMode == CameraClearMode::Skybox;
        const bool renderTargetNeedsResize = m_requestedGameWidth != 0 && m_requestedGameHeight != 0
            && (m_requestedGameWidth != m_renderWidth || m_requestedGameHeight != m_renderHeight);
        const bool clearValueNeedsUpdate = clearsColor
            && !colorsEqual(m_cameraClearColor, m_gameRenderTargetClearColor);
        if (renderTargetNeedsResize || clearValueNeedsUpdate)
        {
            if (m_lastSubmittedFenceValue != 0 && !m_directFence.waitOnCpu(m_lastSubmittedFenceValue))
                return false;
            if (renderTargetNeedsResize && !m_postProcess.releaseTargets())
                return false;
            if (!createGameRenderTarget(m_requestedGameWidth, m_requestedGameHeight, m_cameraClearColor)
                || (m_imguiSystem != nullptr
                    && !m_imguiSystem->updateGameTextureView(*m_device.get(), *m_gameRenderTarget.get())))
            {
                return false;
            }
            if (renderTargetNeedsResize
                && (!createDepthBuffer(m_requestedGameWidth, m_requestedGameHeight)
                    || !createDeferredRenderTargets(m_requestedGameWidth, m_requestedGameHeight)))
                return false;
            m_renderWidth = m_requestedGameWidth;
            m_renderHeight = m_requestedGameHeight;
        }

        m_shaderManager.processHotReload();
        m_modelGpuCache.collectGarbage();
        m_materialGpuCache.collectGarbage();

        if (m_psoRebuildPending)
        {
            if (m_lastSubmittedFenceValue != 0 && !m_directFence.waitOnCpu(m_lastSubmittedFenceValue))
                return false;
            if (rebuildGraphicsPipelines())
            {
                LOG_INFO("[Renderer] Graphics Pipelines and Material GPU Cache successfully rebuilt via Shader Hot Reload.");
            }
            else
            {
                LOG_ERROR("[Renderer] Failed to rebuild Graphics Pipelines during Shader Hot Reload.");
            }
            m_psoRebuildPending = false;
        }

        m_lighting = LightingFrame::instance().forCamera(m_cameraPosition, m_cameraForward, m_viewProjection);
        const auto& lightShafts = m_lighting.lightShafts;
        m_cameraPostProcess.lightShafts = {
            lightShafts.options.x > 0.5f,
            lightShafts.options.y > 0.5f,
            Vector2(lightShafts.screenPosition.x, lightShafts.screenPosition.y),
            Vector3(lightShafts.tint.x, lightShafts.tint.y, lightShafts.tint.z),
            lightShafts.options.z,
            lightShafts.options.w,
            lightShafts.tint.w,
        };
        if (hasCameraView && LightingFrame::instance().hasShadow())
            m_shadowViewProjection = m_lighting.shadowViewProjection;
        else
            m_shadowViewProjection.reset();
        std::vector<ModelHandle> usedModels;
        std::vector<MaterialHandle> usedMaterials;
        buildModelRenderQueue(usedModels, usedMaterials);
        if (m_imguiSystem != nullptr)
            m_imguiSystem->beginFrame(&m_shaderManager,
            m_imguiSystem->getGameTextureId(), m_requestedGameWidth, m_requestedGameHeight, [this]
            {
                if (ImGui::Begin("Renderer Statistics"))
                {
                    ImGui::Text("Frustum-visible Objects: %u", m_statistics.visibleObjects);
                    ImGui::Checkbox("Deferred Rendering", &m_deferredRenderingEnabled);
                    if (m_deferredFrameActive)
                    {
                        static constexpr const char* DEFERRED_VIEWS[] = {
                            "Final Lighting", "Albedo", "Normal", "Metallic", "Roughness",
                            "Material AO", "Emission", "Depth", "Shadow Map"
                        };
                        ImGui::Combo("G-Buffer View", &m_deferredDebugView,
                            DEFERRED_VIEWS, static_cast<int>(std::size(DEFERRED_VIEWS)));
                    }
                    else
                    {
                        ImGui::TextUnformatted("Forward rendering is active.");
                    }
                    ImGui::Text("Frustum-culled Objects: %u", m_statistics.culledObjects);
                    ImGui::Text("Terrain chunks: %u visible, %u culled, %u shadow",
                        m_statistics.terrainVisibleChunks, m_statistics.terrainCulledChunks, m_statistics.terrainShadowChunks);
                    ImGui::Text("Terrain triangles: %llu / %llu full",
                        static_cast<unsigned long long>(m_statistics.terrainTriangles),
                        static_cast<unsigned long long>(m_statistics.terrainFullTriangles));
                    ImGui::Text("Render Items: %u", m_statistics.renderItemCount);
                    ImGui::Text("Submitted Direct Draw Calls: %u", m_statistics.drawCallCount);
                    ImGui::Text("Submitted ExecuteIndirect Calls: %u", m_statistics.indirectCallCount);
                    ImGui::Text("Submitted Model Batches: %u", m_statistics.batchCount);
                    ImGui::Text("Submitted Model Instances (all passes): %u", m_statistics.instanceCount);
                    ImGui::Checkbox("Model Instancing", &m_enableModelInstancing);
                    ImGui::Checkbox("Parallel Model Recording", &m_enableParallelModelRecording);
                    ImGui::Text("Model Command Lists: %u", m_statistics.modelCommandListCount);
                    ImGui::Text("Model Recording Threads: %u", m_statistics.modelRecordingThreadCount);
                    ImGui::Checkbox("GPU Occlusion Culling", &m_enableOcclusionCulling);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Same-frame Hi-Z culling per instance, including current-pose animation bounds. Invalid bounds stay visible. Depth, shadows and transparent draws are unchanged.");
                    ImGui::Text("Hi-Z Candidate Batches (last submitted frame): %u", m_statistics.occlusionBatchCount);
                    const auto& completed = m_statistics.completedOcclusion;
                    ImGui::Text("Executed Indirect Draws (completed slot): %u / %u",
                        completed.batches - completed.occludedBatches, completed.batches);
                    ImGui::Text("Occluded Color Batches (completed slot): %u", completed.occludedBatches);
                    ImGui::Text("Occluded Color Instances (completed slot): %u / %u",
                        completed.occludedInstances, completed.instances);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Completed GPU results are statistics only, not current-frame object counts. Instances count per submesh. A zero command count executes no color Draw.");
                    ImGui::Text("PSO Switches: %u", m_statistics.psoSwitchCount);
                    ImGui::Text("Material Switches: %u", m_statistics.materialSwitchCount);
                    ImGui::Text("Vertex Buffer Switches: %u", m_statistics.vertexBufferSwitchCount);
                    ImGui::Text("Index Buffer Switches: %u", m_statistics.indexBufferSwitchCount);
                }
                ImGui::End();
            });
        const std::uint32_t frameIndex = m_swapChain.getCurrentBackBufferIndex();
        if (frameIndex >= FRAME_COUNT)
        {
            LOG_ERROR("[DX12] 不正な Back Buffer Index です: {}", frameIndex);
            return false;
        }

        {
            FrameProfiler::ScopedCpuTimer profile(FrameProfileMetric::RenderFenceWait);
            const std::uint64_t frameFenceValue = m_frameFenceValues[frameIndex];
            if (frameFenceValue != 0 && !m_directFence.isComplete(frameFenceValue)
                && !m_directFence.waitOnCpu(frameFenceValue))
            {
                return false;
            }
        }
        if (!m_occlusionCulling[frameIndex].collectCompletedResults(m_frameStatistics.completedOcclusion))
            return false;
        bool gpuMeasurementAvailable = false;
        std::uint64_t gpuFrameId = 0;
        double gpuMilliseconds = 0.0;
        if (!m_gpuProfiler.collect(frameIndex, gpuMeasurementAvailable, gpuFrameId, gpuMilliseconds))
            return false;
        if (gpuMeasurementAvailable
            && !FrameProfiler::instance().recordGpuTime(gpuFrameId, gpuMilliseconds))
        {
            return false;
        }

        const Viewport pixelViewport = CameraUtils::toPixelViewport(m_cameraViewport,
            Vector2(static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight)));
        m_deferredConstants = {
            .inverseViewProjection = m_inverseViewProjection,
            .viewport = Vector4(pixelViewport.x, pixelViewport.y, pixelViewport.width, pixelViewport.height),
            .renderSize = Vector4(static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight),
                m_cameraClearMode == CameraClearMode::Skybox ? 1.0f : 0.0f, 0.0f),
            .backgroundColor = Vector4(m_cameraClearColor.x, m_cameraClearColor.y,
                m_cameraClearColor.z, m_cameraClearColor.w),
            .clipAndDebug = Vector4(m_cameraNearClip, m_cameraFarClip,
                static_cast<float>(std::clamp(m_deferredDebugView, 0, 8)), 0.0f),
        };
        DX12CommandList& commandList = m_commandLists[frameIndex];
        if (!m_lightingBuffers[frameIndex].write(std::as_bytes(std::span(&m_lighting, 1)))
            || (m_deferredFrameActive
                && !m_deferredConstantsBuffers[frameIndex].write(
                    std::as_bytes(std::span(&m_deferredConstants, 1)))))
            return false;
        DX12Resource* const backBuffer = m_swapChain.getCurrentBackBuffer();
        if (backBuffer == nullptr || !commandList.begin(m_directFence)
            || !backBuffer->transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET))
        {
            return false;
        }
        if (m_deferredFrameActive)
        {
            for (DX12Resource& target : m_gbufferTargets)
                if (!target.transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET))
                    return false;
        }
        else if (!m_gameRenderTarget.transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET))
        {
            return false;
        }
        if (!m_gpuProfiler.beginFrame(frameIndex, FrameProfiler::instance().currentFrameId(), commandList))
            return false;

        ID3D12GraphicsCommandList* nativeCommandList = commandList.getForRecording();
        const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = m_gameRenderTargetView.native;
        const D3D12_VIEWPORT viewport{ pixelViewport.x, pixelViewport.y,
            pixelViewport.width, pixelViewport.height, pixelViewport.minDepth, pixelViewport.maxDepth };
        const D3D12_RECT scissorRect{
            static_cast<LONG>(std::floor(viewport.TopLeftX)),
            static_cast<LONG>(std::floor(viewport.TopLeftY)),
            static_cast<LONG>(std::ceil(viewport.TopLeftX + viewport.Width)),
            static_cast<LONG>(std::ceil(viewport.TopLeftY + viewport.Height))
        };
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 5> gbufferViews{};
        for (std::size_t index = 0; index < gbufferViews.size(); ++index)
            gbufferViews[index] = m_gbufferRenderTargetViews[index].native;
        if (m_deferredFrameActive)
        {
            nativeCommandList->OMSetRenderTargets(static_cast<UINT>(gbufferViews.size()),
                gbufferViews.data(), FALSE, &m_depthStencilView.native);
        }
        else
        {
            nativeCommandList->OMSetRenderTargets(1, &renderTargetView, FALSE, &m_depthStencilView.native);
        }
        nativeCommandList->RSSetViewports(1, &viewport);
        nativeCommandList->RSSetScissorRects(1, &scissorRect);
        if (m_deferredFrameActive)
        {
            constexpr float CLEAR_COLOR[4]{};
            constexpr float CLEAR_VELOCITY[4]{ 0.0f, 0.0f, 1.0f, 1.0f };
            for (std::size_t index = 0; index < m_gbufferRenderTargetViews.size(); ++index)
                nativeCommandList->ClearRenderTargetView(m_gbufferRenderTargetViews[index].native,
                    index == 4 ? CLEAR_VELOCITY : CLEAR_COLOR, 0, nullptr);
        }
        else if (m_cameraClearMode == CameraClearMode::SolidColor || m_cameraClearMode == CameraClearMode::Skybox)
        {
            nativeCommandList->ClearRenderTargetView(renderTargetView, &m_cameraClearColor.x, 1, &scissorRect);
        }
        if (m_cameraClearMode != CameraClearMode::Nothing)
            nativeCommandList->ClearDepthStencilView(
                m_depthStencilView.native, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &scissorRect);
        m_skinningPaletteBufferCursors[frameIndex] = 0;
        if (!renderModelQueue(commandList, frameIndex))
            return false;

        DX12CommandList& finishList = m_finishCommandLists[frameIndex];
        if (!finishList.begin(m_directFence))
            return false;
        if (m_deferredFrameActive)
        {
            if (m_deferredDebugView != 0)
            {
                if (!recordDeferredDebug(finishList, frameIndex))
                    return false;
            }
            else
            {
                const PostProcessImage input{
                    &m_deferredLightingTarget, *m_deferredLightingShaderView->gpu, m_deferredLightingRenderTargetView };
                const PostProcessImage output{ &m_gameRenderTarget, {}, m_gameRenderTargetView };
                const PostProcessImage depth{ &m_depthBuffer, *m_depthShaderView->gpu, {} };
                const PostProcessImage velocity{ &m_gbufferTargets[4], *m_gbufferShaderViews[4]->gpu, {} };
                if (!m_postProcess.record(finishList, input, output, viewport,
                    m_cameraPostProcess, &depth, &velocity))
                    return false;
                m_postProcessRecorded = true;
            }
        }
        nativeCommandList = finishList.getForRecording();
        nativeCommandList->OMSetRenderTargets(1, &renderTargetView, FALSE, &m_depthStencilView.native);
        nativeCommandList->RSSetViewports(1, &viewport);
        nativeCommandList->RSSetScissorRects(1, &scissorRect);
        if (hasCameraView && m_mode == ExecutionMode::Editor && m_editorOverlayEnabled)
            DebugPrimitive::instance().drawGrid(Vector3::Zero, 20.0f, 20.0f, 1.0f);
        const CameraFrustum debugFrustum = hasCameraView
            ? CameraUtils::buildWorldFrustum(m_viewProjection, Matrix::Identity) : CameraFrustum{};
        if (!DebugPrimitive::instance().render(finishList, frameIndex, m_viewProjection, debugFrustum, hasCameraView))
            return false;

        const bool hasUi = !RuntimeUI::instance().draws().empty();
        if (hasUi && !m_runtimeUiEnabled)
        {
            LOG_ERROR("[Renderer] Runtime UI draws were submitted while Runtime UI is disabled.");
            return false;
        }
        DX12CommandList* presentation = &finishList;
        if (hasUi)
        {
            if (!finishList.close())
                return false;
            m_executionLists.push_back(finishList.getForExecution());
            if (!m_directQueue.execute(m_executionLists))
                return false;
            const auto sceneFence = m_directFence.signal(*m_directQueue.get());
            if (sceneFence == 0)
                return false;
            m_frameFenceValues[frameIndex] = sceneFence;
            m_lastSubmittedFenceValue = sceneFence;
            if (!markSceneSubmitted(frameIndex, sceneFence, usedModels, usedMaterials))
                return false;
            const bool uiSucceeded = m_runtimeUi.render(*m_gameRenderTarget.get(), RuntimeUI::instance().draws());
            const auto uiFence = m_directFence.signal(*m_directQueue.get());
            if (uiFence == 0)
                return false;
            m_frameFenceValues[frameIndex] = uiFence;
            m_lastSubmittedFenceValue = uiFence;
            if (!uiSucceeded)
                return false;
            presentation = &m_presentationLists[frameIndex];
            if (!presentation->begin(m_directFence))
                return false;
            nativeCommandList = presentation->getForRecording();
            m_executionLists.clear();
        }
        if (m_mode == ExecutionMode::Runtime)
        {
            if (!m_gameRenderTarget.transition(*presentation, D3D12_RESOURCE_STATE_COPY_SOURCE)
                || !backBuffer->transition(*presentation, D3D12_RESOURCE_STATE_COPY_DEST))
                return false;
            nativeCommandList->CopyResource(backBuffer->get(), m_gameRenderTarget.get());
            if (!m_gameRenderTarget.transition(*presentation, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
        }
        else
        {
            if (!m_gameRenderTarget.transition(*presentation, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
            const D3D12_CPU_DESCRIPTOR_HANDLE backBufferView = m_swapChain.getCurrentRtv().native;
            nativeCommandList->OMSetRenderTargets(1, &backBufferView, FALSE, nullptr);
            m_imguiSystem->render(*nativeCommandList);
        }

        if (!backBuffer->transition(*presentation, D3D12_RESOURCE_STATE_PRESENT)
            || !m_gpuProfiler.endFrame(frameIndex, *presentation)
            || !presentation->close())
            return false;

        ID3D12CommandList* const nativeExecutionList = presentation->getForExecution();
        if (nativeExecutionList == nullptr)
            return false;

        m_executionLists.push_back(nativeExecutionList);
        bool presentSucceeded = false;
        std::uint64_t submittedFenceValue = 0;
        {
            FrameProfiler::ScopedCpuTimer profile(FrameProfileMetric::SubmitAndPresent);
            if (!m_directQueue.execute(m_executionLists))
                return false;
            presentSucceeded = m_swapChain.present(true);
            if (!presentSucceeded)
                m_device.logDeviceRemovedReason();
            submittedFenceValue = m_directFence.signal(*m_directQueue.get());
        }
        if (submittedFenceValue == 0)
        {
            m_device.logDeviceRemovedReason();
            return false;
        }

        // Keep shutdown safe even if resource bookkeeping below fails.
        m_frameFenceValues[frameIndex] = submittedFenceValue;
        m_lastSubmittedFenceValue = submittedFenceValue;
        if (!presentation->markSubmitted(submittedFenceValue)
            || !m_gpuProfiler.markSubmitted(frameIndex, submittedFenceValue)
            || (!hasUi && !markSceneSubmitted(frameIndex, submittedFenceValue, usedModels, usedMaterials)))
            return false;
        m_statistics = m_frameStatistics;
        return presentSucceeded;
    }

    bool DX12Renderer::markSceneSubmitted(const std::uint32_t frameIndex, const std::uint64_t submittedFenceValue,
        const std::span<const ModelHandle> usedModels, const std::span<const MaterialHandle> usedMaterials)
    {
        if (m_postProcessRecorded && !m_postProcess.markSubmitted(submittedFenceValue))
            return false;
        if (!m_commandLists[frameIndex].markSubmitted(submittedFenceValue)
            || !m_lightingBuffers[frameIndex].markUsed(submittedFenceValue)
            || !m_finishCommandLists[frameIndex].markSubmitted(submittedFenceValue)
            || !m_modelCommandRecorders[frameIndex].markSubmitted(submittedFenceValue)
            || (m_queryListRecorded
                && !m_queryCommandLists[frameIndex].markSubmitted(submittedFenceValue))
            || (m_deferredPassRecorded
                && (!m_deferredConstantsBuffers[frameIndex].markUsed(submittedFenceValue)
                    || !m_deferredLightingCommandLists[frameIndex].markSubmitted(submittedFenceValue))))
            return false;
        if (!DebugPrimitive::instance().markFrameUsed(frameIndex, submittedFenceValue))
            return false;
        for (const ModelHandle handle : usedModels)
        {
            if (!m_modelGpuCache.markUsed(handle, submittedFenceValue))
                return false;
        }
        for (const MaterialHandle handle : usedMaterials)
        {
            if (!m_materialGpuCache.markUsed(handle, submittedFenceValue))
                return false;
        }
        for (std::size_t index = 0; index < m_skinningPaletteBufferCursors[frameIndex]; ++index)
        {
            if (!m_skinningPaletteBuffers[frameIndex][index]->markUsed(submittedFenceValue))
                return false;
        }
        if (!m_modelRenderQueue.getItems().empty()
            && !m_modelInstanceBuffers[frameIndex].markUsed(submittedFenceValue))
            return false;
        if (!m_occlusionCulling[frameIndex].markSubmitted(submittedFenceValue))
            return false;

        m_lastSubmittedViewProjection = m_viewProjection;
        m_previousRenderCamera = m_currentRenderCamera;
        m_previousRenderSize = m_currentRenderSize;
        m_hasPreviousRenderView = m_currentRenderCamera.isValid();
        if (m_temporalAAActive)
            ++m_temporalAAJitterIndex;
        else
            m_temporalAAJitterIndex = 0;
        m_previousObjectWorldMatrices.swap(m_currentObjectWorldMatrices);
        m_currentObjectWorldMatrices.clear();
        m_previousSkinningPalettes.swap(m_currentSkinningPalettes);
        m_currentSkinningPalettes.clear();
        return true;
    }

    bool DX12Renderer::resize(const std::uint32_t width, const std::uint32_t height)
    {
        if (width == 0 || height == 0)
            return true;

        ID3D12Device* const device = m_device.get();
        if (device == nullptr)
        {
            LOG_ERROR("[DX12] 未初期化の Renderer をリサイズしようとしました");
            return false;
        }

        if (!m_swapChain.resize(*device, m_directFence, m_lastSubmittedFenceValue, width, height))
            return false;

        m_frameFenceValues.fill(0);
        m_lastSubmittedFenceValue = 0;
        if (m_mode == ExecutionMode::Runtime)
        {
            m_requestedGameWidth = width;
            m_requestedGameHeight = height;
        }
        return true;
    }

    bool DX12Renderer::createGameRenderTarget(
        const std::uint32_t width, const std::uint32_t height, const Color& clearColor)
    {
        if (m_device.get() == nullptr || m_gameRtvHeap.get() == nullptr || width == 0 || height == 0)
            return false;

        m_runtimeUi.releaseTarget();
        m_gameRenderTarget.finalize();
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = width;
        description.Height = height;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc = { 1, 0 };
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE optimizedClearValue{};
        optimizedClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        optimizedClearValue.Color[0] = clearColor.x;
        optimizedClearValue.Color[1] = clearColor.y;
        optimizedClearValue.Color[2] = clearColor.z;
        optimizedClearValue.Color[3] = clearColor.w;
        const DX12ResourceConfig config{
            .description = description,
            .initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            .clearValue = &optimizedClearValue,
        };
        if (!m_gameRenderTarget.initialize(*m_device.get(), config))
            return false;

        if (m_gameRtvHeap.getAllocatedCount() == 0)
        {
            const std::optional<DX12DescriptorAllocation> allocation = m_gameRtvHeap.allocate();
            if (!allocation)
                return false;
            m_gameRenderTargetView = allocation->cpu;
        }
        m_device.get()->CreateRenderTargetView(
            m_gameRenderTarget.get(), nullptr, m_gameRenderTargetView.native);
        m_gameRenderTargetClearColor = clearColor;
        return true;
    }

    void DX12Renderer::setRenderView(const RenderView& view) noexcept
    {
        m_cameraPostProcess = view.postProcess;
        m_cameraPostProcess.temporalHistoryReset = view.camera.temporalHistoryReset;
        m_currentRenderCamera = view.postProcess.cameraGuid;
        m_currentRenderSize = view.camera.viewportSize;
        const bool sameCamera = m_hasPreviousRenderView && !view.camera.temporalHistoryReset
            && m_previousRenderCamera == m_currentRenderCamera
            && m_previousRenderSize.x == m_currentRenderSize.x
            && m_previousRenderSize.y == m_currentRenderSize.y;
        if (view.camera.temporalHistoryReset)
        {
            m_previousObjectWorldMatrices.clear();
            m_previousSkinningPalettes.clear();
            m_currentSkinningPalettes.clear();
        }
        const bool requestedTemporalAA = m_deferredRenderingEnabled
            && view.clearMode != CameraClearMode::Nothing
            && view.postProcess.enabled && view.postProcess.weight > 0
            && view.postProcess.effects.temporalAA > 0
            && view.postProcess.settings.temporalAAEnabled.value;
        m_temporalAAActive = requestedTemporalAA;
        Matrix projection = view.camera.projection;
        if (requestedTemporalAA)
        {
            if (view.camera.temporalHistoryReset)
                m_temporalAAJitterIndex = 0;
            const std::uint32_t sampleIndex = m_temporalAAJitterIndex % 8 + 1;
            const float width = std::max(view.camera.viewportSize.x, 1.0f);
            const float height = std::max(view.camera.viewportSize.y, 1.0f);
            projection._31 += (halton(sampleIndex, 2) - 0.5f) * 2.0f / width;
            projection._32 += (0.5f - halton(sampleIndex, 3)) * 2.0f / height;
        }
        m_viewProjection = view.camera.view * projection;
        m_inverseViewProjection = m_viewProjection.Invert();
        m_previousViewProjection = sameCamera ? m_lastSubmittedViewProjection : m_viewProjection;
        m_cameraPosition = view.camera.position;
        m_cameraNearClip = view.camera.nearClip;
        m_cameraFarClip = view.camera.farClip;
        m_cameraForward = view.camera.forward;
        m_frustum = view.frustum;
        m_cameraViewport = view.viewport;
        m_cameraClearMode = view.clearMode;
        m_cameraClearColor = view.backgroundColor;
        m_cameraCullingMask = view.cullingMask;
    }

    bool DX12Renderer::createDepthBuffer(const std::uint32_t width, const std::uint32_t height)
    {
        if (m_device.get() == nullptr || m_dsvHeap.get() == nullptr || width == 0 || height == 0)
            return false;

        m_depthBuffer.finalize();
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = width;
        description.Height = height;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_R32_TYPELESS;
        description.SampleDesc = { 1, 0 };
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        const D3D12_CLEAR_VALUE clearValue{
            .Format = DXGI_FORMAT_D32_FLOAT,
            .DepthStencil = { 1.0f, 0 },
        };
        const DX12ResourceConfig config{
            .description = description,
            .initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE,
            .clearValue = &clearValue,
        };
        if (!m_depthBuffer.initialize(*m_device.get(), config))
            return false;

        if (m_dsvHeap.getAllocatedCount() == 0)
        {
            const std::optional<DX12DescriptorAllocation> allocation = m_dsvHeap.allocate();
            if (!allocation)
                return false;
            m_depthStencilView = allocation->cpu;
        }
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        m_device.get()->CreateDepthStencilView(m_depthBuffer.get(), &dsv, m_depthStencilView.native);
        if (!m_depthShaderView)
            m_depthShaderView = TextureManager::instance().getDescriptorHeap().allocate();
        if (!m_depthShaderView || !m_depthShaderView->gpu)
        {
            LOG_ERROR("[Renderer] Failed to allocate the depth SRV descriptor.");
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        m_device.get()->CreateShaderResourceView(m_depthBuffer.get(), &srv, m_depthShaderView->cpu.native);
        return true;
    }

    bool DX12Renderer::createDeferredRenderTargets(const std::uint32_t width, const std::uint32_t height)
    {
        if (m_device.get() == nullptr || m_gameRtvHeap.get() == nullptr
            || width == 0 || height == 0)
        {
            LOG_ERROR("[Renderer] Cannot create deferred targets with an invalid device or size.");
            return false;
        }

        constexpr std::array formats = {
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
        };
        constexpr std::array<float, 4> clearColor{};
        constexpr std::array<float, 4> clearVelocity{ 0.0f, 0.0f, 1.0f, 1.0f };
        for (const DXGI_FORMAT format : formats)
        {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{ .Format = format };
            const HRESULT result = m_device.get()->CheckFeatureSupport(
                D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
            const D3D12_FORMAT_SUPPORT1 required =
                D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
            if (FAILED(result) || (support.Support1 & required) != required)
            {
                LOG_ERROR("[Renderer] G-Buffer format {} lacks render-target or shader-load support (HRESULT: 0x{:08X}).",
                    static_cast<unsigned int>(format), static_cast<unsigned long>(result));
                return false;
            }
        }
        D3D12_FEATURE_DATA_FORMAT_SUPPORT lightingSupport{ .Format = DXGI_FORMAT_R16G16B16A16_FLOAT };
        const HRESULT lightingSupportResult = m_device.get()->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT, &lightingSupport, sizeof(lightingSupport));
        if (FAILED(lightingSupportResult)
            || (lightingSupport.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) == 0
            || (lightingSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0)
        {
            LOG_ERROR("[Renderer] HDR lighting format lacks typed UAV store support (HRESULT: 0x{:08X}).",
                static_cast<unsigned long>(lightingSupportResult));
            return false;
        }
        const auto createTarget = [&](DX12Resource& resource, const DXGI_FORMAT format,
            const DX12CpuDescriptorHandle& renderTargetView,
            const DX12DescriptorAllocation& shaderView,
            const std::array<float, 4>& clearColor, const bool unorderedAccess) -> bool
        {
            D3D12_RESOURCE_DESC description{};
            description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            description.Width = width;
            description.Height = height;
            description.DepthOrArraySize = 1;
            description.MipLevels = 1;
            description.Format = format;
            description.SampleDesc = { 1, 0 };
            description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            description.Flags = unorderedAccess
                ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_CLEAR_VALUE clearValue{};
            clearValue.Format = format;
            std::copy(clearColor.begin(), clearColor.end(), clearValue.Color);
            const DX12ResourceConfig config{
                .description = description,
                .initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                .clearValue = &clearValue,
            };
            if (!resource.initialize(*m_device.get(), config))
                return false;

            m_device.get()->CreateRenderTargetView(resource.get(), nullptr, renderTargetView.native);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = format;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = 1;
            m_device.get()->CreateShaderResourceView(resource.get(), &srv, shaderView.cpu.native);
            return true;
        };

        for (std::size_t index = 0; index < m_gbufferTargets.size(); ++index)
        {
            if (m_gbufferRenderTargetViews[index].native.ptr == 0)
            {
                const auto allocation = m_gameRtvHeap.allocate();
                if (!allocation)
                {
                    LOG_ERROR("[Renderer] Failed to allocate G-Buffer RTV {}.", index);
                    return false;
                }
                m_gbufferRenderTargetViews[index] = allocation->cpu;
            }
            if (!m_gbufferShaderViews[index])
                m_gbufferShaderViews[index] = TextureManager::instance().getDescriptorHeap().allocate();
            if (!m_gbufferShaderViews[index] || !m_gbufferShaderViews[index]->gpu)
            {
                LOG_ERROR("[Renderer] Failed to allocate G-Buffer SRV {}.", index);
                return false;
            }
            if (!createTarget(m_gbufferTargets[index], formats[index],
                m_gbufferRenderTargetViews[index], *m_gbufferShaderViews[index],
                index == 4 ? clearVelocity : clearColor, false))
            {
                LOG_ERROR("[Renderer] Failed to create G-Buffer target {}.", index);
                return false;
            }
        }

        if (m_deferredLightingRenderTargetView.native.ptr == 0)
        {
            const auto allocation = m_gameRtvHeap.allocate();
            if (!allocation)
            {
                LOG_ERROR("[Renderer] Failed to allocate HDR lighting RTV.");
                return false;
            }
            m_deferredLightingRenderTargetView = allocation->cpu;
        }
        if (!m_deferredLightingShaderView)
            m_deferredLightingShaderView = TextureManager::instance().getDescriptorHeap().allocate();
        if (!m_deferredLightingShaderView || !m_deferredLightingShaderView->gpu)
        {
            LOG_ERROR("[Renderer] Failed to allocate HDR lighting SRV.");
            return false;
        }
        if (!m_deferredLightingUnorderedView)
            m_deferredLightingUnorderedView = TextureManager::instance().getDescriptorHeap().allocate();
        if (!m_deferredLightingUnorderedView || !m_deferredLightingUnorderedView->gpu)
        {
            LOG_ERROR("[Renderer] Failed to allocate HDR lighting UAV.");
            return false;
        }
        if (!createTarget(m_deferredLightingTarget, DXGI_FORMAT_R16G16B16A16_FLOAT,
            m_deferredLightingRenderTargetView, *m_deferredLightingShaderView, clearColor, true))
        {
            LOG_ERROR("[Renderer] Failed to create HDR lighting target.");
            return false;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device.get()->CreateUnorderedAccessView(m_deferredLightingTarget.get(), nullptr,
            &uav, m_deferredLightingUnorderedView->cpu.native);
        return true;
    }

    bool DX12Renderer::createShadowMap()
    {
        constexpr std::uint32_t SHADOW_MAP_SIZE = 2048;
        if (m_device.get() == nullptr || m_dsvHeap.get() == nullptr)
            return false;

        m_shadowMap.finalize();
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = SHADOW_MAP_SIZE;
        description.Height = SHADOW_MAP_SIZE;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_R32_TYPELESS;
        description.SampleDesc = { 1, 0 };
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        const D3D12_CLEAR_VALUE clearValue{
            .Format = DXGI_FORMAT_D32_FLOAT,
            .DepthStencil = { 1.0f, 0 },
        };
        const DX12ResourceConfig config{
            .description = description,
            .initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE,
            .clearValue = &clearValue,
        };
        if (!m_shadowMap.initialize(*m_device.get(), config))
            return false;
        if (m_dsvHeap.getAllocatedCount() < 2)
        {
            const std::optional<DX12DescriptorAllocation> allocation = m_dsvHeap.allocate();
            if (!allocation)
                return false;
            m_shadowDepthStencilView = allocation->cpu;
        }
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        m_device.get()->CreateDepthStencilView(m_shadowMap.get(), &dsv, m_shadowDepthStencilView.native);
        if (!m_shadowSrv)
            m_shadowSrv = TextureManager::instance().getDescriptorHeap().allocate();
        if (!m_shadowSrv || !m_shadowSrv->gpu)
            return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        m_device.get()->CreateShaderResourceView(m_shadowMap.get(), &srv, m_shadowSrv->cpu.native);
        return true;
    }

    bool DX12Renderer::recordDeferredLighting(const std::uint32_t frameIndex)
    {
        if (frameIndex >= FRAME_COUNT || !m_deferredFrameActive
            || m_deferredLightingShaderView == std::nullopt || m_deferredLightingUnorderedView == std::nullopt
            || m_depthShaderView == std::nullopt
            || m_shadowSrv == std::nullopt
            || !m_deferredLightingShaderView->gpu || !m_deferredLightingUnorderedView->gpu
            || !m_depthShaderView->gpu || !m_shadowSrv->gpu)
        {
            LOG_ERROR("[Renderer] Deferred Lighting resources are not ready.");
            return false;
        }
        for (const auto& view : m_gbufferShaderViews)
        {
            if (!view || !view->gpu)
            {
                LOG_ERROR("[Renderer] A G-Buffer SRV is not ready for Deferred Lighting.");
                return false;
            }
        }

        DX12CommandList& commandList = m_deferredLightingCommandLists[frameIndex];
        if (!commandList.begin(m_directFence))
            return false;
        constexpr auto shaderRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
            | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        for (DX12Resource& target : m_gbufferTargets)
            if (!target.transition(commandList, shaderRead))
                return false;
        if (!m_depthBuffer.transition(commandList, shaderRead)
            || !m_shadowMap.transition(commandList, shaderRead)
            || !m_deferredLightingTarget.transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            return false;

        ID3D12GraphicsCommandList* const native = commandList.getForRecording();
        ID3D12DescriptorHeap* const descriptorHeap = TextureManager::instance().getDescriptorHeap().get();
        if (native == nullptr || descriptorHeap == nullptr)
        {
            LOG_ERROR("[Renderer] Deferred Lighting is missing a command list or shader-visible heap.");
            return false;
        }
        native->SetDescriptorHeaps(1, &descriptorHeap);
        if (!m_deferredLightingPipeline.bind(commandList))
            return false;

        native->SetComputeRootConstantBufferView(0, m_lightingBuffers[frameIndex].getGpuVirtualAddress());
        native->SetComputeRootConstantBufferView(1, m_deferredConstantsBuffers[frameIndex].getGpuVirtualAddress());
        native->SetComputeRootDescriptorTable(2, m_gbufferShaderViews[0]->gpu->native);
        native->SetComputeRootDescriptorTable(3, m_gbufferShaderViews[1]->gpu->native);
        native->SetComputeRootDescriptorTable(4, m_gbufferShaderViews[2]->gpu->native);
        native->SetComputeRootDescriptorTable(5, m_gbufferShaderViews[3]->gpu->native);
        native->SetComputeRootDescriptorTable(6, m_depthShaderView->gpu->native);
        native->SetComputeRootDescriptorTable(7, m_gbufferShaderViews[0]->gpu->native);
        native->SetComputeRootDescriptorTable(8, m_shadowSrv->gpu->native);
        native->SetComputeRootDescriptorTable(9, m_deferredLightingUnorderedView->gpu->native);
        native->Dispatch((m_renderWidth + 7) / 8, (m_renderHeight + 7) / 8, 1);
        if (!m_depthBuffer.transition(commandList, D3D12_RESOURCE_STATE_DEPTH_WRITE)
            || !m_deferredLightingTarget.transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET)
            || !commandList.close())
            return false;

        m_deferredPassRecorded = true;
        return true;
    }

    bool DX12Renderer::recordDeferredDebug(DX12CommandList& commandList, const std::uint32_t frameIndex)
    {
        if (frameIndex >= FRAME_COUNT || !m_deferredLightingShaderView || !m_deferredLightingShaderView->gpu
            || !m_depthShaderView || !m_depthShaderView->gpu
            || !m_shadowSrv || !m_shadowSrv->gpu)
        {
            LOG_ERROR("[Renderer] Deferred debug resources are not ready.");
            return false;
        }
        for (const auto& view : m_gbufferShaderViews)
        {
            if (!view || !view->gpu)
            {
                LOG_ERROR("[Renderer] A G-Buffer SRV is not ready for Deferred debug.");
                return false;
            }
        }
        if (!m_deferredLightingTarget.transition(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            || !m_depthBuffer.transition(commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            || !m_gameRenderTarget.transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET))
            return false;

        ID3D12GraphicsCommandList* const native = commandList.getForRecording();
        ID3D12DescriptorHeap* const descriptorHeap = TextureManager::instance().getDescriptorHeap().get();
        if (native == nullptr || descriptorHeap == nullptr)
        {
            LOG_ERROR("[Renderer] Deferred debug is missing a command list or shader-visible heap.");
            return false;
        }
        native->SetDescriptorHeaps(1, &descriptorHeap);
        native->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        native->OMSetRenderTargets(1, &m_gameRenderTargetView.native, FALSE, nullptr);
        const Viewport pixelViewport = CameraUtils::toPixelViewport(m_cameraViewport,
            Vector2(static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight)));
        const D3D12_VIEWPORT viewport{ pixelViewport.x, pixelViewport.y,
            pixelViewport.width, pixelViewport.height, pixelViewport.minDepth, pixelViewport.maxDepth };
        const D3D12_RECT scissor{
            static_cast<LONG>(std::floor(viewport.TopLeftX)),
            static_cast<LONG>(std::floor(viewport.TopLeftY)),
            static_cast<LONG>(std::ceil(viewport.TopLeftX + viewport.Width)),
            static_cast<LONG>(std::ceil(viewport.TopLeftY + viewport.Height)),
        };
        native->RSSetViewports(1, &viewport);
        native->RSSetScissorRects(1, &scissor);
        if (!m_deferredDebugPipeline.bind(commandList))
            return false;

        native->SetGraphicsRootConstantBufferView(0, m_lightingBuffers[frameIndex].getGpuVirtualAddress());
        native->SetGraphicsRootConstantBufferView(1, m_deferredConstantsBuffers[frameIndex].getGpuVirtualAddress());
        native->SetGraphicsRootDescriptorTable(2, m_gbufferShaderViews[0]->gpu->native);
        native->SetGraphicsRootDescriptorTable(3, m_gbufferShaderViews[1]->gpu->native);
        native->SetGraphicsRootDescriptorTable(4, m_gbufferShaderViews[2]->gpu->native);
        native->SetGraphicsRootDescriptorTable(5, m_gbufferShaderViews[3]->gpu->native);
        native->SetGraphicsRootDescriptorTable(6, m_depthShaderView->gpu->native);
        native->SetGraphicsRootDescriptorTable(7, m_deferredLightingShaderView->gpu->native);
        native->SetGraphicsRootDescriptorTable(8, m_shadowSrv->gpu->native);
        native->DrawInstanced(3, 1, 0, 0);
        return m_depthBuffer.transition(commandList, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }

    bool DX12Renderer::setDeferredDebugView(const DeferredDebugView view)
    {
        const int value = static_cast<int>(view);
        if (value < 0 || value > static_cast<int>(DeferredDebugView::ShadowMap))
        {
            LOG_ERROR("[Renderer] Invalid Deferred debug view {}.", value);
            return false;
        }
        m_deferredDebugView = value;
        return true;
    }

    bool DX12Renderer::processImGuiMessage(const HWND hwnd, const UINT message, const WPARAM wparam, const LPARAM lparam)
    {
        return m_imguiSystem != nullptr && m_imguiSystem->processMessage(hwnd, message, wparam, lparam);
    }

    bool DX12Renderer::rebuildGraphicsPipelines()
    {
        const auto modelVertexShader = m_shaderManager.get(m_modelVertexShaderID);
        const auto skinningComputeShader = m_shaderManager.get(m_skinningComputeShaderID);
        const auto modelPixelShader = m_shaderManager.get(m_modelPixelShaderID);
        const auto alphaTestPixelShader = m_shaderManager.get(m_alphaTestPixelShaderID);
        const auto depthAlphaTestPixelShader = m_shaderManager.get(m_depthAlphaTestPixelShaderID);
        const auto gbufferPixelShader = m_shaderManager.get(m_gbufferPixelShaderID);
        const auto gbufferAlphaTestPixelShader = m_shaderManager.get(m_gbufferAlphaTestPixelShaderID);
        const auto deferredVertexShader = m_shaderManager.get(m_deferredVertexShaderID);
        const auto deferredLightingComputeShader = m_shaderManager.get(m_deferredLightingComputeShaderID);
        const auto deferredDebugPixelShader = m_shaderManager.get(m_deferredDebugPixelShaderID);
        const auto postProcessVertexShader = m_shaderManager.get(m_postProcessVertexShaderID);
        const auto postProcessToneMappingShader = m_shaderManager.get(m_postProcessToneMappingShaderID);
        const auto postProcessOutputShader = m_shaderManager.get(m_postProcessOutputShaderID);
        PostProcessBuiltinShaders postProcessBuiltins;
        for (std::size_t index = 0; index < postProcessBuiltins.size(); ++index)
            postProcessBuiltins[index] = m_shaderManager.get(m_postProcessBuiltinShaderIDs[index]);
        const auto debugVertexShader = m_shaderManager.get(m_debugVertexShaderID);
        const auto debugShapeVertexShader = m_shaderManager.get(m_debugShapeVertexShaderID);
        const auto debugPixelShader = m_shaderManager.get(m_debugPixelShaderID);
        const auto hizShader = m_shaderManager.get(m_hizShaderID);
        const auto occlusionShader = m_shaderManager.get(m_occlusionShaderID);
        if (!modelVertexShader || !skinningComputeShader || !modelPixelShader || !alphaTestPixelShader || !depthAlphaTestPixelShader
            || !gbufferPixelShader || !gbufferAlphaTestPixelShader || !deferredVertexShader
            || !deferredLightingComputeShader || !deferredDebugPixelShader
            || !postProcessVertexShader || !postProcessToneMappingShader || !postProcessOutputShader
            || !debugVertexShader || !debugShapeVertexShader || !debugPixelShader || !hizShader || !occlusionShader
            || !modelVertexShader->isCompiled() || !skinningComputeShader->isCompiled() || !modelPixelShader->isCompiled()
            || !alphaTestPixelShader->isCompiled() || !depthAlphaTestPixelShader->isCompiled()
            || !gbufferPixelShader->isCompiled() || !gbufferAlphaTestPixelShader->isCompiled()
            || !deferredVertexShader->isCompiled() || !deferredLightingComputeShader->isCompiled()
            || !deferredDebugPixelShader->isCompiled()
            || !postProcessVertexShader->isCompiled() || !postProcessToneMappingShader->isCompiled()
            || !postProcessOutputShader->isCompiled()
            || !debugVertexShader->isCompiled() || !debugShapeVertexShader->isCompiled() || !debugPixelShader->isCompiled()
            || !hizShader->isCompiled() || !occlusionShader->isCompiled())
        {
            LOG_ERROR("[DX12] 有効なModel Shaderがロードされていません");
            return false;
        }

        D3D12_ROOT_PARAMETER objectConstants{};
        objectConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        objectConstants.Descriptor.ShaderRegister = 5;
        objectConstants.Descriptor.RegisterSpace = 0;
        objectConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        std::array<D3D12_DESCRIPTOR_RANGE, 5> textureRanges{};
        std::array<D3D12_ROOT_PARAMETER, 5> textureTables{};
        for (std::uint32_t textureIndex = 0; textureIndex < textureRanges.size(); ++textureIndex)
        {
            textureRanges[textureIndex].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            textureRanges[textureIndex].NumDescriptors = 1;
            textureRanges[textureIndex].BaseShaderRegister = textureIndex;
            textureRanges[textureIndex].RegisterSpace = 0;
            textureRanges[textureIndex].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            textureTables[textureIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            textureTables[textureIndex].DescriptorTable.NumDescriptorRanges = 1;
            textureTables[textureIndex].DescriptorTable.pDescriptorRanges = &textureRanges[textureIndex];
            textureTables[textureIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_ROOT_PARAMETER bonePalette{};
        bonePalette.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        bonePalette.Descriptor.ShaderRegister = 1;
        bonePalette.Descriptor.RegisterSpace = 0;
        bonePalette.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_PARAMETER materialConstants{};
        materialConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        materialConstants.Descriptor.ShaderRegister = 2;
        materialConstants.Descriptor.RegisterSpace = 0;
        materialConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_PARAMETER materialProperties{};
        materialProperties.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        materialProperties.Constants.ShaderRegister = 3;
        materialProperties.Constants.RegisterSpace = 0;
        materialProperties.Constants.Num32BitValues = 17;
        materialProperties.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_PARAMETER lighting{};
        lighting.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        lighting.Descriptor.ShaderRegister = 4;
        lighting.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_DESCRIPTOR_RANGE shadowRange{};
        shadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        shadowRange.NumDescriptors = 1;
        shadowRange.BaseShaderRegister = 6;
        shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER shadowTable{};
        shadowTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        shadowTable.DescriptorTable = { 1, &shadowRange };
        shadowTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        std::array<D3D12_DESCRIPTOR_RANGE, TERRAIN_LAYER_TEXTURE_COUNT> terrainRanges{};
        std::array<D3D12_ROOT_PARAMETER, 12 + TERRAIN_LAYER_TEXTURE_COUNT> rootParameters = {
            objectConstants, bonePalette, materialConstants,
            textureTables[0], textureTables[1], textureTables[2], textureTables[3], textureTables[4],
            materialProperties, lighting, shadowTable,
        };
        for (std::uint32_t index = 0; index < terrainRanges.size(); ++index)
        {
            auto& range = terrainRanges[index];
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = 7 + index;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            auto& parameter = rootParameters[11 + index];
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.DescriptorTable = { 1, &range };
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        auto& cachedSkinningFlag = rootParameters[11 + TERRAIN_LAYER_TEXTURE_COUNT];
        cachedSkinningFlag.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        cachedSkinningFlag.Constants = { 5, 0, 1 };
        cachedSkinningFlag.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        const std::array staticSamplers = {
            D3D12_STATIC_SAMPLER_DESC{
                .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                .MipLODBias = 0.0f,
                .MaxAnisotropy = 1,
                .ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
                .BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
                .MinLOD = 0.0f,
                .MaxLOD = D3D12_FLOAT32_MAX,
                .ShaderRegister = 0,
                .RegisterSpace = 0,
                .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
            },
            D3D12_STATIC_SAMPLER_DESC{
                .Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
                .AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                .AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                .AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER,
                .MaxAnisotropy = 1,
                .ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL,
                .BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
                .MaxLOD = D3D12_FLOAT32_MAX,
                .ShaderRegister = 1,
                .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
            },
        };
        const DX12GraphicsPipelineConfig modelConfig{
            .vertexShader = modelVertexShader.get(),
            .pixelShader = modelPixelShader.get(),
            .inputLayout = MODEL_INPUT_LAYOUT,
            .rootParameters = rootParameters,
            .staticSamplers = staticSamplers,
            .renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
            .depthStencilFormat = DXGI_FORMAT_D32_FLOAT,
        };
        DX12GraphicsPipelineConfig transparentModelConfig = modelConfig;
        transparentModelConfig.enableAlphaBlend = true;
        transparentModelConfig.enableDepthWrite = false;
        DX12GraphicsPipelineConfig alphaTestModelConfig = modelConfig;
        alphaTestModelConfig.pixelShader = alphaTestPixelShader.get();
        alphaTestModelConfig.enableDepthWrite = false;
        alphaTestModelConfig.depthComparison = D3D12_COMPARISON_FUNC_EQUAL;
        DX12GraphicsPipelineConfig opaqueModelConfig = modelConfig;
        opaqueModelConfig.enableDepthWrite = false;
        opaqueModelConfig.depthComparison = D3D12_COMPARISON_FUNC_EQUAL;
        constexpr std::array gbufferFormats = {
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
        };
        DX12GraphicsPipelineConfig gbufferModelConfig = opaqueModelConfig;
        gbufferModelConfig.pixelShader = gbufferPixelShader.get();
        gbufferModelConfig.renderTargetFormat = DXGI_FORMAT_UNKNOWN;
        gbufferModelConfig.renderTargetFormats = gbufferFormats;
        DX12GraphicsPipelineConfig gbufferAlphaTestModelConfig = gbufferModelConfig;
        gbufferAlphaTestModelConfig.pixelShader = gbufferAlphaTestPixelShader.get();
        DX12GraphicsPipelineConfig deferredTransparentModelConfig = transparentModelConfig;
        deferredTransparentModelConfig.renderTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        DX12GraphicsPipelineConfig depthOnlyModelConfig = modelConfig;
        depthOnlyModelConfig.pixelShader = nullptr;
        depthOnlyModelConfig.renderTargetFormat = DXGI_FORMAT_UNKNOWN;
        DX12GraphicsPipelineConfig depthAlphaTestModelConfig = depthOnlyModelConfig;
        depthAlphaTestModelConfig.pixelShader = depthAlphaTestPixelShader.get();
        DX12GraphicsPipeline nextModelPipeline;
        DX12GraphicsPipeline nextAlphaTestModelPipeline;
        DX12GraphicsPipeline nextTransparentModelPipeline;
        DX12GraphicsPipeline nextDepthOnlyModelPipeline;
        DX12GraphicsPipeline nextDepthAlphaTestModelPipeline;
        DX12GraphicsPipeline nextGBufferModelPipeline;
        DX12GraphicsPipeline nextGBufferAlphaTestModelPipeline;
        DX12GraphicsPipeline nextDeferredTransparentModelPipeline;
        DX12ComputePipeline nextDeferredLightingPipeline;
        DX12GraphicsPipeline nextDeferredDebugPipeline;
        DX12ComputePipeline nextHiZPipeline;
        DX12ComputePipeline nextOcclusionPipeline;
        DX12ComputePipeline nextSkinningPipeline;

        std::array<D3D12_ROOT_PARAMETER, 4> skinningParameters{};
        skinningParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        skinningParameters[0].Descriptor.ShaderRegister = 20;
        skinningParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        skinningParameters[1].Descriptor.ShaderRegister = 0;
        skinningParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        skinningParameters[2].Descriptor.ShaderRegister = 1;
        skinningParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        skinningParameters[3].Constants = { 6, 0, 2 };

        D3D12_ROOT_PARAMETER deferredLightingConstants{};
        deferredLightingConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        deferredLightingConstants.Descriptor.ShaderRegister = 4;
        deferredLightingConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_PARAMETER deferredPassConstants{};
        deferredPassConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        deferredPassConstants.Descriptor.ShaderRegister = 5;
        deferredPassConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        std::array<D3D12_DESCRIPTOR_RANGE, 7> deferredRanges{};
        std::array<D3D12_ROOT_PARAMETER, 7> deferredTextureTables{};
        for (std::uint32_t index = 0; index < deferredRanges.size(); ++index)
        {
            deferredRanges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            deferredRanges[index].NumDescriptors = 1;
            deferredRanges[index].BaseShaderRegister = index;
            deferredRanges[index].RegisterSpace = 0;
            deferredRanges[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            deferredTextureTables[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            deferredTextureTables[index].DescriptorTable = { 1, &deferredRanges[index] };
            deferredTextureTables[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        std::array<D3D12_ROOT_PARAMETER, 9> deferredRootParameters{};
        deferredRootParameters[0] = deferredLightingConstants;
        deferredRootParameters[1] = deferredPassConstants;
        std::copy(deferredTextureTables.begin(), deferredTextureTables.end(),
            deferredRootParameters.begin() + 2);
        const std::array deferredStaticSamplers{ staticSamplers[1] };
        const DX12GraphicsPipelineConfig deferredDebugConfig{
            .vertexShader = deferredVertexShader.get(),
            .pixelShader = deferredDebugPixelShader.get(),
            .rootParameters = deferredRootParameters,
            .staticSamplers = deferredStaticSamplers,
            .renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
        };
        std::array<D3D12_ROOT_PARAMETER, 10> deferredComputeParameters{};
        std::copy(deferredRootParameters.begin(), deferredRootParameters.end(), deferredComputeParameters.begin());
        for (auto& parameter : deferredComputeParameters)
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        const D3D12_DESCRIPTOR_RANGE deferredOutputRange{
            D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 };
        deferredComputeParameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        deferredComputeParameters[9].DescriptorTable = { 1, &deferredOutputRange };
        std::array deferredComputeSamplers{ staticSamplers[1] };
        deferredComputeSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        if (!nextModelPipeline.initialize(*m_device.get(), opaqueModelConfig)
            || !nextAlphaTestModelPipeline.initialize(*m_device.get(), alphaTestModelConfig)
            || !nextTransparentModelPipeline.initialize(*m_device.get(), transparentModelConfig)
            || !nextDepthOnlyModelPipeline.initialize(*m_device.get(), depthOnlyModelConfig)
            || !nextDepthAlphaTestModelPipeline.initialize(*m_device.get(), depthAlphaTestModelConfig)
            || !nextGBufferModelPipeline.initialize(*m_device.get(), gbufferModelConfig)
            || !nextGBufferAlphaTestModelPipeline.initialize(*m_device.get(), gbufferAlphaTestModelConfig)
            || !nextDeferredTransparentModelPipeline.initialize(*m_device.get(), deferredTransparentModelConfig)
            || !nextSkinningPipeline.initialize(*m_device.get(), *skinningComputeShader, skinningParameters)
            || !nextDeferredLightingPipeline.initialize(*m_device.get(), *deferredLightingComputeShader,
                deferredComputeParameters, deferredComputeSamplers)
            || !nextDeferredDebugPipeline.initialize(*m_device.get(), deferredDebugConfig)
            || !DX12GpuOcclusionCulling::createPipelines(*m_device.get(), *hizShader, *occlusionShader,
                nextHiZPipeline, nextOcclusionPipeline)
            || !DebugPrimitive::instance().rebuildPipeline(*debugVertexShader, *debugShapeVertexShader, *debugPixelShader)
            || !m_materialGpuCache.rebuildAll()
            || !m_postProcess.rebuild(postProcessVertexShader, postProcessToneMappingShader, postProcessOutputShader, postProcessBuiltins))
        {
            return false;
        }
        m_modelPipeline.swap(nextModelPipeline);
        m_skinningPipeline.swap(nextSkinningPipeline);
        m_alphaTestModelPipeline.swap(nextAlphaTestModelPipeline);
        m_transparentModelPipeline.swap(nextTransparentModelPipeline);
        m_depthOnlyModelPipeline.swap(nextDepthOnlyModelPipeline);
        m_depthAlphaTestModelPipeline.swap(nextDepthAlphaTestModelPipeline);
        m_gbufferModelPipeline.swap(nextGBufferModelPipeline);
        m_gbufferAlphaTestModelPipeline.swap(nextGBufferAlphaTestModelPipeline);
        m_deferredTransparentModelPipeline.swap(nextDeferredTransparentModelPipeline);
        m_deferredLightingPipeline.swap(nextDeferredLightingPipeline);
        m_deferredDebugPipeline.swap(nextDeferredDebugPipeline);
        m_occlusionPipeline.swap(nextOcclusionPipeline);
        m_hizPipeline.swap(nextHiZPipeline);
        return true;
    }

    void DX12Renderer::buildModelRenderQueue(std::vector<ModelHandle>& usedModels, std::vector<MaterialHandle>& usedMaterials)
    {
        FrameProfiler::ScopedCpuTimer profile(FrameProfileMetric::RenderQueue);
        m_modelRenderQueue.clear();
        m_frameStatistics = {};
        ModelRenderSubmissionQueue::instance().consume(m_modelSubmissions);
        m_modelRenderQueue.reserve(m_modelSubmissions.size());
        usedModels.reserve(m_modelSubmissions.size());
        usedMaterials.reserve(m_modelSubmissions.size());
        const auto shadowFrustum = m_shadowViewProjection
            ? std::optional(CameraUtils::buildWorldFrustum(*m_shadowViewProjection, Matrix::Identity)) : std::nullopt;

        for (const ModelRenderSubmission& batch : m_modelSubmissions)
        {
            ModelRenderSubmission instanceSubmission;
            if (batch.instanceTransforms)
                instanceSubmission = batch;
            const ModelRenderSubmission& submission = batch.instanceTransforms ? instanceSubmission : batch;
            const std::size_t instanceCount = batch.instanceTransforms ? batch.instanceTransforms->size() : 1;
            if (batch.instanceTransforms && (!batch.modelSnapshot || batch.skinningPalette || batch.terrainLod))
            {
                LOG_ERROR("[Renderer] Static instance batches require a model snapshot and no skinning or terrain LOD.");
                continue;
            }
            for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex)
            {
                if (batch.instanceTransforms)
                {
                    instanceSubmission.worldMatrix = (*batch.instanceTransforms)[instanceIndex] * batch.worldMatrix;
                    batch.modelSnapshot->boundingBox.Transform(instanceSubmission.worldBounds, instanceSubmission.worldMatrix);
                }
                if (submission.layer >= 32
                    || (m_cameraCullingMask & (1u << submission.layer)) == 0)
                {
                    ++m_frameStatistics.culledObjects;
                    if (submission.terrainLod)
                        m_frameStatistics.terrainCulledChunks += static_cast<std::uint32_t>(submission.terrainLod->chunks.size());
                    continue;
                }

                const bool submitsShadow = m_shadowViewProjection.has_value() && submission.castShadows;
                bool outsideCamera = submission.skinningPalette == nullptr && m_frustum
                    && !isVisible(*m_frustum, submission.worldBounds);
                if (outsideCamera)
                    ++m_frameStatistics.culledObjects;
                if (outsideCamera && !submitsShadow)
                {
                    if (submission.terrainLod)
                        m_frameStatistics.terrainCulledChunks += static_cast<std::uint32_t>(submission.terrainLod->chunks.size());
                    continue;
                }

                ModelGpuResource* const gpuModel = m_modelGpuCache.getOrCreate(submission.model, submission.modelSnapshot);
                if (gpuModel == nullptr || gpuModel->source == nullptr)
                    continue;
                if (submission.terrainLod && (submission.terrainLod->chunks.size() != gpuModel->meshes.size()
                    || !validateTerrainLodSettings(submission.terrainLod->settings)))
                {
                    LOG_ERROR("[TerrainLod] Queued LOD metadata does not match the model snapshot.");
                    continue;
                }

                if (submission.skinningPalette != nullptr)
                {
                    m_animatedMeshBounds.resize(gpuModel->meshes.size());
                    AABB objectBounds{};
                    bool hasBounds = false;
                    bool allBoundsValid = true;
                    for (std::size_t meshIndex = 0; meshIndex < gpuModel->meshes.size(); ++meshIndex)
                    {
                        AnimatedMeshBounds& pose = m_animatedMeshBounds[meshIndex];
                        pose = {};
                        const auto& gpuMesh = gpuModel->meshes[meshIndex];
                        if (gpuMesh == nullptr)
                            continue;
                        pose.valid = gpuMesh->skinningBounds.calculate(*submission.skinningPalette,
                            gpuMesh->nodeTransform * submission.worldMatrix, pose.worldBounds);
                        allBoundsValid = allBoundsValid && pose.valid;
                        if (pose.valid)
                        {
                            if (hasBounds)
                                AABB::CreateMerged(objectBounds, objectBounds, pose.worldBounds);
                            else
                                objectBounds = pose.worldBounds;
                            hasBounds = true;
                        }
                    }
                    outsideCamera = allBoundsValid && hasBounds && m_frustum && !isVisible(*m_frustum, objectBounds);
                    if (outsideCamera)
                        ++m_frameStatistics.culledObjects;
                    if (outsideCamera && !submitsShadow)
                        continue;
                }

                bool objectVisible = false;
                bool modelResourceUsed = false;
                for (std::size_t meshIndex = 0; meshIndex < gpuModel->meshes.size(); ++meshIndex)
                {
                    const std::unique_ptr<ModelGpuMesh>& gpuMesh = gpuModel->meshes[meshIndex];
                    if (gpuMesh == nullptr)
                        continue;
                    const MeshResource& mesh = gpuModel->source->meshes[meshIndex];
                    const Matrix meshWorldMatrix = gpuMesh->nodeTransform * submission.worldMatrix;
                    AABB meshWorldBounds;
                    mesh.boundingBox.Transform(meshWorldBounds, meshWorldMatrix);
                    const bool boundsCullable = submission.skinningPalette == nullptr
                        || m_animatedMeshBounds[meshIndex].valid;
                    if (submission.skinningPalette != nullptr && boundsCullable)
                        meshWorldBounds = m_animatedMeshBounds[meshIndex].worldBounds;
                    const TerrainChunkLod* terrain = submission.terrainLod ? &submission.terrainLod->chunks[meshIndex] : nullptr;
                    const bool terrainInCamera = !terrain || !m_frustum || isVisible(*m_frustum, meshWorldBounds);
                    const bool terrainInShadow = (!terrain && !batch.instanceTransforms)
                        || !shadowFrustum || isVisible(*shadowFrustum, meshWorldBounds);
                    std::uint32_t terrainLevel = 0;
                    if (terrain)
                    {
                        if (terrain->levelCount == 0 || terrain->levelCount > terrain->levels.size()
                            || mesh.subMeshes.size() != 1 || terrain->levels[0].start != mesh.subMeshes[0].indexStart
                            || terrain->levels[0].count != mesh.subMeshes[0].indexCount
                            || !std::ranges::all_of(std::span(terrain->levels).first(terrain->levelCount),
                                [&mesh](const auto range)
                                {
                                    return range.count > 0 && range.count % 3 == 0 && range.start % 3 == 0
                                        && range.start <= mesh.indices.size() && range.count <= mesh.indices.size() - range.start;
                                }))
                        {
                            LOG_ERROR("[TerrainLod] Queued index ranges do not match full-resolution chunk geometry.");
                            continue;
                        }
                        if (!terrainInCamera)
                            ++m_frameStatistics.terrainCulledChunks;
                        if (!terrainInCamera && (!submitsShadow || !terrainInShadow))
                            continue;
                        terrainLevel = selectTerrainLod(submission.terrainLod->settings, meshWorldBounds,
                            m_cameraPosition, terrain->levelCount);
                    }

                    const auto submitSubMesh = [&](const std::uint32_t indexStart, const std::uint32_t indexCount,
                        const std::uint32_t materialIndex)
                        {
                            if (indexCount == 0 || indexStart > mesh.indices.size()
                                || indexCount > mesh.indices.size() - indexStart)
                                return;

                            MaterialHandle materialHandle = materialIndex < submission.materials.size()
                                ? submission.materials[materialIndex] : MaterialHandle::Invalid();
                            const auto materialSnapshot = submission.materialSnapshots && materialIndex < submission.materialSnapshots->size()
                                ? (*submission.materialSnapshots)[materialIndex] : (materialIndex == 0 ? submission.materialSnapshot : nullptr);
                            MaterialGpuResource* const gpuMaterial = m_materialGpuCache.getOrCreate(materialHandle, materialSnapshot);
                            if (gpuMaterial == nullptr || gpuMaterial->source == nullptr)
                                return;
                            materialHandle = gpuMaterial->handle;
                            RenderPassType pass = RenderPassType::Opaque;
                            if (gpuMaterial->source->renderState.surfaceType == MaterialSurfaceType::AlphaTest)
                                pass = RenderPassType::AlphaTest;
                            else if (gpuMaterial->source->renderState.surfaceType == MaterialSurfaceType::Transparent)
                                pass = RenderPassType::Transparent;
                            const Vector3 cameraRelativePosition = Vector3(meshWorldBounds.Center) - m_cameraPosition;
                            RenderItem colorItem{
                                .vertexBuffer = &gpuMesh->vertexBufferView,
                                .indexBuffer = &gpuMesh->indexBufferView,
                                .worldMatrix = meshWorldMatrix,
                                .worldBounds = meshWorldBounds,
                                .indexStart = indexStart,
                                .indexCount = indexCount,
                                .objectID = submission.objectID,
                                .pipelineID = static_cast<std::uint32_t>(pass),
                                .shaderVariantID = gpuMaterial->shaderVariantID,
                                .material = materialHandle,
                                .materialSnapshot = materialSnapshot,
                                .surfaceType = gpuMaterial->source->renderState.surfaceType,
                                .materialProperties = submission.materialProperties,
                                .textureID = gpuMaterial->baseColorTexture.index,
                                .hasSkinWeights = gpuMesh->hasSkinWeights,
                                .skinningPalette = submission.skinningPalette,
                                .bonePaletteBuffer = &gpuModel->bonePaletteBuffer,
                                .meshID = static_cast<std::uint32_t>(meshIndex),
                                .cameraDepth = cameraRelativePosition.x * m_cameraForward.x
                                    + cameraRelativePosition.y * m_cameraForward.y
                                    + cameraRelativePosition.z * m_cameraForward.z,
                                .pass = pass,
                                .animationBoundsValid = submission.skinningPalette != nullptr && boundsCullable,
                            };
                            const CameraFrustum* const cullingFrustum = m_frustum ? &*m_frustum : nullptr;
                            const bool submitted = terrainInCamera && m_modelRenderQueue.submit(colorItem, cullingFrustum);
                            if (submitted && terrain)
                            {
                                ++m_frameStatistics.terrainVisibleChunks;
                                ++m_frameStatistics.terrainLodChunks[terrainLevel];
                                m_frameStatistics.terrainTriangles += indexCount / 3;
                                m_frameStatistics.terrainFullTriangles += terrain->levels[0].count / 3;
                            }
                            bool materialUsed = submitted;
                            if (submitted && pass != RenderPassType::Transparent)
                            {
                                RenderItem depthItem = colorItem;
                                depthItem.pipelineID = depthItem.surfaceType == MaterialSurfaceType::AlphaTest ? 1u : 0u;
                                depthItem.pass = RenderPassType::DepthOnly;
                                m_modelRenderQueue.submit(depthItem);
                            }
                            if (submitsShadow && terrainInShadow && pass != RenderPassType::Transparent)
                            {
                                RenderItem shadowItem = colorItem;
                                shadowItem.pipelineID = colorItem.surfaceType == MaterialSurfaceType::AlphaTest ? 1u : 0u;
                                shadowItem.pass = RenderPassType::Shadow;
                                if (terrain)
                                {
                                    // Shadows retain full resolution, independent of camera LOD or visibility.
                                    shadowItem.indexStart = terrain->levels[0].start;
                                    shadowItem.indexCount = terrain->levels[0].count;
                                }
                                const bool shadowSubmitted = m_modelRenderQueue.submit(shadowItem);
                                if (shadowSubmitted && terrain)
                                {
                                    ++m_frameStatistics.terrainShadowChunks;
                                    m_frameStatistics.terrainShadowTriangles += shadowItem.indexCount / 3;
                                }
                                materialUsed = shadowSubmitted || materialUsed;
                            }
                            objectVisible = submitted || objectVisible;
                            modelResourceUsed = materialUsed || modelResourceUsed;
                            if (materialUsed && std::find(usedMaterials.begin(), usedMaterials.end(), materialHandle) == usedMaterials.end())
                                usedMaterials.push_back(materialHandle);
                        };

                    if (terrain)
                    {
                        const auto range = terrain->levels[terrainLevel];
                        submitSubMesh(range.start, range.count, 0);
                    }
                    else if (mesh.subMeshes.empty())
                        submitSubMesh(0, static_cast<std::uint32_t>(mesh.indices.size()), 0);
                    else
                    {
                        for (const SubMeshResource& subMesh : mesh.subMeshes)
                            submitSubMesh(subMesh.indexStart, subMesh.indexCount, subMesh.materialIndex);
                    }
                }

                if (objectVisible)
                {
                    ++m_frameStatistics.visibleObjects;
                }
                if (modelResourceUsed
                    && std::find(usedModels.begin(), usedModels.end(), submission.model) == usedModels.end())
                    usedModels.push_back(submission.model);
            }
        }

        m_modelRenderQueue.sort();
        m_frameStatistics.renderItemCount = static_cast<std::uint32_t>(m_modelRenderQueue.getItems().size());
    }

    bool DX12Renderer::renderModelQueue(DX12CommandList& commandList, const std::uint32_t frameIndex)
    {
        m_executionLists.clear();
        m_queryListRecorded = false;
        {
            FrameProfiler::ScopedCpuTimer profile(FrameProfileMetric::DrawPreparation);
            if (!prepareModelDraws(frameIndex))
                return false;
        }
        if (!recordSkinning(commandList, frameIndex))
            return false;
        const bool hasShadow = std::any_of(m_preparedModelDraws.begin(), m_preparedModelDraws.end(),
            [](const DX12PreparedModelDraw& draw) { return draw.item->pass == RenderPassType::Shadow; });
        if (m_shadowViewProjection.has_value())
        {
            if (!m_shadowMap.transition(commandList, D3D12_RESOURCE_STATE_DEPTH_WRITE))
                return false;
            commandList.getForRecording()->ClearDepthStencilView(
                m_shadowDepthStencilView.native, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }
        if (!commandList.close())
            return false;
        m_executionLists.push_back(commandList.getForExecution());

        const Viewport pixelViewport = CameraUtils::toPixelViewport(m_cameraViewport,
            Vector2(static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight)));
        DX12ModelRecordingState state{
            .textureHeap = TextureManager::instance().getDescriptorHeap().get(),
            .lighting = m_lightingBuffers[frameIndex].getGpuVirtualAddress(),
            .shadowTexture = m_shadowSrv->gpu->native,
            .colorTarget = m_gameRenderTargetView.native,
            .geometryTargets = {
                m_gbufferRenderTargetViews[0].native, m_gbufferRenderTargetViews[1].native,
                m_gbufferRenderTargetViews[2].native, m_gbufferRenderTargetViews[3].native,
                m_gbufferRenderTargetViews[4].native },
            .deferredTarget = m_deferredLightingRenderTargetView.native,
            .deferred = m_deferredFrameActive,
            .depthTarget = m_depthStencilView.native,
            .shadowTarget = m_shadowDepthStencilView.native,
            .viewport = {
                pixelViewport.x, pixelViewport.y, pixelViewport.width, pixelViewport.height,
                pixelViewport.minDepth, pixelViewport.maxDepth },
        };
        state.scissor = {
            static_cast<LONG>(std::floor(state.viewport.TopLeftX)), static_cast<LONG>(std::floor(state.viewport.TopLeftY)),
            static_cast<LONG>(std::ceil(state.viewport.TopLeftX + state.viewport.Width)),
            static_cast<LONG>(std::ceil(state.viewport.TopLeftY + state.viewport.Height)) };
        const D3D12_RESOURCE_DESC& shadowDescription = m_shadowMap.getDescription();
        state.shadowViewport = { 0.0f, 0.0f, static_cast<float>(shadowDescription.Width),
            static_cast<float>(shadowDescription.Height), 0.0f, 1.0f };
        state.shadowScissor = { 0, 0, static_cast<LONG>(shadowDescription.Width),
            static_cast<LONG>(shadowDescription.Height) };

        const bool hasColor = std::any_of(m_preparedModelDraws.begin(), m_preparedModelDraws.end(),
            [](const DX12PreparedModelDraw& draw) { return draw.item->pass >= RenderPassType::Opaque; });
        DX12CommandList& queryList = m_queryCommandLists[frameIndex];
        if (hasColor || hasShadow)
        {
            m_queryListRecorded = true;
            if (!queryList.begin(m_directFence))
                return false;
            if (!m_shadowMap.transition(queryList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
            ID3D12GraphicsCommandList& native = *queryList.getForRecording();
            native.OMSetRenderTargets(0, nullptr, FALSE, &state.depthTarget);
            native.RSSetViewports(1, &state.viewport);
            native.RSSetScissorRects(1, &state.scissor);
            DX12GpuOcclusionCulling& culling = m_occlusionCulling[frameIndex];
            const bool cullColor = m_enableOcclusionCulling && hasColor
                && m_cameraClearMode != CameraClearMode::Nothing;
            if ((cullColor && !culling.record(queryList, *m_device.get(), m_directFence, m_depthBuffer,
                m_hizPipeline, m_occlusionPipeline, m_modelRenderQueue, *m_modelInstanceBuffers[frameIndex].get(),
                m_viewProjection, state.viewport, m_enableModelInstancing)) || !queryList.close())
                return false;
            m_frameStatistics.occlusionBatchCount = cullColor ? culling.getBatchCount() : 0;
            if (m_frameStatistics.occlusionBatchCount != 0)
                state.culling = &culling;
        }

        DX12ModelCommandRecorder& recorder = m_modelCommandRecorders[frameIndex];
        {
            FrameProfiler::ScopedCpuTimer profile(FrameProfileMetric::CommandRecording);
            if (!recorder.record(*m_device.get(), m_directFence, m_preparedModelDraws,
                state, m_enableParallelModelRecording))
                return false;
        }
        if (m_deferredFrameActive && !recordDeferredLighting(frameIndex))
            return false;
        bool queryInserted = !hasColor && !hasShadow;
        bool deferredInserted = !m_deferredFrameActive;
        const auto ranges = recorder.getRanges();
        for (std::size_t index = 0; index < ranges.size(); ++index)
        {
            const RenderPassType pass = m_preparedModelDraws[ranges[index].first].item->pass;
            if (!queryInserted && pass > RenderPassType::Shadow)
            {
                m_executionLists.push_back(queryList.getForExecution());
                queryInserted = true;
            }
            if (!deferredInserted && pass == RenderPassType::Transparent)
            {
                m_executionLists.push_back(m_deferredLightingCommandLists[frameIndex].getForExecution());
                deferredInserted = true;
            }
            ID3D12CommandList* const native = recorder.getForExecution(index);
            if (native == nullptr)
            {
                LOG_ERROR("[Renderer] Model command list {} was not closed.", index);
                return false;
            }
            m_executionLists.push_back(native);
        }
        if (!queryInserted)
            m_executionLists.push_back(queryList.getForExecution());
        if (!deferredInserted)
            m_executionLists.push_back(m_deferredLightingCommandLists[frameIndex].getForExecution());
        const auto& statistics = recorder.getStatistics();
        m_frameStatistics.drawCallCount = statistics.drawCallCount;
        m_frameStatistics.indirectCallCount = statistics.indirectCallCount;
        m_frameStatistics.batchCount = statistics.drawCallCount + statistics.indirectCallCount;
        m_frameStatistics.instanceCount = statistics.instanceCount;
        m_frameStatistics.psoSwitchCount = statistics.psoSwitchCount;
        m_frameStatistics.materialSwitchCount = statistics.materialSwitchCount;
        m_frameStatistics.textureSwitchCount = statistics.textureSwitchCount;
        m_frameStatistics.vertexBufferSwitchCount = statistics.vertexBufferSwitchCount;
        m_frameStatistics.indexBufferSwitchCount = statistics.indexBufferSwitchCount;
        m_frameStatistics.modelCommandListCount = static_cast<std::uint32_t>(ranges.size());
        m_frameStatistics.modelRecordingThreadCount = statistics.recordingThreadCount;
        return true;
    }

    bool DX12Renderer::prepareModelDraws(const std::uint32_t frameIndex)
    {
        m_preparedModelDraws.clear();
        m_currentObjectWorldMatrices.clear();
        m_currentSkinningPalettes.clear();
        if (frameIndex >= FRAME_COUNT)
        {
            LOG_ERROR("[Renderer] Invalid skinning frame index {}.", frameIndex);
            return false;
        }
        m_cachedSkinningBufferCursors[frameIndex] = 0;
        const bool motionBlurHistory = m_deferredFrameActive && m_cameraPostProcess.enabled
            && m_cameraPostProcess.weight > 0 && m_cameraPostProcess.effects.motionBlur > 0
            && m_cameraPostProcess.settings.motionBlurEnabled.value
            && m_cameraPostProcess.settings.motionBlurAmount.value > 0;
        const bool trackSkinningHistory = m_temporalAAActive || motionBlurHistory;
        if (!trackSkinningHistory)
            m_previousSkinningPalettes.clear();
        const std::span<const RenderItem> items = m_modelRenderQueue.getItems();
        if (items.empty())
            return true;

        if (frameIndex >= FRAME_COUNT || items.size() > UINT32_MAX)
        {
            LOG_ERROR("[Renderer] Invalid instance buffer frame or item count.");
            return false;
        }
        m_modelInstanceConstants.resize(items.size());
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            const RenderItem& item = items[index];
            const bool shadowPass = item.pass == RenderPassType::Shadow && m_shadowViewProjection.has_value();
            const Matrix& viewProjection = shadowPass ? *m_shadowViewProjection : m_viewProjection;
            const std::uint64_t objectMeshKey = (static_cast<std::uint64_t>(item.objectID) << 32)
                | item.meshID;
            const auto previousObject = item.objectID != 0
                ? m_previousObjectWorldMatrices.find(objectMeshKey) : m_previousObjectWorldMatrices.end();
            const Matrix& previousWorld = previousObject != m_previousObjectWorldMatrices.end()
                ? previousObject->second : item.worldMatrix;
            const Matrix& previousViewProjection = shadowPass ? viewProjection : m_previousViewProjection;
            m_modelInstanceConstants[index] = {
                item.worldMatrix * viewProjection,
                item.worldMatrix,
                previousWorld * previousViewProjection,
            };
            if (item.objectID != 0 && (item.pass == RenderPassType::Opaque || item.pass == RenderPassType::AlphaTest))
                m_currentObjectWorldMatrices.insert_or_assign(objectMeshKey, item.worldMatrix);
        }
        DX12UploadBuffer& instanceBuffer = m_modelInstanceBuffers[frameIndex];
        const auto instanceBytes = std::as_bytes(std::span<const ModelInstanceConstants>(m_modelInstanceConstants));
        if (instanceBuffer.getSize() < instanceBytes.size())
        {
            const std::uint64_t capacity = std::max<std::uint64_t>(instanceBytes.size(),
                std::max<std::uint64_t>(sizeof(ModelInstanceConstants) * 256, instanceBuffer.getSize() * 2));
            if (!instanceBuffer.initialize(*m_device.get(), m_directFence, capacity))
            {
                LOG_ERROR("[Renderer] Failed to allocate model instance buffer.");
                return false;
            }
        }
        if (!instanceBuffer.write(instanceBytes))
        {
            LOG_ERROR("[Renderer] Failed to upload model instances.");
            return false;
        }

        TextureManager& textureManager = TextureManager::instance();
        std::unordered_map<const SkinningPaletteSnapshot*,
            std::unordered_map<std::uint32_t, DX12UploadBuffer*>> uploadedPalettes;
        std::unordered_map<D3D12_GPU_VIRTUAL_ADDRESS,
            std::unordered_map<D3D12_GPU_VIRTUAL_ADDRESS, CachedSkinningBuffer*>> skinnedMeshes;
        MaterialHandle currentMaterial;
        std::shared_ptr<const MaterialAsset> currentMaterialSnapshot;
        D3D12_GPU_VIRTUAL_ADDRESS materialAddress = 0;
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 5> textures{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, TERRAIN_LAYER_TEXTURE_COUNT> terrainTextures{};
        m_preparedModelDraws.reserve(items.size());

        for (std::size_t itemIndex = 0; itemIndex < items.size();)
        {
            const RenderItem& item = items[itemIndex];
            const std::size_t batchSize = m_enableModelInstancing
                ? m_modelRenderQueue.getInstanceBatchSize(itemIndex) : 1;
            const DX12GraphicsPipeline* requiredPipeline = &m_modelPipeline;
            if (item.pass == RenderPassType::DepthOnly || item.pass == RenderPassType::Shadow)
            {
                requiredPipeline = item.surfaceType == MaterialSurfaceType::AlphaTest
                    ? &m_depthAlphaTestModelPipeline : &m_depthOnlyModelPipeline;
            }
            else if (item.pass == RenderPassType::AlphaTest)
            {
                requiredPipeline = m_deferredFrameActive
                    ? &m_gbufferAlphaTestModelPipeline : &m_alphaTestModelPipeline;
            }
            else if (item.pass == RenderPassType::Transparent)
            {
                requiredPipeline = m_deferredFrameActive
                    ? &m_deferredTransparentModelPipeline : &m_transparentModelPipeline;
            }
            else if (item.pass == RenderPassType::Opaque && m_deferredFrameActive)
            {
                requiredPipeline = &m_gbufferModelPipeline;
            }
            if (currentMaterial != item.material || currentMaterialSnapshot != item.materialSnapshot)
            {
                const MaterialGpuResource* const currentMaterialResource = m_materialGpuCache.getOrCreate(item.material, item.materialSnapshot);
                if (currentMaterialResource == nullptr || currentMaterialResource->constantBuffer.getGpuVirtualAddress() == 0)
                {
                    LOG_ERROR("[Renderer] Failed to resolve material for draw {}.", itemIndex);
                    return false;
                }
                materialAddress = currentMaterialResource->constantBuffer.getGpuVirtualAddress();
                const std::array materialTextures = {
                    currentMaterialResource->baseColorTexture,
                    currentMaterialResource->normalTexture,
                    currentMaterialResource->metallicRoughnessTexture,
                    currentMaterialResource->ambientOcclusionTexture,
                    currentMaterialResource->emissiveTexture,
                };
                for (std::uint32_t textureIndex = 0; textureIndex < materialTextures.size(); ++textureIndex)
                {
                    const Texture* texture = textureManager.get(materialTextures[textureIndex]);
                    const TextureResourceInfo* textureInfo = texture != nullptr ? texture->getResourceInfo() : nullptr;
                    if (textureInfo == nullptr)
                    {
                        LOG_ERROR("[Renderer] Failed to resolve texture {} for draw {}.", textureIndex, itemIndex);
                        return false;
                    }
                    textures[textureIndex] = textureInfo->srv;
                }
                currentMaterial = item.material;
                currentMaterialSnapshot = item.materialSnapshot;
                for (std::size_t index = 0; index < terrainTextures.size(); ++index)
                {
                    const Texture* texture = textureManager.get(currentMaterialResource->terrainTextures[index]);
                    const TextureResourceInfo* info = texture != nullptr ? texture->getResourceInfo() : nullptr;
                    if (info == nullptr)
                    {
                        LOG_ERROR("[Renderer] Failed to resolve terrain texture {} for draw {}.", index, itemIndex);
                        return false;
                    }
                    terrainTextures[index] = info->srv;
                }
            }
            const DX12UploadBuffer* bonePalette = item.bonePaletteBuffer;
            if (item.skinningPalette != nullptr)
            {
                const SkinningPaletteSnapshot* const snapshot = item.skinningPalette.get();
                auto& palettesByObject = uploadedPalettes[snapshot];
                if (const auto found = palettesByObject.find(item.objectID); found != palettesByObject.end())
                {
                    bonePalette = found->second;
                }
                else
                {
                    const auto previous = item.objectID != 0 && trackSkinningHistory
                        ? m_previousSkinningPalettes.find(item.objectID) : m_previousSkinningPalettes.end();
                    const SkinningPaletteSnapshot* const previousSnapshot =
                        previous != m_previousSkinningPalettes.end() ? previous->second.get() : nullptr;
                    if (item.objectID != 0 && trackSkinningHistory)
                        m_currentSkinningPalettes.try_emplace(item.objectID, item.skinningPalette);
                    DX12UploadBuffer* const uploaded = uploadSkinningPalette(
                        frameIndex, *snapshot, previousSnapshot);
                    if (uploaded == nullptr)
                        return false;
                    palettesByObject.emplace(item.objectID, uploaded);
                    bonePalette = uploaded;
                }
            }
            if (bonePalette == nullptr || bonePalette->getGpuVirtualAddress() == 0 || materialAddress == 0)
            {
                LOG_ERROR("[Renderer] Invalid resolved GPU bindings for draw {}.", itemIndex);
                return false;
            }
            const D3D12_VERTEX_BUFFER_VIEW* vertices = item.vertexBuffer;
            const D3D12_VERTEX_BUFFER_VIEW* cachedSkinning = nullptr;
            if (item.hasSkinWeights)
            {
                if (vertices == nullptr || vertices->BufferLocation == 0
                    || vertices->StrideInBytes != sizeof(ModelVertex)
                    || vertices->SizeInBytes == 0 || vertices->SizeInBytes % sizeof(ModelVertex) != 0)
                {
                    LOG_ERROR("[Renderer] Invalid source vertex buffer for skinning draw {}.", itemIndex);
                    return false;
                }
                auto& byPalette = skinnedMeshes[vertices->BufferLocation];
                const auto paletteAddress = bonePalette->getGpuVirtualAddress();
                CachedSkinningBuffer* cached = nullptr;
                if (const auto found = byPalette.find(paletteAddress); found != byPalette.end())
                {
                    cached = found->second;
                }
                else
                {
                    const std::uint64_t vertexCount = vertices->SizeInBytes / sizeof(ModelVertex);
                    const std::uint64_t size = vertexCount * sizeof(CachedSkinningVertex);
                    if (size > (std::numeric_limits<UINT>::max)())
                    {
                        LOG_ERROR("[Renderer] Skinned vertex buffer exceeds DirectX 12 view limits.");
                        return false;
                    }
                    auto& buffers = m_cachedSkinningBuffers[frameIndex];
                    const std::size_t index = m_cachedSkinningBufferCursors[frameIndex];
                    if (index == buffers.size())
                        buffers.push_back(std::make_unique<CachedSkinningBuffer>());
                    cached = buffers[index].get();
                    if (cached->resource.get() == nullptr || cached->resource.getDescription().Width < size)
                    {
                        D3D12_RESOURCE_DESC description{};
                        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                        description.Width = size;
                        description.Height = 1;
                        description.DepthOrArraySize = 1;
                        description.MipLevels = 1;
                        description.SampleDesc = { 1, 0 };
                        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                        if (!cached->resource.initialize(*m_device.get(), {
                            .description = description,
                            .initialState = D3D12_RESOURCE_STATE_COMMON }))
                        {
                            LOG_ERROR("[Renderer] Failed to allocate a skinned vertex buffer.");
                            return false;
                        }
                    }
                    const auto address = cached->resource.get()->GetGPUVirtualAddress();
                    cached->vertices = { address, static_cast<UINT>(size), sizeof(CachedSkinningVertex) };
                    cached->extra = { address + offsetof(CachedSkinningVertex, previousPosition),
                        static_cast<UINT>(size - offsetof(CachedSkinningVertex, previousPosition)),
                        sizeof(CachedSkinningVertex) };
                    cached->source = vertices->BufferLocation;
                    cached->palette = paletteAddress;
                    cached->vertexCount = static_cast<std::uint32_t>(vertexCount);
                    ++m_cachedSkinningBufferCursors[frameIndex];
                    byPalette.emplace(paletteAddress, cached);
                }
                vertices = &cached->vertices;
                cachedSkinning = &cached->extra;
            }
            m_preparedModelDraws.push_back({
                .item = &item,
                .pipeline = requiredPipeline,
                .itemIndex = itemIndex,
                .instanceCount = static_cast<std::uint32_t>(batchSize),
                .instances = instanceBuffer.getGpuVirtualAddress() + itemIndex * sizeof(ModelInstanceConstants),
                .material = materialAddress,
                .bones = bonePalette->getGpuVirtualAddress(),
                .vertices = vertices,
                .cachedSkinning = cachedSkinning,
                .textures = textures,
                .terrainTextures = terrainTextures,
                });
            itemIndex += batchSize;
        }
        return true;
    }

    bool DX12Renderer::recordSkinning(DX12CommandList& commandList, const std::uint32_t frameIndex)
    {
        const std::size_t count = m_cachedSkinningBufferCursors[frameIndex];
        if (count == 0)
            return true;
        if (!m_skinningPipeline.bind(commandList))
            return false;
        ID3D12GraphicsCommandList& native = *commandList.getForRecording();
        for (std::size_t index = 0; index < count; ++index)
        {
            CachedSkinningBuffer& buffer = *m_cachedSkinningBuffers[frameIndex][index];
            if (!buffer.resource.transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
                return false;
            const std::uint32_t groups = (buffer.vertexCount + 63) / 64;
            const std::uint32_t groupsX = std::min(groups, 65535u);
            const std::uint32_t constants[]{ buffer.vertexCount, groupsX };
            native.SetComputeRootShaderResourceView(0, buffer.source);
            native.SetComputeRootUnorderedAccessView(1, buffer.resource.get()->GetGPUVirtualAddress());
            native.SetComputeRootConstantBufferView(2, buffer.palette);
            native.SetComputeRoot32BitConstants(3, 2, constants, 0);
            native.Dispatch(groupsX, (groups + groupsX - 1) / groupsX, 1);
            if (!buffer.resource.transition(commandList, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER))
                return false;
        }
        return true;
    }

    DX12UploadBuffer* DX12Renderer::uploadSkinningPalette(const std::uint32_t frameIndex,
        const SkinningPaletteSnapshot& snapshot, const SkinningPaletteSnapshot* previousSnapshot)
    {
        if (frameIndex >= FRAME_COUNT || snapshot.jointCount == 0
            || snapshot.jointCount > MAX_SKINNING_BONES)
        {
            LOG_ERROR("[Renderer] Invalid skinning palette frame or joint count (frame: {}, joints: {}).",
                frameIndex, snapshot.jointCount);
            return nullptr;
        }

        auto& buffers = m_skinningPaletteBuffers[frameIndex];
        const std::size_t index = m_skinningPaletteBufferCursors[frameIndex];
        if (index == buffers.size())
        {
            auto buffer = std::make_unique<DX12UploadBuffer>();
            if (!buffer->initialize(*m_device.get(), m_directFence, sizeof(SkinningPaletteConstants)))
                return nullptr;
            buffers.push_back(std::move(buffer));
        }

        DX12UploadBuffer* const buffer = buffers[index].get();
        SkinningPaletteConstants constants = snapshot.constants;
        if (previousSnapshot != nullptr && previousSnapshot->skeletonGuid == snapshot.skeletonGuid
            && previousSnapshot->skeletonSignature == snapshot.skeletonSignature
            && previousSnapshot->jointCount == snapshot.jointCount)
            constants.previousBoneMatrices = previousSnapshot->constants.boneMatrices;
        else
            constants.previousBoneMatrices = snapshot.constants.boneMatrices;
        if (!buffer->write(std::as_bytes(std::span{ &constants, 1 })))
            return nullptr;
        ++m_skinningPaletteBufferCursors[frameIndex];
        return buffer;
    }
} // namespace Engine