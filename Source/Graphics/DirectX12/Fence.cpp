#include "Pch.h"
#include "Graphics\DirectX12\Fence.h"

namespace Engine
{
    DX12Fence::~DX12Fence()
    {
        finalize();
    }

    bool DX12Fence::initialize(ID3D12Device& device)
    {
        finalize();

        const HRESULT result = device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] Fence の作成に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (m_event == nullptr)
        {
            LOG_ERROR("[DX12] Fence 待機イベントの作成に失敗しました (Win32 Error: {})", GetLastError());
            finalize();
            return false;
        }

        return true;
    }

    void DX12Fence::finalize()
    {
        if (m_event != nullptr)
        {
            CloseHandle(m_event);
            m_event = nullptr;
        }

        m_fence.Reset();
        m_nextValue = 1;
    }

    std::uint64_t DX12Fence::signal(ID3D12CommandQueue& queue)
    {
        if (m_fence == nullptr)
        {
            LOG_ERROR("[DX12] 未初期化の Fence を通知しようとしました");
            return 0;
        }

        const std::uint64_t value = m_nextValue;
        const HRESULT result = queue.Signal(m_fence.Get(), value);
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] Fence の通知に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return 0;
        }

        ++m_nextValue;
        return value;
    }

    bool DX12Fence::waitOnGpu(ID3D12CommandQueue& queue, const std::uint64_t value) const
    {
        if (m_fence == nullptr || value == 0)
            return false;

        const HRESULT result = queue.Wait(m_fence.Get(), value);
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] GPU Fence 待機の登録に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        return true;
    }

    bool DX12Fence::waitOnCpu(const std::uint64_t value) const
    {
        if (m_fence == nullptr || m_event == nullptr || value == 0)
            return false;

        if (isComplete(value))
            return true;

        const HRESULT result = m_fence->SetEventOnCompletion(value, m_event);
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] CPU Fence 待機の登録に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        const DWORD waitResult = WaitForSingleObject(m_event, INFINITE);
        if (waitResult != WAIT_OBJECT_0)
        {
            LOG_ERROR("[DX12] CPU Fence 待機に失敗しました (Win32 Error: {})", GetLastError());
            return false;
        }

        return true;
    }

    bool DX12Fence::isComplete(const std::uint64_t value) const noexcept
    {
        return m_fence != nullptr && m_fence->GetCompletedValue() >= value;
    }

    std::uint64_t DX12Fence::getCompletedValue() const noexcept
    {
        return m_fence != nullptr ? m_fence->GetCompletedValue() : 0;
    }
} // namespace Engine