#include "Pch.h"
#include "Graphics\DirectX12\Resource.h"
#include "Graphics\DirectX12\Command.h"
#include "Graphics\DirectX12\Fence.h"

namespace Engine
{
    bool DX12Resource::initialize(ID3D12Device& device, const DX12ResourceConfig& config)
    {
        finalize();

        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = config.heapType;
        heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;

        const HRESULT result = device.CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &config.description,
            config.initialState,
            config.clearValue,
            IID_PPV_ARGS(&m_resource));
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] Resource の作成に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        m_description = config.description;
        m_initialState = config.initialState;
        m_currentState = config.initialState;
        return true;
    }

    bool DX12Resource::initializeExisting(ID3D12Resource& resource, const D3D12_RESOURCE_STATES initialState)
    {
        finalize();

        m_resource = &resource;
        m_description = resource.GetDesc();
        m_initialState = initialState;
        m_currentState = initialState;
        return true;
    }

    void DX12Resource::finalize()
    {
        m_resource.Reset();
        m_description = {};
        m_initialState = D3D12_RESOURCE_STATE_COMMON;
        m_currentState = D3D12_RESOURCE_STATE_COMMON;
    }

    bool DX12Resource::transition(
        DX12CommandList& commandList,
        const D3D12_RESOURCE_STATES state,
        const UINT subresource)
    {
        if (m_resource == nullptr)
        {
            LOG_ERROR("[DX12] 未初期化の Resource を遷移しようとしました");
            return false;
        }

        ID3D12GraphicsCommandList* const nativeCommandList = commandList.getForRecording();
        if (nativeCommandList == nullptr)
        {
            LOG_ERROR("[DX12] Close 済みの Command List に Resource State 遷移を記録しようとしました");
            return false;
        }

        if (m_currentState == state)
            return true;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = m_resource.Get();
        barrier.Transition.Subresource = subresource;
        barrier.Transition.StateBefore = m_currentState;
        barrier.Transition.StateAfter = state;
        nativeCommandList->ResourceBarrier(1, &barrier);

        m_currentState = state;
        return true;
    }

    DX12UploadBuffer::~DX12UploadBuffer()
    {
        finalize();
    }

    bool DX12UploadBuffer::initialize(
        ID3D12Device& device,
        const DX12Fence& completionFence,
        const std::uint64_t size)
    {
        if (size == 0)
        {
            LOG_ERROR("[DX12] Upload Buffer のサイズは 1 以上である必要があります");
            return false;
        }

        if (!finalize())
            return false;

        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Alignment = 0;
        description.Width = size;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_UNKNOWN;
        description.SampleDesc = { 1, 0 };
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        description.Flags = D3D12_RESOURCE_FLAG_NONE;

        DX12ResourceConfig config{};
        config.heapType = D3D12_HEAP_TYPE_UPLOAD;
        config.description = description;
        config.initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        if (!m_resource.initialize(device, config))
            return false;

        D3D12_RANGE readRange{ 0, 0 };
        void* mappedData = nullptr;
        const HRESULT result = m_resource.get()->Map(0, &readRange, &mappedData);
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] Upload Buffer のマップに失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            m_resource.finalize();
            return false;
        }

        m_completionFence = &completionFence;
        m_mappedData = static_cast<std::byte*>(mappedData);
        m_size = size;
        m_lastUsedFenceValue = 0;
        return true;
    }

    bool DX12UploadBuffer::finalize()
    {
        if (m_resource.get() != nullptr && m_lastUsedFenceValue != 0)
        {
            if (m_completionFence == nullptr || !m_completionFence->waitOnCpu(m_lastUsedFenceValue))
            {
                LOG_ERROR("[DX12] GPU 使用中の Upload Buffer を解放しようとしました");
                return false;
            }
        }

        if (m_resource.get() != nullptr && m_mappedData != nullptr)
            m_resource.get()->Unmap(0, nullptr);

        m_resource.finalize();
        m_completionFence = nullptr;
        m_mappedData = nullptr;
        m_size = 0;
        m_lastUsedFenceValue = 0;
        return true;
    }

    bool DX12UploadBuffer::write(const std::span<const std::byte> data, const std::uint64_t offset)
    {
        if (m_mappedData == nullptr || offset > m_size || data.size() > m_size - offset)
        {
            LOG_ERROR("[DX12] Upload Buffer の書き込み範囲が不正です");
            return false;
        }

        std::memcpy(m_mappedData + offset, data.data(), data.size());
        return true;
    }

    bool DX12UploadBuffer::markUsed(const std::uint64_t fenceValue)
    {
        if (m_resource.get() == nullptr || m_completionFence == nullptr || fenceValue == 0)
        {
            LOG_ERROR("[DX12] Upload Buffer の使用 Fence 値が不正です");
            return false;
        }

        m_lastUsedFenceValue = fenceValue;
        return true;
    }

    D3D12_GPU_VIRTUAL_ADDRESS DX12UploadBuffer::getGpuVirtualAddress() const noexcept
    {
        return m_resource.get() != nullptr ? m_resource.get()->GetGPUVirtualAddress() : 0;
    }

    DX12ReadbackBuffer::~DX12ReadbackBuffer()
    {
        finalize();
    }

    bool DX12ReadbackBuffer::initialize(
        ID3D12Device& device,
        const DX12Fence& completionFence,
        const std::uint64_t size)
    {
        if (size == 0)
        {
            LOG_ERROR("[DX12] Readback Buffer のサイズは 1 以上である必要があります");
            return false;
        }

        if (!finalize())
            return false;

        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Alignment = 0;
        description.Width = size;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_UNKNOWN;
        description.SampleDesc = { 1, 0 };
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        description.Flags = D3D12_RESOURCE_FLAG_NONE;

        DX12ResourceConfig config{};
        config.heapType = D3D12_HEAP_TYPE_READBACK;
        config.description = description;
        config.initialState = D3D12_RESOURCE_STATE_COPY_DEST;
        if (!m_resource.initialize(device, config))
            return false;

        m_completionFence = &completionFence;
        m_size = size;
        m_pendingFenceValue = 0;
        return true;
    }

    bool DX12ReadbackBuffer::finalize()
    {
        if (m_resource.get() != nullptr && m_pendingFenceValue != 0)
        {
            if (m_completionFence == nullptr || !m_completionFence->waitOnCpu(m_pendingFenceValue))
            {
                LOG_ERROR("[DX12] GPU Copy 中の Readback Buffer を解放しようとしました");
                return false;
            }
        }

        m_resource.finalize();
        m_completionFence = nullptr;
        m_size = 0;
        m_pendingFenceValue = 0;
        return true;
    }

    bool DX12ReadbackBuffer::markPending(const std::uint64_t fenceValue)
    {
        if (m_resource.get() == nullptr || m_completionFence == nullptr || fenceValue == 0
            || fenceValue < m_pendingFenceValue)
        {
            LOG_ERROR("[DX12] Readback Buffer の Copy Fence 値が不正です");
            return false;
        }

        m_pendingFenceValue = fenceValue;
        return true;
    }

    bool DX12ReadbackBuffer::read(const std::span<std::byte> destination, const std::uint64_t offset) const
    {
        if (m_resource.get() == nullptr || m_completionFence == nullptr || m_pendingFenceValue == 0
            || offset > m_size || destination.size() > m_size - offset)
        {
            LOG_ERROR("[DX12] Readback Buffer の読み取り条件または範囲が不正です");
            return false;
        }

        if (!m_completionFence->waitOnCpu(m_pendingFenceValue))
            return false;

        const SIZE_T readEnd = static_cast<SIZE_T>(offset + destination.size());
        const D3D12_RANGE readRange{ static_cast<SIZE_T>(offset), readEnd };
        void* mappedData = nullptr;
        const HRESULT result = m_resource.get()->Map(0, &readRange, &mappedData);
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] Readback Buffer のマップに失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        const std::byte* const source = static_cast<const std::byte*>(mappedData) + offset;
        std::memcpy(destination.data(), source, destination.size());
        m_resource.get()->Unmap(0, nullptr);
        return true;
    }
} // namespace Engine