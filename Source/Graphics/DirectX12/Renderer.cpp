#include "Pch.h"
#include "Graphics\DirectX12\Renderer.h"
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
            || !m_modelGpuCache.initialize(m_device, m_directFence))
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

        ModelRenderSubmissionQueue::instance().clear();
        m_modelRenderQueue.clear();
        if (!m_modelGpuCache.finalize())
            return false;
        if (!TextureManager::instance().finalize())
            return false;

        m_transparentModelPipeline.finalize();
        m_modelPipeline.finalize();
        m_shaderManager.shutdown();
        m_modelVertexShaderID = 0;
        m_modelPixelShaderID = 0;
        m_psoRebuildPending = false;
        for (DX12CommandList& commandList : m_commandLists)
            commandList.finalize();

        m_frameFenceValues.fill(0);
        m_lastSubmittedFenceValue = 0;
        m_renderWidth = 0;
        m_renderHeight = 0;
        m_frustum.reset();
        m_viewProjection = Matrix::Identity;
        m_cameraPosition = Vector3::Zero;
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

        m_shaderManager.processHotReload();

        if (m_psoRebuildPending)
        {
            if (m_lastSubmittedFenceValue != 0)
            {
                m_directFence.waitOnCpu(m_lastSubmittedFenceValue);
            }
            if (rebuildGraphicsPipelines())
            {
                LOG_INFO("[Renderer] Graphics Pipelines successfully rebuilt via Shader Hot Reload.");
            }
            else
            {
                LOG_ERROR("[Renderer] Failed to rebuild Graphics Pipelines during Shader Hot Reload.");
            }
            m_psoRebuildPending = false;
        }

        std::vector<ModelHandle> usedModels;
        buildModelRenderQueue(usedModels);
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

        static constexpr float CLEAR_COLOR[] = { 0.08f, 0.16f, 0.24f, 1.0f };
        ID3D12GraphicsCommandList* const nativeCommandList = commandList.getForRecording();
        const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = m_swapChain.getCurrentRtv().native;
        const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight), 0.0f, 1.0f };
        const D3D12_RECT scissorRect{ 0, 0, static_cast<LONG>(m_renderWidth), static_cast<LONG>(m_renderHeight) };
        nativeCommandList->OMSetRenderTargets(1, &renderTargetView, FALSE, nullptr);
        nativeCommandList->RSSetViewports(1, &viewport);
        nativeCommandList->RSSetScissorRects(1, &scissorRect);
        nativeCommandList->ClearRenderTargetView(renderTargetView, CLEAR_COLOR, 0, nullptr);
        if (!renderModelQueue(commandList))
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
        for (const ModelHandle handle : usedModels)
        {
            if (!m_modelGpuCache.markUsed(handle, submittedFenceValue))
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

        m_frameFenceValues.fill(0);
        m_lastSubmittedFenceValue = 0;
        m_renderWidth = width;
        m_renderHeight = height;
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
        if (!modelVertexShader || !modelPixelShader
            || !modelVertexShader->isCompiled() || !modelPixelShader->isCompiled())
        {
            LOG_ERROR("[DX12] 有効なModel Shaderがロードされていません");
            return false;
        }

        m_modelPipeline.finalize();
        m_transparentModelPipeline.finalize();
        D3D12_ROOT_PARAMETER objectConstants{};
        objectConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        objectConstants.Constants.ShaderRegister = 0;
        objectConstants.Constants.RegisterSpace = 0;
        objectConstants.Constants.Num32BitValues = 20;
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
        const std::array rootParameters = { objectConstants, baseColorTable };
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
        };
        DX12GraphicsPipelineConfig transparentModelConfig = modelConfig;
        transparentModelConfig.enableAlphaBlend = true;
        return m_modelPipeline.initialize(*m_device.get(), modelConfig)
            && m_transparentModelPipeline.initialize(*m_device.get(), transparentModelConfig);
    }

    void DX12Renderer::buildModelRenderQueue(std::vector<ModelHandle>& usedModels)
    {
        m_modelRenderQueue.clear();
        m_frameStatistics = {};
        ModelRenderSubmissionQueue::instance().consume(m_modelSubmissions);
        m_modelRenderQueue.reserve(m_modelSubmissions.size());
        usedModels.reserve(m_modelSubmissions.size());

        for (const ModelRenderSubmission& submission : m_modelSubmissions)
        {
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
                AABB meshWorldBounds;
                mesh.boundingBox.Transform(meshWorldBounds, submission.worldMatrix);

                const auto submitSubMesh = [&](const std::uint32_t indexStart, const std::uint32_t indexCount,
                    const std::uint32_t materialIndex)
                    {
                        if (indexCount == 0 || indexStart > mesh.indices.size()
                            || indexCount > mesh.indices.size() - indexStart)
                            return;

                        const MaterialResource* material = materialIndex < gpuModel->source->materials.size()
                            ? &gpuModel->source->materials[materialIndex] : nullptr;
                        const TextureHandle baseColorTexture = materialIndex < gpuModel->materials.size()
                            ? gpuModel->materials[materialIndex].baseColorTexture
                            : TextureManager::instance().getWhiteTexture();
                        Vector4 baseColor = material != nullptr ? material->baseColor : Vector4::One;
                        if (material != nullptr)
                            baseColor.w *= material->opacity;
                        const RenderPassType pass = baseColor.w < 1.0f
                            ? RenderPassType::Transparent : RenderPassType::Opaque;
                        objectVisible = m_modelRenderQueue.submit(RenderItem{
                            .vertexBuffer = &gpuMesh->vertexBufferView,
                            .indexBuffer = &gpuMesh->indexBufferView,
                            .worldMatrix = submission.worldMatrix,
                            .worldBounds = meshWorldBounds,
                            .baseColor = baseColor,
                            .indexStart = indexStart,
                            .indexCount = indexCount,
                            .objectID = submission.objectID,
                            .pipelineID = static_cast<std::uint32_t>(pass),
                            .materialID = materialIndex,
                            .textureID = baseColorTexture.index,
                            .baseColorTexture = baseColorTexture,
                            .meshID = static_cast<std::uint32_t>(meshIndex),
                            .cameraDepth = (Vector3(meshWorldBounds.Center) - m_cameraPosition).LengthSquared(),
                            .pass = pass,
                        }, m_frustum ? &*m_frustum : nullptr) || objectVisible;
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
        std::uint32_t currentMaterial = UINT32_MAX;
        TextureHandle currentTexture = TextureHandle::Invalid();
        nativeCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        for (const RenderItem& item : items)
        {
            const DX12GraphicsPipeline* const requiredPipeline = item.pass == RenderPassType::Transparent
                ? &m_transparentModelPipeline : &m_modelPipeline;
            if (currentPipeline != requiredPipeline)
            {
                if (!requiredPipeline->bind(commandList))
                    return false;
                currentPipeline = requiredPipeline;
                currentTexture = TextureHandle::Invalid();
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
            if (currentMaterial != item.materialID)
            {
                currentMaterial = item.materialID;
                ++m_frameStatistics.materialSwitchCount;
            }
            if (currentTexture != item.baseColorTexture)
            {
                const Texture* texture = textureManager.get(item.baseColorTexture);
                const TextureResourceInfo* textureInfo = texture != nullptr ? texture->getResourceInfo() : nullptr;
                if (textureInfo == nullptr)
                    return false;
                nativeCommandList->SetGraphicsRootDescriptorTable(1, textureInfo->srv);
                currentTexture = item.baseColorTexture;
                ++m_frameStatistics.textureSwitchCount;
            }

            const Matrix worldViewProjection = item.worldMatrix * m_viewProjection;
            nativeCommandList->SetGraphicsRoot32BitConstants(0, 16, &worldViewProjection._11, 0);
            nativeCommandList->SetGraphicsRoot32BitConstants(0, 4, &item.baseColor.x, 16);
            nativeCommandList->DrawIndexedInstanced(item.indexCount, 1, item.indexStart, item.baseVertex, 0);
            ++m_frameStatistics.drawCallCount;
            ++m_frameStatistics.instanceCount;
        }
        m_frameStatistics.batchCount = m_frameStatistics.drawCallCount;
        return true;
    }
} // namespace Engine