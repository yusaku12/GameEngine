#include "Pch.h"
#include "Graphics\DirectX12\ModelCommandRecorder.h"
#include "Graphics\DirectX12\OcclusionQueries.h"
#include "Graphics\DirectX12\Pipeline.h"
#include "Core\Threading\JobSystem.h"

namespace Engine
{
    bool DX12ModelCommandRecorder::record(ID3D12Device& device, const DX12Fence& fence,
        const std::span<const DX12PreparedModelDraw> draws, const DX12ModelRecordingState& state,
        const bool parallel)
    {
        m_statistics = {};
        JobSystem& jobs = JobSystem::instance();
        const std::size_t availableThreads = static_cast<std::size_t>(jobs.getWorkerCount())
            + (JobSystem::isWorkerThread() ? 0u : 1u);
        const std::size_t concurrency = parallel && jobs.isInitialized()
            ? std::clamp(availableThreads, std::size_t{ 1 }, MAX_TASKS_PER_PASS) : 1;
        buildCommandRecordingRanges(draws, concurrency, MIN_DRAWS_PER_TASK,
            [](const DX12PreparedModelDraw& draw) { return draw.item->pass; }, m_ranges);
        if (m_ranges.empty())
            return true;
        if (state.textureHeap == nullptr)
        {
            LOG_ERROR("[Renderer] Missing texture heap for model command recording.");
            return false;
        }
        while (m_contexts.size() < m_ranges.size())
        {
            auto context = std::make_unique<Context>();
            if (!context->commandList.initialize(device, DX12CommandQueueType::DIRECT))
                return false;
            m_contexts.push_back(std::move(context));
        }
        for (std::size_t index = 0; index < m_ranges.size(); ++index)
        {
            m_contexts[index]->statistics = {};
            m_contexts[index]->succeeded = false;
        }

        const auto recordTask = [&](const std::size_t index)
            {
                const CommandRecordingRange& range = m_ranges[index];
                Context& context = *m_contexts[index];
                context.succeeded = recordRange(context, fence, draws.subspan(range.first, range.count), state);
            };
        // Destroy the join guard before the callback or its captured frame data.
        struct RecordingTasks
        {
            JobSystem& jobs;
            JobCounter counter;
            ~RecordingTasks() { jobs.wait(counter); }
        } tasks{ jobs };
        if (concurrency > 1)
        {
            for (std::size_t index = 1; index < m_ranges.size(); ++index)
                jobs.schedule([&, index] { recordTask(index); }, &tasks.counter);
            recordTask(0);
            jobs.wait(tasks.counter);
        }
        else
        {
            for (std::size_t index = 0; index < m_ranges.size(); ++index)
                recordTask(index);
        }

        for (std::size_t index = 0; index < m_ranges.size(); ++index)
        {
            const Context& context = *m_contexts[index];
            if (!context.succeeded)
            {
                LOG_ERROR("[Renderer] Model command recording failed for range {}.", index);
                return false;
            }
            const auto& statistics = context.statistics;
            m_statistics.drawCallCount += statistics.drawCallCount;
            m_statistics.instanceCount += statistics.instanceCount;
            m_statistics.psoSwitchCount += statistics.psoSwitchCount;
            m_statistics.materialSwitchCount += statistics.materialSwitchCount;
            m_statistics.textureSwitchCount += statistics.textureSwitchCount;
            m_statistics.vertexBufferSwitchCount += statistics.vertexBufferSwitchCount;
            m_statistics.indexBufferSwitchCount += statistics.indexBufferSwitchCount;
            bool firstUse = true;
            for (std::size_t previous = 0; previous < index; ++previous)
                if (m_contexts[previous]->threadId == context.threadId)
                {
                    firstUse = false;
                    break;
                }
            if (firstUse)
                ++m_statistics.recordingThreadCount;
        }
        return true;
    }

