#include "Pch.h"
#include "Graphics\DirectX12\Queue.h"

namespace Engine
{
    bool DX12CommandQueue::initialize(ID3D12Device& device, const DX12CommandQueueType type)
    {
        finalize();

        D3D12_COMMAND_QUEUE_DESC description{};
        description.Type = static_cast<D3D12_COMMAND_LIST_TYPE>(type);
        description.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        description.NodeMask = 0;

        const HRESULT result = device.CreateCommandQueue(&description, IID_PPV_ARGS(&m_queue));
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] コマンドキューの作成に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        m_type = type;
        return true;
    }

    void DX12CommandQueue::finalize()
    {
        m_queue.Reset();
    }

    bool DX12CommandQueue::execute(const std::span<ID3D12CommandList* const> commandLists) const
    {
        if (m_queue == nullptr || commandLists.empty())
        {
            LOG_ERROR("[DX12] 未初期化の Queue または空の Command List 配列を実行しようとしました");
            return false;
        }

        if (commandLists.size() > std::numeric_limits<UINT>::max())
        {
            LOG_ERROR("[DX12] Command List 配列の要素数が DirectX 12 の上限を超えています");
            return false;
        }

        if (std::any_of(commandLists.begin(), commandLists.end(),
            [](const ID3D12CommandList* commandList) { return commandList == nullptr; }))
        {
            LOG_ERROR("[DX12] nullptr を含む Command List 配列を実行しようとしました");
            return false;
        }

        m_queue->ExecuteCommandLists(static_cast<UINT>(commandLists.size()), commandLists.data());
        return true;
    }
} // namespace Engine