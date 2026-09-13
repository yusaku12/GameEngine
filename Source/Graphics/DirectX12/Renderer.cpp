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
        constexpr std::array MODEL_INPUT_LAYOUT = {
            D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(ModelVertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(ModelVertex, color)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(ModelVertex, texCoord)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, static_cast<UINT>(offsetof(ModelVertex, boneIndices)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(ModelVertex, boneWeights)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
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

        if (!m_dsvHeap.initialize(*m_device.get(), {
                .type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                .capacity = 2,
                .shaderVisible = false,
            })
            || !createDepthBuffer(width, height)
            || !createShadowMap())
        {
            finalize();
            return false;
        }

        for (DX12CommandList& commandList : m_commandLists)
        {
            if (!commandList.initialize(*m_device.get(), DX12CommandQueueType::DIRECT))
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
        return true;
    }

    bool DX12Renderer::finalize()
    {
        if (m_imguiSystem != nullptr)
        {
            m_imguiSystem->finalize();
            m_imguiSystem.reset();
        }

        if (!m_swapChain.finalize(m_directFence, m_lastSubmittedFenceValue))
            return false;

        m_depthBuffer.finalize();
        m_shadowMap.finalize();
        m_dsvHeap.finalize();
        m_depthStencilView = {};
        m_shadowDepthStencilView = {};

        ModelRenderSubmissionQueue::instance().clear();
        m_modelRenderQueue.clear();
        if (!DebugPrimitive::instance().finalize())
            return false;
        if (!m_modelGpuCache.finalize())
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
        m_shaderManager.shutdown();
        m_modelVertexShaderID = 0;
        m_modelPixelShaderID = 0;
        m_alphaTestPixelShaderID = 0;
        m_depthAlphaTestPixelShaderID = 0;
        m_debugVertexShaderID = 0;
        m_debugPixelShaderID = 0;
        m_psoRebuildPending = false;
        for (DX12CommandList& commandList : m_commandLists)
            commandList.finalize();

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
        if (m_imguiSystem == nullptr || !m_imguiSystem->isInitialized())
            return false;

        RenderView submittedView;
        if (CameraRenderSubmissionQueue::instance().consume(submittedView))
            setRenderView(submittedView);

        m_shaderManager.processHotReload();
        m_materialGpuCache.collectGarbage();

        if (m_psoRebuildPending)
        {
            if (m_lastSubmittedFenceValue != 0)
            {
                m_directFence.waitOnCpu(m_lastSubmittedFenceValue);
            }
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
        m_imguiSystem->beginFrame(&m_shaderManager, [this]
            {
                if (ImGui::Begin("Renderer Statistics"))
                {
                    ImGui::Text("Visible Objects: %u", m_statistics.visibleObjects);
                    ImGui::Text("Culled Objects: %u", m_statistics.culledObjects);
                    ImGui::Text("Render Items: %u", m_statistics.renderItemCount);
                    ImGui::Text("Draw Calls: %u", m_statistics.drawCallCount);
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

        DX12CommandList& commandList = m_commandLists[frameIndex];
        DX12Resource* const backBuffer = m_swapChain.getCurrentBackBuffer();
        if (backBuffer == nullptr || !commandList.begin(m_directFence)
            || !backBuffer->transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET))
        {
            return false;
        }

        ID3D12GraphicsCommandList* const nativeCommandList = commandList.getForRecording();
        const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = m_swapChain.getCurrentRtv().native;
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
        if (!renderModelQueue(commandList))
            return false;
        DebugPrimitive::instance().drawGrid(Vector3::Zero, 20.0f, 20.0f, 1.0f);
        if (!DebugPrimitive::instance().render(commandList, frameIndex, m_viewProjection))
            return false;

        m_imguiSystem->render(*nativeCommandList);

        if (!backBuffer->transition(commandList, D3D12_RESOURCE_STATE_PRESENT) || !commandList.close())
            return false;

        ID3D12CommandList* const nativeExecutionList = commandList.getForExecution();
        if (nativeExecutionList == nullptr)
            return false;

        const std::array<ID3D12CommandList*, 1> commandLists = { nativeExecutionList };
        m_directQueue.execute(commandLists);
        const bool presentSucceeded = m_swapChain.present(true);
        if (!presentSucceeded)
            m_device.logDeviceRemovedReason();

        const std::uint64_t submittedFenceValue = m_directFence.signal(*m_directQueue.get());
        if (submittedFenceValue == 0)
        {
            m_device.logDeviceRemovedReason();
            return false;
        }

        if (!commandList.markSubmitted(submittedFenceValue))
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

        m_frameFenceValues[frameIndex] = submittedFenceValue;
        m_lastSubmittedFenceValue = submittedFenceValue;
        m_statistics = m_frameStatistics;
        return presentSucceeded;
    }

    bool DX12Renderer::resize(const std::uint32_t width, const std::uint32_t height)
    {
        if (width == 0 || height == 0)
            return true;

        if (!m_swapChain.resize(*m_device.get(), m_directFence, m_lastSubmittedFenceValue, width, height))
            return false;

        if (!createDepthBuffer(width, height))
            return false;

        m_frameFenceValues.fill(0);
        m_lastSubmittedFenceValue = 0;
        m_renderWidth = width;
        m_renderHeight = height;
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
        if (!modelVertexShader || !modelPixelShader || !alphaTestPixelShader || !depthAlphaTestPixelShader
            || !debugVertexShader || !debugPixelShader
            || !modelVertexShader->isCompiled() || !modelPixelShader->isCompiled()
            || !alphaTestPixelShader->isCompiled() || !depthAlphaTestPixelShader->isCompiled()
            || !debugVertexShader->isCompiled() || !debugPixelShader->isCompiled())
        {
            LOG_ERROR("[DX12] 有効なModel Shaderがロードされていません");
            return false;
        }

        D3D12_ROOT_PARAMETER objectConstants{};
        objectConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        objectConstants.Constants.ShaderRegister = 0;
        objectConstants.Constants.RegisterSpace = 0;
        objectConstants.Constants.Num32BitValues = 16;
        objectConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_DESCRIPTOR_RANGE baseColorRange{};
        baseColorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        baseColorRange.NumDescriptors = 1;
        baseColorRange.BaseShaderRegister = 0;
        baseColorRange.RegisterSpace = 0;
        baseColorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER baseColorTable{};
        baseColorTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        baseColorTable.DescriptorTable.NumDescriptorRanges = 1;
        baseColorTable.DescriptorTable.pDescriptorRanges = &baseColorRange;
        baseColorTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
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
        const std::array rootParameters = { objectConstants, bonePalette, materialConstants, baseColorTable, materialProperties };
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
        if (!nextModelPipeline.initialize(*m_device.get(), opaqueModelConfig)
            || !nextAlphaTestModelPipeline.initialize(*m_device.get(), alphaTestModelConfig)
            || !nextTransparentModelPipeline.initialize(*m_device.get(), transparentModelConfig)
            || !nextDepthOnlyModelPipeline.initialize(*m_device.get(), depthOnlyModelConfig)
            || !nextDepthAlphaTestModelPipeline.initialize(*m_device.get(), depthAlphaTestModelConfig)
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

    bool DX12Renderer::renderModelQueue(DX12CommandList& commandList)
    {
        const std::span<const RenderItem> items = m_modelRenderQueue.getItems();
        if (items.empty())
            return true;

        ID3D12GraphicsCommandList* const nativeCommandList = commandList.getForRecording();
        TextureManager& textureManager = TextureManager::instance();
        ID3D12DescriptorHeap* const descriptorHeap = textureManager.getDescriptorHeap().get();
        if (nativeCommandList == nullptr || descriptorHeap == nullptr)
            return false;
        nativeCommandList->SetDescriptorHeaps(1, &descriptorHeap);

        const DX12GraphicsPipeline* currentPipeline = nullptr;
        const D3D12_VERTEX_BUFFER_VIEW* currentVertexBuffer = nullptr;
        const D3D12_INDEX_BUFFER_VIEW* currentIndexBuffer = nullptr;
        const DX12UploadBuffer* currentBonePalette = nullptr;
        MaterialHandle currentMaterial;
        MaterialGpuResource* currentMaterialResource = nullptr;
        TextureHandle currentTexture = TextureHandle::Invalid();
        RenderPassType targetPass = RenderPassType::Opaque;
        bool shadowMapCleared = false;
        nativeCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        for (const RenderItem& item : items)
        {
            if (item.pass != targetPass)
            {
                if (item.pass == RenderPassType::DepthOnly)
                {
                    nativeCommandList->OMSetRenderTargets(0, nullptr, FALSE, &m_depthStencilView.native);
                }
                else if (item.pass == RenderPassType::Shadow)
                {
                    nativeCommandList->OMSetRenderTargets(0, nullptr, FALSE, &m_shadowDepthStencilView.native);
                    if (!shadowMapCleared)
                    {
                        nativeCommandList->ClearDepthStencilView(
                            m_shadowDepthStencilView.native, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                        shadowMapCleared = true;
                    }
                    constexpr float shadowMapSize = 2048.0f;
                    const D3D12_VIEWPORT shadowViewport{ 0.0f, 0.0f, shadowMapSize, shadowMapSize, 0.0f, 1.0f };
                    const D3D12_RECT shadowScissor{ 0, 0, 2048, 2048 };
                    nativeCommandList->RSSetViewports(1, &shadowViewport);
                    nativeCommandList->RSSetScissorRects(1, &shadowScissor);
                }
                else
                {
                    const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = m_swapChain.getCurrentRtv().native;
                    nativeCommandList->OMSetRenderTargets(1, &renderTargetView, FALSE, &m_depthStencilView.native);
                    const float viewportX = m_cameraViewport.x * static_cast<float>(m_renderWidth);
                    const float viewportY = m_cameraViewport.y * static_cast<float>(m_renderHeight);
                    const D3D12_VIEWPORT viewport{
                        viewportX, viewportY,
                        m_cameraViewport.width * static_cast<float>(m_renderWidth),
                        m_cameraViewport.height * static_cast<float>(m_renderHeight), 0.0f, 1.0f };
                    const D3D12_RECT scissor{
                        static_cast<LONG>(viewport.TopLeftX), static_cast<LONG>(viewport.TopLeftY),
                        static_cast<LONG>(viewport.TopLeftX + viewport.Width),
                        static_cast<LONG>(viewport.TopLeftY + viewport.Height) };
                    nativeCommandList->RSSetViewports(1, &viewport);
                    nativeCommandList->RSSetScissorRects(1, &scissor);
                }
                targetPass = item.pass;
            }

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
            if (currentPipeline != requiredPipeline)
            {
                if (!requiredPipeline->bind(commandList))
                    return false;
                currentPipeline = requiredPipeline;
                currentMaterial = MaterialHandle::Invalid();
                currentMaterialResource = nullptr;
                currentTexture = TextureHandle::Invalid();
                currentBonePalette = nullptr;
                ++m_frameStatistics.psoSwitchCount;
            }
            if (currentVertexBuffer != item.vertexBuffer)
            {
                nativeCommandList->IASetVertexBuffers(0, 1, item.vertexBuffer);
                currentVertexBuffer = item.vertexBuffer;
                ++m_frameStatistics.vertexBufferSwitchCount;
            }
            if (currentIndexBuffer != item.indexBuffer)
            {
                nativeCommandList->IASetIndexBuffer(item.indexBuffer);
                currentIndexBuffer = item.indexBuffer;
                ++m_frameStatistics.indexBufferSwitchCount;
            }
            if (currentMaterial != item.material)
            {
                currentMaterialResource = m_materialGpuCache.getOrCreate(item.material);
                if (currentMaterialResource == nullptr || currentMaterialResource->constantBuffer.getGpuVirtualAddress() == 0)
                    return false;
                nativeCommandList->SetGraphicsRootConstantBufferView(2, currentMaterialResource->constantBuffer.getGpuVirtualAddress());
                currentMaterial = item.material;
                ++m_frameStatistics.materialSwitchCount;
            }
            if (currentBonePalette != item.bonePaletteBuffer)
            {
                if (item.bonePaletteBuffer == nullptr || item.bonePaletteBuffer->getGpuVirtualAddress() == 0)
                    return false;
                nativeCommandList->SetGraphicsRootConstantBufferView(1, item.bonePaletteBuffer->getGpuVirtualAddress());
                currentBonePalette = item.bonePaletteBuffer;
            }
            if (currentMaterialResource == nullptr)
                return false;
            if (currentTexture != currentMaterialResource->baseColorTexture)
            {
                const Texture* texture = textureManager.get(currentMaterialResource->baseColorTexture);
                const TextureResourceInfo* textureInfo = texture != nullptr ? texture->getResourceInfo() : nullptr;
                if (textureInfo == nullptr)
                    return false;
                nativeCommandList->SetGraphicsRootDescriptorTable(3, textureInfo->srv);
                currentTexture = currentMaterialResource->baseColorTexture;
                ++m_frameStatistics.textureSwitchCount;
            }

            const Matrix& viewProjection = item.pass == RenderPassType::Shadow && m_shadowViewProjection
                ? *m_shadowViewProjection : m_viewProjection;
            const Matrix worldViewProjection = item.worldMatrix * viewProjection;
            nativeCommandList->SetGraphicsRoot32BitConstants(0, 16, &worldViewProjection._11, 0);
            const MaterialParameterValues& propertyValues = item.materialProperties.getValues();
            nativeCommandList->SetGraphicsRoot32BitConstants(4, 16, &propertyValues.baseColor.x, 0);
            const std::uint32_t overrideMask = item.materialProperties.getOverrideMask();
            nativeCommandList->SetGraphicsRoot32BitConstants(4, 1, &overrideMask, 16);
            nativeCommandList->DrawIndexedInstanced(item.indexCount, 1, item.indexStart, item.baseVertex, 0);
            ++m_frameStatistics.drawCallCount;
            ++m_frameStatistics.instanceCount;
        }
        m_frameStatistics.batchCount = m_frameStatistics.drawCallCount;
        return true;
    }
} // namespace Engine