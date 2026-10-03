#include "Pch.h"
#include "Graphics\DirectX12\Renderer.h"
#include "Assets\Material\MaterialManager.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include "Graphics\Renderer\ModelRenderSubmission.h"
#include "Graphics\Texture\TextureManager.h"
#include <imgui.h>

namespace Engine
{
    namespace
    {
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
        };

        bool colorsEqual(const Color& left, const Color& right) noexcept
        {
            return left.x == right.x && left.y == right.y
                && left.z == right.z && left.w == right.w;
        }
    }

    bool DX12Renderer::initialize(const HWND hwnd, const std::uint32_t width, const std::uint32_t height)
    {
        if (hwnd == nullptr || width == 0 || height == 0)
            return false;

        if (!finalize())
            return false;

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
            finalize();
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
            finalize();
            return false;
        }

        if (!m_gameRtvHeap.initialize(*m_device.get(), {
                .type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                .capacity = 1,
                .shaderVisible = false,
            })
            || !m_dsvHeap.initialize(*m_device.get(), {
                .type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                .capacity = 2,
                .shaderVisible = false,
                })
                || !createGameRenderTarget(width, height, m_cameraClearColor)
            || !createDepthBuffer(width, height)
            || !createShadowMap())
        {
            finalize();
            return false;
        }

        for (std::uint32_t frame = 0; frame < FRAME_COUNT; ++frame)
        {
            if (!m_commandLists[frame].initialize(*m_device.get(), DX12CommandQueueType::DIRECT)
                || !m_finishCommandLists[frame].initialize(*m_device.get(), DX12CommandQueueType::DIRECT)
                || !m_queryCommandLists[frame].initialize(*m_device.get(), DX12CommandQueueType::DIRECT))
            {
                finalize();
                return false;
            }
        }

        m_imguiSystem = std::make_unique<ImGuiSystem>();
        if (!m_imguiSystem->initialize(*m_device.get(), *m_directQueue.get(), hwnd))
        {
            finalize();
            return false;
        }
        if (!m_imguiSystem->updateGameTextureView(*m_device.get(), *m_gameRenderTarget.get()))
        {
            finalize();
            return false;
        }

        if (!m_shaderManager.initialize(
            ShaderMode::Development,
            "Assets/Shaders",
            [this](ShaderID /*id*/) { m_psoRebuildPending = true; }))
        {
            LOG_ERROR("[DX12] ShaderManager の初期化に失敗しました");
            finalize();
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
        m_modelPixelShaderID = m_shaderManager.registerShader(modelPixelShaderDesc);
        ShaderCompileDesc alphaTestPixelShaderDesc = modelPixelShaderDesc;
        alphaTestPixelShaderDesc.outputPath = "Assets/Shaders/Compiled/Model_psAlphaTest_ps.cso";
        alphaTestPixelShaderDesc.entryPoint = "psAlphaTest";
        m_alphaTestPixelShaderID = m_shaderManager.registerShader(alphaTestPixelShaderDesc);
        ShaderCompileDesc depthAlphaTestPixelShaderDesc = modelPixelShaderDesc;
        depthAlphaTestPixelShaderDesc.outputPath = "Assets/Shaders/Compiled/Model_psDepthAlphaTest_ps.cso";
        depthAlphaTestPixelShaderDesc.entryPoint = "psDepthAlphaTest";
        m_depthAlphaTestPixelShaderID = m_shaderManager.registerShader(depthAlphaTestPixelShaderDesc);
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
        m_debugPixelShaderID = m_shaderManager.registerShader(debugPixelShaderDesc);
        ShaderCompileDesc occlusionVertexShaderDesc = debugVertexShaderDesc;
        occlusionVertexShaderDesc.sourcePath = "Assets/Shaders/Occlusion.hlsl";
        occlusionVertexShaderDesc.outputPath = "Assets/Shaders/Compiled/Occlusion_vsMain_vs.cso";
        m_occlusionVertexShaderID = m_shaderManager.registerShader(occlusionVertexShaderDesc);

        if (!m_shaderManager.loadAll())
        {
            LOG_ERROR("[DX12] Shader CSO のロードに失敗しました");
            finalize();
            return false;
        }

        if (!rebuildGraphicsPipelines())
        {
            finalize();
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
        if (m_lastSubmittedFenceValue != 0 && !m_directFence.waitOnCpu(m_lastSubmittedFenceValue))
            return false;

        if (m_imguiSystem != nullptr)
        {
            m_imguiSystem->finalize();
            m_imguiSystem.reset();
        }

        if (!m_swapChain.finalize(m_directFence, m_lastSubmittedFenceValue))
            return false;

        m_depthBuffer.finalize();
        m_gameRenderTarget.finalize();
        m_shadowMap.finalize();
        m_gameRtvHeap.finalize();
        m_dsvHeap.finalize();
        m_gameRenderTargetView = {};
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
        for (DX12UploadBuffer& buffer : m_modelInstanceBuffers)
            if (!buffer.finalize())
                return false;
        m_modelInstanceConstants.clear();
        for (DX12OcclusionQueries& queries : m_occlusionQueries)
            if (!queries.finalize())
                return false;
        if (!m_materialGpuCache.finalize())
            return false;
        if (!TextureManager::instance().finalize())
            return false;

        m_transparentModelPipeline.finalize();
        m_alphaTestModelPipeline.finalize();
        m_depthOnlyModelPipeline.finalize();
        m_depthAlphaTestModelPipeline.finalize();
        m_modelPipeline.finalize();
        m_occlusionPipeline.finalize();
        m_shaderManager.shutdown();
        m_modelVertexShaderID = 0;
        m_modelPixelShaderID = 0;
        m_alphaTestPixelShaderID = 0;
        m_depthAlphaTestPixelShaderID = 0;
        m_debugVertexShaderID = 0;
        m_debugPixelShaderID = 0;
        m_occlusionVertexShaderID = 0;
        m_psoRebuildPending = false;
        for (DX12CommandList& commandList : m_commandLists)
            commandList.finalize();
        for (DX12CommandList& commandList : m_finishCommandLists)
            commandList.finalize();
        for (DX12CommandList& commandList : m_queryCommandLists)
            commandList.finalize();
        for (DX12ModelCommandRecorder& recorder : m_modelCommandRecorders)
            recorder.finalize();
        m_preparedModelDraws.clear();
        m_executionLists.clear();
        m_renderFailed = false;

        m_frameFenceValues.fill(0);
        m_lastSubmittedFenceValue = 0;
        m_renderWidth = 0;
        m_renderHeight = 0;
        m_frustum.reset();
        m_shadowViewProjection.reset();
        m_viewProjection = Matrix::Identity;
        m_cameraPosition = Vector3::Zero;
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
        if (m_imguiSystem == nullptr || !m_imguiSystem->isInitialized())
            return false;

        RenderView submittedView;
        if (CameraRenderSubmissionQueue::instance().consume(submittedView))
            setRenderView(submittedView);

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
            if (!createGameRenderTarget(m_requestedGameWidth, m_requestedGameHeight, m_cameraClearColor)
                || !m_imguiSystem->updateGameTextureView(*m_device.get(), *m_gameRenderTarget.get()))
            {
                return false;
            }
            if (renderTargetNeedsResize && !createDepthBuffer(m_requestedGameWidth, m_requestedGameHeight))
                return false;
            m_renderWidth = m_requestedGameWidth;
            m_renderHeight = m_requestedGameHeight;
        }

        m_shaderManager.processHotReload();
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

        std::vector<ModelHandle> usedModels;
        std::vector<MaterialHandle> usedMaterials;
        buildModelRenderQueue(usedModels, usedMaterials);
        m_imguiSystem->beginFrame(&m_shaderManager,
            m_imguiSystem->getGameTextureId(), m_requestedGameWidth, m_requestedGameHeight, [this]
            {
                if (ImGui::Begin("Renderer Statistics"))
                {
                    ImGui::Text("Visible Objects: %u", m_statistics.visibleObjects);
                    ImGui::Text("Culled Objects: %u", m_statistics.culledObjects);
                    ImGui::Text("Render Items: %u", m_statistics.renderItemCount);
                    ImGui::Text("Submitted Model Draw Calls: %u", m_statistics.drawCallCount);
                    ImGui::Text("Submitted Model Batches: %u", m_statistics.batchCount);
                    ImGui::Text("Submitted Model Instances (all passes): %u", m_statistics.instanceCount);
                    ImGui::Checkbox("Model Instancing", &m_enableModelInstancing);
                    ImGui::Checkbox("Parallel Model Recording", &m_enableParallelModelRecording);
                    ImGui::Text("Model Command Lists: %u", m_statistics.modelCommandListCount);
                    ImGui::Text("Model Recording Threads: %u", m_statistics.modelRecordingThreadCount);
                    ImGui::Checkbox("GPU Occlusion Culling", &m_enableOcclusionCulling);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Same-frame color-pass culling. Tests batches with at least 256 indices; up to 4096 queries per frame. Compare GPU performance with this disabled.");
                    ImGui::Text("Occlusion Queries (last submitted frame): %u", m_statistics.occlusionQueryCount);
                    ImGui::Text("Occluded Batches (completed slot): %u / %u",
                        m_statistics.occludedBatches, m_statistics.completedOcclusionQueries);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Completed GPU results are statistics only. Color draws use same-frame predicates; depth and shadows always draw.");
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

        const std::uint64_t frameFenceValue = m_frameFenceValues[frameIndex];
        if (frameFenceValue != 0 && !m_directFence.isComplete(frameFenceValue)
            && !m_directFence.waitOnCpu(frameFenceValue))
        {
            return false;
        }
        if (!m_occlusionQueries[frameIndex].collectCompletedResults(
            m_frameStatistics.completedOcclusionQueries, m_frameStatistics.occludedBatches))
            return false;

        DX12CommandList& commandList = m_commandLists[frameIndex];
        DX12Resource* const backBuffer = m_swapChain.getCurrentBackBuffer();
        if (backBuffer == nullptr || !commandList.begin(m_directFence)
            || !backBuffer->transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET)
            || !m_gameRenderTarget.transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET))
        {
            return false;
        }

        ID3D12GraphicsCommandList* nativeCommandList = commandList.getForRecording();
        const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = m_gameRenderTargetView.native;
        const float viewportX = m_cameraViewport.x * static_cast<float>(m_renderWidth);
        const float viewportY = m_cameraViewport.y * static_cast<float>(m_renderHeight);
        const float viewportWidth = m_cameraViewport.width * static_cast<float>(m_renderWidth);
        const float viewportHeight = m_cameraViewport.height * static_cast<float>(m_renderHeight);
        const D3D12_VIEWPORT viewport{ viewportX, viewportY, viewportWidth, viewportHeight, 0.0f, 1.0f };
        const D3D12_RECT scissorRect{
            static_cast<LONG>(viewportX),
            static_cast<LONG>(viewportY),
            static_cast<LONG>(viewportX + viewportWidth),
            static_cast<LONG>(viewportY + viewportHeight)
        };
        nativeCommandList->OMSetRenderTargets(1, &renderTargetView, FALSE, &m_depthStencilView.native);
        nativeCommandList->RSSetViewports(1, &viewport);
        nativeCommandList->RSSetScissorRects(1, &scissorRect);
        if (m_cameraClearMode == CameraClearMode::SolidColor || m_cameraClearMode == CameraClearMode::Skybox)
            nativeCommandList->ClearRenderTargetView(renderTargetView, &m_cameraClearColor.x, 0, nullptr);
        nativeCommandList->ClearDepthStencilView(
            m_depthStencilView.native, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        m_skinningPaletteBufferCursors[frameIndex] = 0;
        if (!renderModelQueue(commandList, frameIndex))
            return false;

        DX12CommandList& finishList = m_finishCommandLists[frameIndex];
        if (!finishList.begin(m_directFence))
            return false;
        nativeCommandList = finishList.getForRecording();
        nativeCommandList->OMSetRenderTargets(1, &renderTargetView, FALSE, &m_depthStencilView.native);
        nativeCommandList->RSSetViewports(1, &viewport);
        nativeCommandList->RSSetScissorRects(1, &scissorRect);
        DebugPrimitive::instance().drawGrid(Vector3::Zero, 20.0f, 20.0f, 1.0f);
        if (!DebugPrimitive::instance().render(finishList, frameIndex, m_viewProjection))
            return false;

        if (!m_gameRenderTarget.transition(finishList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
            return false;

        const D3D12_CPU_DESCRIPTOR_HANDLE backBufferView = m_swapChain.getCurrentRtv().native;
        nativeCommandList->OMSetRenderTargets(1, &backBufferView, FALSE, nullptr);
        m_imguiSystem->render(*nativeCommandList);

        if (!backBuffer->transition(finishList, D3D12_RESOURCE_STATE_PRESENT) || !finishList.close())
            return false;

        ID3D12CommandList* const nativeExecutionList = finishList.getForExecution();
        if (nativeExecutionList == nullptr)
            return false;

        m_executionLists.push_back(nativeExecutionList);
        m_directQueue.execute(m_executionLists);
        const bool presentSucceeded = m_swapChain.present(true);
        if (!presentSucceeded)
            m_device.logDeviceRemovedReason();

        const std::uint64_t submittedFenceValue = m_directFence.signal(*m_directQueue.get());
        if (submittedFenceValue == 0)
        {
            m_device.logDeviceRemovedReason();
            return false;
        }

        // Keep shutdown safe even if resource bookkeeping below fails.
        m_frameFenceValues[frameIndex] = submittedFenceValue;
        m_lastSubmittedFenceValue = submittedFenceValue;
        if (!commandList.markSubmitted(submittedFenceValue)
            || !finishList.markSubmitted(submittedFenceValue)
            || !m_modelCommandRecorders[frameIndex].markSubmitted(submittedFenceValue)
            || (m_frameStatistics.occlusionQueryCount != 0
                && !m_queryCommandLists[frameIndex].markSubmitted(submittedFenceValue)))
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
        if (!m_occlusionQueries[frameIndex].markSubmitted(submittedFenceValue))
            return false;

        m_statistics = m_frameStatistics;
        return presentSucceeded;
    }

    bool DX12Renderer::resize(const std::uint32_t width, const std::uint32_t height)
    {
        if (width == 0 || height == 0)
            return true;

        if (!m_swapChain.resize(*m_device.get(), m_directFence, m_lastSubmittedFenceValue, width, height))
            return false;

        m_frameFenceValues.fill(0);
        m_lastSubmittedFenceValue = 0;
        return true;
    }

    bool DX12Renderer::createGameRenderTarget(
        const std::uint32_t width, const std::uint32_t height, const Color& clearColor)
    {
        if (m_device.get() == nullptr || m_gameRtvHeap.get() == nullptr || width == 0 || height == 0)
            return false;

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
        m_viewProjection = view.camera.viewProjection;
        m_cameraPosition = view.camera.position;
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
        description.Format = DXGI_FORMAT_D32_FLOAT;
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
        m_device.get()->CreateDepthStencilView(m_depthBuffer.get(), nullptr, m_depthStencilView.native);
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
        description.Format = DXGI_FORMAT_D32_FLOAT;
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
        m_device.get()->CreateDepthStencilView(m_shadowMap.get(), nullptr, m_shadowDepthStencilView.native);
        return true;
    }

    bool DX12Renderer::processImGuiMessage(const HWND hwnd, const UINT message, const WPARAM wparam, const LPARAM lparam)
    {
        return m_imguiSystem != nullptr && m_imguiSystem->processMessage(hwnd, message, wparam, lparam);
    }

    bool DX12Renderer::rebuildGraphicsPipelines()
    {
        const auto modelVertexShader = m_shaderManager.get(m_modelVertexShaderID);
        const auto modelPixelShader = m_shaderManager.get(m_modelPixelShaderID);
        const auto alphaTestPixelShader = m_shaderManager.get(m_alphaTestPixelShaderID);
        const auto depthAlphaTestPixelShader = m_shaderManager.get(m_depthAlphaTestPixelShaderID);
        const auto debugVertexShader = m_shaderManager.get(m_debugVertexShaderID);
        const auto debugPixelShader = m_shaderManager.get(m_debugPixelShaderID);
        const auto occlusionVertexShader = m_shaderManager.get(m_occlusionVertexShaderID);
        if (!modelVertexShader || !modelPixelShader || !alphaTestPixelShader || !depthAlphaTestPixelShader
            || !debugVertexShader || !debugPixelShader || !occlusionVertexShader
            || !modelVertexShader->isCompiled() || !modelPixelShader->isCompiled()
            || !alphaTestPixelShader->isCompiled() || !depthAlphaTestPixelShader->isCompiled()
            || !debugVertexShader->isCompiled() || !debugPixelShader->isCompiled()
            || !occlusionVertexShader->isCompiled())
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
        const std::array rootParameters = {
            objectConstants, bonePalette, materialConstants,
            textureTables[0], textureTables[1], textureTables[2], textureTables[3], textureTables[4],
            materialProperties,
        };
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
        D3D12_ROOT_PARAMETER queryConstants{};
        queryConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        queryConstants.Constants.Num32BitValues = 5;
        queryConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        const std::array queryParameters = { queryConstants };
        const DX12GraphicsPipelineConfig occlusionConfig{
            .vertexShader = occlusionVertexShader.get(),
            .rootParameters = queryParameters,
            .renderTargetFormat = DXGI_FORMAT_UNKNOWN,
            .depthStencilFormat = DXGI_FORMAT_D32_FLOAT,
            .depthComparison = D3D12_COMPARISON_FUNC_LESS_EQUAL,
            .enableDepthWrite = false,
            .cullMode = D3D12_CULL_MODE_NONE,
        };
        DX12GraphicsPipeline nextOcclusionPipeline;
        if (!nextModelPipeline.initialize(*m_device.get(), opaqueModelConfig)
            || !nextAlphaTestModelPipeline.initialize(*m_device.get(), alphaTestModelConfig)
            || !nextTransparentModelPipeline.initialize(*m_device.get(), transparentModelConfig)
            || !nextDepthOnlyModelPipeline.initialize(*m_device.get(), depthOnlyModelConfig)
            || !nextDepthAlphaTestModelPipeline.initialize(*m_device.get(), depthAlphaTestModelConfig)
            || !nextOcclusionPipeline.initialize(*m_device.get(), occlusionConfig)
            || !DebugPrimitive::instance().rebuildPipeline(*debugVertexShader, *debugPixelShader)
            || !m_materialGpuCache.rebuildAll())
        {
            return false;
        }
        m_modelPipeline.swap(nextModelPipeline);
        m_alphaTestModelPipeline.swap(nextAlphaTestModelPipeline);
        m_transparentModelPipeline.swap(nextTransparentModelPipeline);
        m_depthOnlyModelPipeline.swap(nextDepthOnlyModelPipeline);
        m_depthAlphaTestModelPipeline.swap(nextDepthAlphaTestModelPipeline);
        m_occlusionPipeline.swap(nextOcclusionPipeline);
        return true;
    }

    void DX12Renderer::buildModelRenderQueue(std::vector<ModelHandle>& usedModels, std::vector<MaterialHandle>& usedMaterials)
    {
        m_modelRenderQueue.clear();
        m_frameStatistics = {};
        ModelRenderSubmissionQueue::instance().consume(m_modelSubmissions);
        m_modelRenderQueue.reserve(m_modelSubmissions.size());
        usedModels.reserve(m_modelSubmissions.size());
        usedMaterials.reserve(m_modelSubmissions.size());

        for (const ModelRenderSubmission& submission : m_modelSubmissions)
        {
            if (submission.layer >= 32
                || (m_cameraCullingMask & (1u << submission.layer)) == 0)
            {
                ++m_frameStatistics.culledObjects;
                continue;
            }

            if (m_frustum && !isVisible(*m_frustum, submission.worldBounds))
            {
                ++m_frameStatistics.culledObjects;
                continue;
            }

            ModelGpuResource* const gpuModel = m_modelGpuCache.getOrCreate(submission.model);
            if (gpuModel == nullptr || gpuModel->source == nullptr)
                continue;

            bool objectVisible = false;
            for (std::size_t meshIndex = 0; meshIndex < gpuModel->meshes.size(); ++meshIndex)
            {
                const std::unique_ptr<ModelGpuMesh>& gpuMesh = gpuModel->meshes[meshIndex];
                if (gpuMesh == nullptr)
                    continue;
                const MeshResource& mesh = gpuModel->source->meshes[meshIndex];
                const Matrix meshWorldMatrix = gpuMesh->nodeTransform * submission.worldMatrix;
                AABB meshWorldBounds;
                mesh.boundingBox.Transform(meshWorldBounds, meshWorldMatrix);

                const auto submitSubMesh = [&](const std::uint32_t indexStart, const std::uint32_t indexCount,
                    const std::uint32_t materialIndex)
                    {
                        if (indexCount == 0 || indexStart > mesh.indices.size()
                            || indexCount > mesh.indices.size() - indexStart)
                            return;

                        MaterialHandle materialHandle = materialIndex < submission.materials.size()
                            ? submission.materials[materialIndex] : MaterialHandle::Invalid();
                        MaterialGpuResource* const gpuMaterial = m_materialGpuCache.getOrCreate(materialHandle);
                        if (gpuMaterial == nullptr || gpuMaterial->source == nullptr)
                            return;
                        materialHandle = gpuMaterial->handle;
                        RenderPassType pass = RenderPassType::Opaque;
                        if (gpuMaterial->source->renderState.surfaceType == MaterialSurfaceType::AlphaTest)
                            pass = RenderPassType::AlphaTest;
                        else if (gpuMaterial->source->renderState.surfaceType == MaterialSurfaceType::Transparent)
                            pass = RenderPassType::Transparent;
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
                            .surfaceType = gpuMaterial->source->renderState.surfaceType,
                            .materialProperties = submission.materialProperties,
                            .textureID = gpuMaterial->baseColorTexture.index,
                            .skinningPalette = submission.skinningPalette,
                            .bonePaletteBuffer = &gpuModel->bonePaletteBuffer,
                            .meshID = static_cast<std::uint32_t>(meshIndex),
                            .cameraDepth = (Vector3(meshWorldBounds.Center) - m_cameraPosition).LengthSquared(),
                            .pass = pass,
                        };
                        const bool submitted = m_modelRenderQueue.submit(colorItem, m_frustum ? &*m_frustum : nullptr);
                        if (submitted && pass != RenderPassType::Transparent)
                        {
                            RenderItem depthItem = colorItem;
                            depthItem.pipelineID = depthItem.surfaceType == MaterialSurfaceType::AlphaTest ? 1u : 0u;
                            depthItem.pass = RenderPassType::DepthOnly;
                            m_modelRenderQueue.submit(depthItem);
                            if (m_shadowViewProjection && submission.castShadows)
                            {
                                RenderItem shadowItem = depthItem;
                                shadowItem.pass = RenderPassType::Shadow;
                                m_modelRenderQueue.submit(shadowItem);
                            }
                        }
                        objectVisible = submitted || objectVisible;
                        if (submitted && std::find(usedMaterials.begin(), usedMaterials.end(), materialHandle) == usedMaterials.end())
                            usedMaterials.push_back(materialHandle);
                    };

                if (mesh.subMeshes.empty())
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
                if (std::find(usedModels.begin(), usedModels.end(), submission.model) == usedModels.end())
                    usedModels.push_back(submission.model);
            }
        }

        m_modelRenderQueue.sort();
        m_frameStatistics.renderItemCount = static_cast<std::uint32_t>(m_modelRenderQueue.getItems().size());
    }

    bool DX12Renderer::renderModelQueue(DX12CommandList& commandList, const std::uint32_t frameIndex)
    {
        m_executionLists.clear();
        if (!prepareModelDraws(frameIndex))
            return false;
        const bool hasShadow = std::any_of(m_preparedModelDraws.begin(), m_preparedModelDraws.end(),
            [](const DX12PreparedModelDraw& draw) { return draw.item->pass == RenderPassType::Shadow; });
        if (hasShadow)
            commandList.getForRecording()->ClearDepthStencilView(
                m_shadowDepthStencilView.native, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        if (!commandList.close())
            return false;
        m_executionLists.push_back(commandList.getForExecution());

        DX12ModelRecordingState state{
            .textureHeap = TextureManager::instance().getDescriptorHeap().get(),
            .colorTarget = m_gameRenderTargetView.native,
            .depthTarget = m_depthStencilView.native,
            .shadowTarget = m_shadowDepthStencilView.native,
            .viewport = {
                m_cameraViewport.x * static_cast<float>(m_renderWidth),
                m_cameraViewport.y * static_cast<float>(m_renderHeight),
                m_cameraViewport.width * static_cast<float>(m_renderWidth),
                m_cameraViewport.height * static_cast<float>(m_renderHeight), 0.0f, 1.0f },
        };
        state.scissor = {
            static_cast<LONG>(state.viewport.TopLeftX), static_cast<LONG>(state.viewport.TopLeftY),
            static_cast<LONG>(state.viewport.TopLeftX + state.viewport.Width),
            static_cast<LONG>(state.viewport.TopLeftY + state.viewport.Height) };
        const D3D12_RESOURCE_DESC& shadowDescription = m_shadowMap.getDescription();
        state.shadowViewport = { 0.0f, 0.0f, static_cast<float>(shadowDescription.Width),
            static_cast<float>(shadowDescription.Height), 0.0f, 1.0f };
        state.shadowScissor = { 0, 0, static_cast<LONG>(shadowDescription.Width),
            static_cast<LONG>(shadowDescription.Height) };

        const bool hasColor = std::any_of(m_preparedModelDraws.begin(), m_preparedModelDraws.end(),
            [](const DX12PreparedModelDraw& draw) { return draw.item->pass >= RenderPassType::Opaque; });
        DX12CommandList& queryList = m_queryCommandLists[frameIndex];
        if (hasColor && m_enableOcclusionCulling)
        {
            if (!queryList.begin(m_directFence))
                return false;
            ID3D12GraphicsCommandList& native = *queryList.getForRecording();
            native.OMSetRenderTargets(0, nullptr, FALSE, &state.depthTarget);
            native.RSSetViewports(1, &state.viewport);
            native.RSSetScissorRects(1, &state.scissor);
            DX12OcclusionQueries& queries = m_occlusionQueries[frameIndex];
            if (!queries.record(queryList, *m_device.get(), m_directFence, m_occlusionPipeline,
                m_modelRenderQueue, m_viewProjection, Vector2(state.viewport.Width, state.viewport.Height),
                m_enableModelInstancing) || !queryList.close())
                return false;
            m_frameStatistics.occlusionQueryCount = queries.getQueryCount();
            if (queries.getQueryCount() != 0)
                state.queries = &queries;
        }

        DX12ModelCommandRecorder& recorder = m_modelCommandRecorders[frameIndex];
        if (!recorder.record(*m_device.get(), m_directFence, m_preparedModelDraws,
            state, m_enableParallelModelRecording))
            return false;
        bool queryInserted = state.queries == nullptr;
        const auto ranges = recorder.getRanges();
        for (std::size_t index = 0; index < ranges.size(); ++index)
        {
            const RenderPassType pass = m_preparedModelDraws[ranges[index].first].item->pass;
            if (!queryInserted && pass >= RenderPassType::Opaque)
            {
                m_executionLists.push_back(queryList.getForExecution());
                queryInserted = true;
            }
            ID3D12CommandList* const native = recorder.getForExecution(index);
            if (native == nullptr)
            {
                LOG_ERROR("[Renderer] Model command list {} was not closed.", index);
                return false;
            }
            m_executionLists.push_back(native);
        }
        const auto& statistics = recorder.getStatistics();
        m_frameStatistics.drawCallCount = statistics.drawCallCount;
        m_frameStatistics.batchCount = statistics.drawCallCount;
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
            const Matrix& viewProjection = item.pass == RenderPassType::Shadow && m_shadowViewProjection
                ? *m_shadowViewProjection : m_viewProjection;
            m_modelInstanceConstants[index] = { item.worldMatrix * viewProjection, item.worldMatrix };
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
        std::unordered_map<const SkinningPaletteSnapshot*, DX12UploadBuffer*> uploadedPalettes;
        MaterialHandle currentMaterial;
        D3D12_GPU_VIRTUAL_ADDRESS materialAddress = 0;
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 5> textures{};
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
                requiredPipeline = &m_alphaTestModelPipeline;
            }
            else if (item.pass == RenderPassType::Transparent)
            {
                requiredPipeline = &m_transparentModelPipeline;
            }
            if (currentMaterial != item.material)
            {
                const MaterialGpuResource* const currentMaterialResource = m_materialGpuCache.getOrCreate(item.material);
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
            }
            const DX12UploadBuffer* bonePalette = item.bonePaletteBuffer;
            if (item.skinningPalette != nullptr)
            {
                const SkinningPaletteSnapshot* const snapshot = item.skinningPalette.get();
                if (const auto found = uploadedPalettes.find(snapshot); found != uploadedPalettes.end())
                {
                    bonePalette = found->second;
                }
                else
                {
                    DX12UploadBuffer* const uploaded = uploadSkinningPalette(frameIndex, *snapshot);
                    if (uploaded == nullptr)
                        return false;
                    uploadedPalettes.emplace(snapshot, uploaded);
                    bonePalette = uploaded;
                }
            }
            if (bonePalette == nullptr || bonePalette->getGpuVirtualAddress() == 0 || materialAddress == 0)
            {
                LOG_ERROR("[Renderer] Invalid resolved GPU bindings for draw {}.", itemIndex);
                return false;
            }
            m_preparedModelDraws.push_back({
                .item = &item,
                .pipeline = requiredPipeline,
                .itemIndex = itemIndex,
                .instanceCount = static_cast<std::uint32_t>(batchSize),
                .instances = instanceBuffer.getGpuVirtualAddress() + itemIndex * sizeof(ModelInstanceConstants),
                .material = materialAddress,
                .bones = bonePalette->getGpuVirtualAddress(),
                .textures = textures,
            });
            itemIndex += batchSize;
        }
        return true;
    }

    DX12UploadBuffer* DX12Renderer::uploadSkinningPalette(const std::uint32_t frameIndex, const SkinningPaletteSnapshot& snapshot)
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
        if (!buffer->write(std::as_bytes(std::span{ &snapshot.constants, 1 })))
            return nullptr;
        ++m_skinningPaletteBufferCursors[frameIndex];
        return buffer;
    }
} // namespace Engine