    bool DX12ModelCommandRecorder::recordRange(Context& context, const DX12Fence& fence,
        const std::span<const DX12PreparedModelDraw> draws, const DX12ModelRecordingState& state)
    {
        DX12CommandList& commandList = context.commandList;
        context.threadId = getCurrentThreadId();
        if (!commandList.begin(fence))
            return false;
        ID3D12GraphicsCommandList& native = *commandList.getForRecording();
        native.SetDescriptorHeaps(1, &state.textureHeap);
        native.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        native.SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
        const RenderPassType pass = draws.front().item->pass;
        if (pass == RenderPassType::Shadow)
        {
            native.OMSetRenderTargets(0, nullptr, FALSE, &state.shadowTarget);
            native.RSSetViewports(1, &state.shadowViewport);
            native.RSSetScissorRects(1, &state.shadowScissor);
        }
        else
        {
            native.OMSetRenderTargets(pass == RenderPassType::DepthOnly ? 0 : 1,
                pass == RenderPassType::DepthOnly ? nullptr : &state.colorTarget, FALSE, &state.depthTarget);
            native.RSSetViewports(1, &state.viewport);
            native.RSSetScissorRects(1, &state.scissor);
        }

        const DX12GraphicsPipeline* pipeline = nullptr;
        const D3D12_VERTEX_BUFFER_VIEW* vertices = nullptr;
        const D3D12_INDEX_BUFFER_VIEW* indices = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS material = 0;
        D3D12_GPU_VIRTUAL_ADDRESS bones = 0;
        for (const DX12PreparedModelDraw& draw : draws)
        {
            const RenderItem& item = *draw.item;
            if (pipeline != draw.pipeline)
            {
                if (!draw.pipeline->bind(commandList))
                    return false;
                pipeline = draw.pipeline;
                material = 0;
                bones = 0;
                ++context.statistics.psoSwitchCount;
            }
            if (vertices != item.vertexBuffer)
            {
                native.IASetVertexBuffers(0, 1, item.vertexBuffer);
                vertices = item.vertexBuffer;
                ++context.statistics.vertexBufferSwitchCount;
            }
            if (indices != item.indexBuffer)
            {
                native.IASetIndexBuffer(item.indexBuffer);
                indices = item.indexBuffer;
                ++context.statistics.indexBufferSwitchCount;
            }
            if (material != draw.material)
            {
                native.SetGraphicsRootConstantBufferView(2, draw.material);
                for (std::uint32_t texture = 0; texture < draw.textures.size(); ++texture)
                    native.SetGraphicsRootDescriptorTable(3 + texture, draw.textures[texture]);
                material = draw.material;
                ++context.statistics.materialSwitchCount;
                context.statistics.textureSwitchCount += static_cast<std::uint32_t>(draw.textures.size());
            }
            if (bones != draw.bones)
            {
                native.SetGraphicsRootConstantBufferView(1, draw.bones);
                bones = draw.bones;
            }
            native.SetGraphicsRootShaderResourceView(0, draw.instances);
            const MaterialParameterValues& properties = item.materialProperties.getValues();
            native.SetGraphicsRoot32BitConstants(8, 16, &properties.baseColor.x, 0);
            const std::uint32_t mask = item.materialProperties.getOverrideMask();
            native.SetGraphicsRoot32BitConstants(8, 1, &mask, 16);
            if (state.queries != nullptr && pass >= RenderPassType::Opaque)
                state.queries->setPredicate(native, draw.itemIndex);
            native.DrawIndexedInstanced(item.indexCount, draw.instanceCount, item.indexStart, item.baseVertex, 0);
            ++context.statistics.drawCallCount;
            context.statistics.instanceCount += draw.instanceCount;
        }
        native.SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
        return commandList.close();
    }

    bool DX12ModelCommandRecorder::markSubmitted(const std::uint64_t fenceValue)
    {
        for (std::size_t index = 0; index < m_ranges.size(); ++index)
            if (!m_contexts[index]->commandList.markSubmitted(fenceValue))
                return false;
        return true;
    }

    ID3D12CommandList* DX12ModelCommandRecorder::getForExecution(const std::size_t index) const noexcept
    {
        return index < m_ranges.size() && index < m_contexts.size()
            ? m_contexts[index]->commandList.getForExecution() : nullptr;
    }

    void DX12ModelCommandRecorder::finalize()
    {
        m_contexts.clear();
        m_ranges.clear();
        m_statistics = {};
    }
} // namespace Engine
