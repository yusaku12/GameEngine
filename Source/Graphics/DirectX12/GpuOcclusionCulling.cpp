#include "Pch.h"
#include <bit>
#include "Graphics\DirectX12\GpuOcclusionCulling.h"
#include "Graphics\DirectX12\Command.h"
#include "Graphics\DirectX12\Pipeline.h"
#include "Graphics\Renderer\RenderQueue.h"

namespace Engine
{
    namespace
    {
        D3D12_RESOURCE_DESC bufferDescription(const std::uint64_t size)
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
            return description;
        }

        void transitionMip(ID3D12GraphicsCommandList& commandList, ID3D12Resource& resource,
            const std::uint32_t mip, const D3D12_RESOURCE_STATES before, const D3D12_RESOURCE_STATES after)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition = { &resource, mip, before, after };
            commandList.ResourceBarrier(1, &barrier);
        }
    }

    bool DX12GpuOcclusionCulling::createPipelines(ID3D12Device& device, const DX12Shader& depthShader,
        const DX12Shader& cullShader, DX12ComputePipeline& depthPipeline, DX12ComputePipeline& cullPipeline)
    {
        const D3D12_DESCRIPTOR_RANGE sourceRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0 };
        const D3D12_DESCRIPTOR_RANGE targetRange{ D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 };
        std::array<D3D12_ROOT_PARAMETER, 3> depthParameters{};
        depthParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        depthParameters[0].Constants = { 0, 0, 5 };
        depthParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        depthParameters[1].DescriptorTable = { 1, &sourceRange };
        depthParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        depthParameters[2].DescriptorTable = { 1, &targetRange };
        std::array<D3D12_ROOT_PARAMETER, 7> cullParameters{};
        cullParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        cullParameters[0].Constants = { 0, 0, 24 };
        cullParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        cullParameters[1].DescriptorTable = { 1, &sourceRange };
        for (std::uint32_t index = 2; index < 5; ++index)
        {
            cullParameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            cullParameters[index].Descriptor.ShaderRegister = index - 1;
        }
        for (std::uint32_t index = 5; index < 7; ++index)
        {
            cullParameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            cullParameters[index].Descriptor.ShaderRegister = index - 5;
        }
        return depthPipeline.initialize(device, depthShader, depthParameters)
            && cullPipeline.initialize(device, cullShader, cullParameters);
    }

    bool DX12GpuOcclusionCulling::collectCompletedResults(GpuOcclusionStatistics& statistics)
    {
        statistics = {};
        if (m_recorded && !m_pending)
        {
            LOG_ERROR("[Occlusion] Cannot reuse recorded culling resources before submission.");
            return false;
        }
        if (m_pending)
        {
            m_completedCommands.resize(m_batches.size());
            if (!m_readback.read(std::as_writable_bytes(std::span(m_completedCommands))))
                return false;
            statistics.batches = getBatchCount();
            for (std::size_t index = 0; index < m_batches.size(); ++index)
            {
                const Command& command = m_completedCommands[index];
                const Batch& batch = m_batches[index];
                if (command.draw.InstanceCount > batch.instanceCount
                    || command.count != (command.draw.InstanceCount != 0 ? 1u : 0u)
                    || command.draw.IndexCountPerInstance != batch.indexCount
                    || command.draw.StartIndexLocation != batch.indexStart
                    || command.draw.BaseVertexLocation != batch.baseVertex
                    || command.draw.StartInstanceLocation != 0)
                {
                    LOG_ERROR("[Occlusion] Invalid GPU indirect arguments for batch {}.", index);
                    return false;
                }
                statistics.instances += batch.instanceCount;
                statistics.occludedInstances += batch.instanceCount - command.draw.InstanceCount;
                if (command.count == 0)
                    ++statistics.occludedBatches;
            }
        }
        m_pending = false;
        m_recorded = false;
        m_batches.clear();
        m_candidates.clear();
        m_itemBatches.clear();
        return true;
    }

    bool DX12GpuOcclusionCulling::ensureCapacity(ID3D12Device& device, const DX12Fence& fence,
        const std::uint32_t itemCount)
    {
        if (m_signature == nullptr)
        {
            D3D12_INDIRECT_ARGUMENT_DESC argument{};
            argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
            const D3D12_COMMAND_SIGNATURE_DESC description{ sizeof(Command), 1, &argument, 0 };
            const HRESULT result = device.CreateCommandSignature(&description, nullptr, IID_PPV_ARGS(&m_signature));
            if (FAILED(result))
            {
                LOG_ERROR("[Occlusion] Indirect command signature creation failed (HRESULT: 0x{:08X}).",
                    static_cast<unsigned long>(result));
                return false;
            }
        }
        if (itemCount <= m_capacity)
            return true;
        const std::uint32_t capacity = std::min(UINT32_MAX / INSTANCE_STRIDE,
            std::max(itemCount, std::max(256u, m_capacity * 2)));
        m_capacity = 0;
        if (!m_candidateUpload.initialize(device, fence, static_cast<std::uint64_t>(capacity) * sizeof(Candidate))
            || !m_batchUpload.initialize(device, fence, static_cast<std::uint64_t>(capacity) * sizeof(Batch))
            || !m_readback.initialize(device, fence, static_cast<std::uint64_t>(capacity) * sizeof(Command))
            || !m_visibleInstances.initialize(device, {
                .description = bufferDescription(static_cast<std::uint64_t>(capacity) * INSTANCE_STRIDE) })
            || !m_commands.initialize(device, {
                .description = bufferDescription(static_cast<std::uint64_t>(capacity) * sizeof(Command)) }))
        {
            LOG_ERROR("[Occlusion] Failed to allocate GPU culling buffers.");
            return false;
        }
        m_capacity = capacity;
        return true;
    }

    bool DX12GpuOcclusionCulling::ensureHierarchy(ID3D12Device& device, DX12Resource& depth)
    {
        const auto& description = depth.getDescription();
        const std::uint32_t width = static_cast<std::uint32_t>(description.Width);
        const std::uint32_t height = description.Height;
        if (m_hierarchy.get() == nullptr || m_depthWidth != width || m_depthHeight != height)
        {
            m_width = std::bit_ceil(width);
            m_height = std::bit_ceil(height);
            m_mipCount = std::bit_width(std::max(m_width, m_height));
            D3D12_RESOURCE_DESC hierarchyDescription = description;
            hierarchyDescription.Width = m_width;
            hierarchyDescription.Height = m_height;
            hierarchyDescription.Format = DXGI_FORMAT_R32_FLOAT;
            hierarchyDescription.MipLevels = static_cast<UINT16>(m_mipCount);
            hierarchyDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            if (!m_hierarchy.initialize(device, {
                .description = hierarchyDescription,
                .initialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE })
                || !m_heap.initialize(device, { .capacity = 2 + m_mipCount * 2, .shaderVisible = true }))
                return false;

            const auto depthAllocation = m_heap.allocate();
            const auto hierarchyAllocation = m_heap.allocate();
            if (!depthAllocation || !depthAllocation->gpu || !hierarchyAllocation || !hierarchyAllocation->gpu)
                return false;
            m_depthSource = depthAllocation->gpu->native;
            m_hierarchySource = hierarchyAllocation->gpu->native;
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_R32_FLOAT;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = m_mipCount;
            device.CreateShaderResourceView(m_hierarchy.get(), &srv, hierarchyAllocation->cpu.native);
            m_mipSources.clear();
            m_mipTargets.clear();
            for (std::uint32_t mip = 0; mip < m_mipCount; ++mip)
            {
                const auto source = m_heap.allocate();
                const auto target = m_heap.allocate();
                if (!source || !source->gpu || !target || !target->gpu)
                    return false;
                srv.Texture2D.MostDetailedMip = mip;
                srv.Texture2D.MipLevels = 1;
                device.CreateShaderResourceView(m_hierarchy.get(), &srv, source->cpu.native);
                D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                uav.Format = DXGI_FORMAT_R32_FLOAT;
                uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uav.Texture2D.MipSlice = mip;
                device.CreateUnorderedAccessView(m_hierarchy.get(), nullptr, &uav, target->cpu.native);
                m_mipSources.push_back(source->gpu->native);
                m_mipTargets.push_back(target->gpu->native);
            }
            m_depthWidth = width;
            m_depthHeight = height;
        }
        // The depth target can be recreated without changing dimensions. Refresh its frame-local SRV.
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device.CreateShaderResourceView(depth.get(), &srv, m_heap.get()->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    bool DX12GpuOcclusionCulling::buildHierarchy(DX12CommandList& commandList, DX12Resource& depth,
        const DX12ComputePipeline& pipeline)
    {
        if (!depth.transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
            || !pipeline.bind(commandList))
            return false;
        ID3D12GraphicsCommandList& native = *commandList.getForRecording();
        ID3D12DescriptorHeap* const heap = m_heap.get();
        native.SetDescriptorHeaps(1, &heap);
        for (std::uint32_t mip = 0; mip < m_mipCount; ++mip)
        {
            const std::uint32_t width = std::max(1u, m_width >> mip);
            const std::uint32_t height = std::max(1u, m_height >> mip);
            const std::array constants{ width, height,
                mip == 0 ? m_depthWidth : std::max(1u, m_width >> (mip - 1)),
                mip == 0 ? m_depthHeight : std::max(1u, m_height >> (mip - 1)),
                mip == 0 ? 1u : 0u };
            // Every mip starts and ends in NON_PIXEL_SHADER_RESOURCE; only the destination is UAV.
            transitionMip(native, *m_hierarchy.get(), mip, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            native.SetComputeRoot32BitConstants(0, static_cast<UINT>(constants.size()), constants.data(), 0);
            native.SetComputeRootDescriptorTable(1, mip == 0 ? m_depthSource : m_mipSources[mip - 1]);
            native.SetComputeRootDescriptorTable(2, m_mipTargets[mip]);
            native.Dispatch((width + 7) / 8, (height + 7) / 8, 1);
            transitionMip(native, *m_hierarchy.get(), mip, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        return depth.transition(commandList, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }

    bool DX12GpuOcclusionCulling::record(DX12CommandList& commandList, ID3D12Device& device,
        const DX12Fence& fence, DX12Resource& depth, const DX12ComputePipeline& depthPipeline,
        const DX12ComputePipeline& cullPipeline, const RenderQueue& queue, ID3D12Resource& instances,
        const Matrix& viewProjection, const D3D12_VIEWPORT& viewport, const bool instancing)
    {
        const auto items = queue.getItems();
        const auto& description = depth.getDescription();
        if (m_recorded || m_pending || commandList.getForRecording() == nullptr || depth.get() == nullptr
            || description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D
            || description.Format != DXGI_FORMAT_R32_TYPELESS || description.SampleDesc.Count != 1
            || description.DepthOrArraySize != 1 || description.MipLevels != 1
            || description.Width == 0 || description.Width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || description.Height == 0 || description.Height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || !std::isfinite(viewport.TopLeftX) || !std::isfinite(viewport.TopLeftY)
            || !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height)
            || viewport.TopLeftX < 0.0f || viewport.TopLeftY < 0.0f
            || viewport.Width <= 0.0f || viewport.Height <= 0.0f
            || viewport.TopLeftX + viewport.Width > static_cast<float>(description.Width)
            || viewport.TopLeftY + viewport.Height > static_cast<float>(description.Height)
            || viewport.MinDepth != 0.0f || viewport.MaxDepth != 1.0f
            || items.size() > UINT32_MAX / INSTANCE_STRIDE
            || instances.GetDesc().Dimension != D3D12_RESOURCE_DIMENSION_BUFFER
            || instances.GetDesc().Width < items.size() * INSTANCE_STRIDE)
        {
            LOG_ERROR("[Occlusion] Invalid culling input or frame reused before completion.");
            return false;
        }
        m_itemBatches.assign(items.size(), UINT32_MAX);
        m_candidates.resize(items.size());
        m_batches.clear();
        for (std::size_t first = 0; first < items.size();)
        {
            const RenderItem& item = items[first];
            const std::size_t batchSize = instancing ? queue.getInstanceBatchSize(first) : 1;
            if (item.pass == RenderPassType::Opaque || item.pass == RenderPassType::AlphaTest)
            {
                m_itemBatches[first] = getBatchCount();
                m_batches.push_back({ static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(batchSize),
                    item.indexCount, item.indexStart, item.baseVertex });
                for (std::size_t offset = 0; offset < batchSize; ++offset)
                {
                    const RenderItem& instance = items[first + offset];
                    m_candidates[first + offset] = { Vector3(instance.worldBounds.Center),
                        instance.hasCullableBounds() ? 1u : 0u, Vector3(instance.worldBounds.Extents), 0 };
                }
            }
            first += batchSize;
        }
        if (m_batches.empty())
            return true;
        if (!ensureCapacity(device, fence, static_cast<std::uint32_t>(items.size()))
            || !ensureHierarchy(device, depth)
            || !m_candidateUpload.write(std::as_bytes(std::span(m_candidates)))
            || !m_batchUpload.write(std::as_bytes(std::span(m_batches)))
            || !buildHierarchy(commandList, depth, depthPipeline)
            || !m_visibleInstances.transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            || !m_commands.transition(commandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            || !cullPipeline.bind(commandList))
            return false;

        struct Constants
        {
            Matrix viewProjection;
            Vector4 viewport;
            std::uint32_t width;
            std::uint32_t height;
            std::uint32_t mipCount;
            std::uint32_t batchOffset;
        };
        static_assert(sizeof(Constants) == 24 * sizeof(std::uint32_t));
        Constants constants{ viewProjection,
            Vector4(viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height),
            m_depthWidth, m_depthHeight, m_mipCount, 0 };
        ID3D12GraphicsCommandList& native = *commandList.getForRecording();
        native.SetComputeRootDescriptorTable(1, m_hierarchySource);
        native.SetComputeRootShaderResourceView(2, m_candidateUpload.getGpuVirtualAddress());
        native.SetComputeRootShaderResourceView(3, instances.GetGPUVirtualAddress());
        native.SetComputeRootShaderResourceView(4, m_batchUpload.getGpuVirtualAddress());
        native.SetComputeRootUnorderedAccessView(5, m_visibleInstances.get()->GetGPUVirtualAddress());
        native.SetComputeRootUnorderedAccessView(6, m_commands.get()->GetGPUVirtualAddress());
        while (constants.batchOffset < getBatchCount())
        {
            const std::uint32_t count = std::min(getBatchCount() - constants.batchOffset,
                static_cast<std::uint32_t>(D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION));
            native.SetComputeRoot32BitConstants(0, 24, &constants, 0);
            native.Dispatch(count, 1, 1);
            constants.batchOffset += count;
        }
        if (!m_visibleInstances.transition(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
            || !m_commands.transition(commandList, D3D12_RESOURCE_STATE_COPY_SOURCE))
            return false;
        native.CopyBufferRegion(m_readback.get(), 0, m_commands.get(), 0,
            static_cast<std::uint64_t>(getBatchCount()) * sizeof(Command));
        if (!m_commands.transition(commandList, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT))
            return false;
        m_recorded = true;
        return true;
    }

    bool DX12GpuOcclusionCulling::execute(ID3D12GraphicsCommandList& commandList,
        const std::size_t itemIndex) const noexcept
    {
        const std::uint32_t batch = itemIndex < m_itemBatches.size() ? m_itemBatches[itemIndex] : UINT32_MAX;
        if (batch == UINT32_MAX)
            return false;
        const std::uint64_t address = static_cast<std::uint64_t>(batch) * sizeof(Command);
        commandList.SetGraphicsRootShaderResourceView(0,
            m_visibleInstances.get()->GetGPUVirtualAddress() + itemIndex * INSTANCE_STRIDE);
        commandList.ExecuteIndirect(m_signature.Get(), 1, m_commands.get(), address,
            m_commands.get(), address + offsetof(Command, count));
        return true;
    }

    bool DX12GpuOcclusionCulling::markSubmitted(const std::uint64_t fenceValue)
    {
        if (!m_recorded)
            return true;
        if (m_pending)
        {
            LOG_ERROR("[Occlusion] Culling frame was submitted twice.");
            return false;
        }
        if (!m_candidateUpload.markUsed(fenceValue) || !m_batchUpload.markUsed(fenceValue)
            || !m_readback.markPending(fenceValue))
            return false;
        m_pending = true;
        return true;
    }

    bool DX12GpuOcclusionCulling::finalize()
    {
        if (!m_readback.finalize() || !m_candidateUpload.finalize() || !m_batchUpload.finalize())
            return false;
        m_commands.finalize();
        m_visibleInstances.finalize();
        m_hierarchy.finalize();
        m_heap.finalize();
        m_signature.Reset();
        m_candidates.clear();
        m_batches.clear();
        m_itemBatches.clear();
        m_completedCommands.clear();
        m_mipSources.clear();
        m_mipTargets.clear();
        m_capacity = 0;
        m_depthWidth = m_depthHeight = m_width = m_height = m_mipCount = 0;
        m_depthSource = {};
        m_hierarchySource = {};
        m_recorded = m_pending = false;
        return true;
    }
} // namespace Engine
