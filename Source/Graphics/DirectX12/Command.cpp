#include "Pch.h"
#include "Graphics\DirectX12\Command.h"
#include "Graphics\DirectX12\Fence.h"

namespace Engine
{
    bool DX12CommandList::initialize(ID3D12Device& device, const DX12CommandQueueType type)
    {
        finalize();

        const D3D12_COMMAND_LIST_TYPE commandListType = static_cast<D3D12_COMMAND_LIST_TYPE>(type);
        HRESULT result = device.CreateCommandAllocator(commandListType, IID_PPV_ARGS(&m_allocator));
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] Command Allocator の作成に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        result = device.CreateCommandList(
            0,
            commandListType,
            m_allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&m_commandList));
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] Command List の作成に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            finalize();
            return false;
        }

        result = m_commandList->Close();
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] 初期 Command List の Close に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            finalize();
            return false;
        }

        m_type = type;
        m_state = DX12CommandListState::CLOSED;
        return true;
    }

    void DX12CommandList::finalize()
    {
        m_commandList.Reset();
        m_allocator.Reset();
        m_type = DX12CommandQueueType::DIRECT;
        m_state = DX12CommandListState::CLOSED;
        m_lastSubmittedFenceValue = 0;
    }

    bool DX12CommandList::begin(const DX12Fence& completionFence)
    {
        if (m_commandList == nullptr || m_allocator == nullptr || m_state != DX12CommandListState::CLOSED)
        {
            LOG_ERROR("[DX12] Command List が記録開始可能な状態ではありません");
            return false;
        }

        if (m_lastSubmittedFenceValue != 0 && !completionFence.isComplete(m_lastSubmittedFenceValue))
        {
            LOG_ERROR("[DX12] GPU 使用中の Command Allocator を Reset しようとしました (Fence: {})", m_lastSubmittedFenceValue);
            return false;
        }

        HRESULT result = m_allocator->Reset();
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] Command Allocator の Reset に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        result = m_commandList->Reset(m_allocator.Get(), nullptr);
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] Command List の Reset に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        m_state = DX12CommandListState::RECORDING;
        return true;
    }

    bool DX12CommandList::close()
    {
        if (m_commandList == nullptr || m_state != DX12CommandListState::RECORDING)
        {
            LOG_ERROR("[DX12] 記録中ではない Command List を Close しようとしました");
            return false;
        }

        const HRESULT result = m_commandList->Close();
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] Command List の Close に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        m_state = DX12CommandListState::CLOSED;
        return true;
    }

    bool DX12CommandList::markSubmitted(const std::uint64_t fenceValue)
    {
        if (m_state != DX12CommandListState::CLOSED || fenceValue == 0)
        {
            LOG_ERROR("[DX12] Command List の提出 Fence 値が不正です");
            return false;
        }

        m_lastSubmittedFenceValue = fenceValue;
        return true;
    }

    ID3D12CommandList* DX12CommandList::getForExecution() const noexcept
    {
        return m_state == DX12CommandListState::CLOSED ? m_commandList.Get() : nullptr;
    }

    ID3D12GraphicsCommandList* DX12CommandList::getForRecording() const noexcept
    {
        return m_state == DX12CommandListState::RECORDING ? m_commandList.Get() : nullptr;
    }
} // namespace Engine