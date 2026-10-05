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
        m_lastSignaledValue = 0;
    }

    std::uint64_t DX12Fence::signal(ID3D12CommandQueue& queue)
    {
        if (m_fence == nullptr)
        {
            LOG_ERROR("[DX12] 未初期化の Fence を通知しようとしました");
            return 0;
        }
        if (m_nextValue == 0)
        {
            LOG_ERROR("[DX12] Fence value space exhausted");
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
        m_lastSignaledValue = value;
        return value;
    }

    bool DX12Fence::waitOnGpu(ID3D12CommandQueue& queue, const std::uint64_t value) const
    {
        if (m_fence == nullptr || value == 0)
            return false;
        if (value > m_lastSignaledValue)
        {
            LOG_ERROR("[DX12] 未通知の Fence 値を GPU 待機に指定しました (Value: {}, Last signaled: {})",
                value, m_lastSignaledValue);
            return false;
        }

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
        if (value > m_lastSignaledValue)
        {
            LOG_ERROR("[DX12] 未通知の Fence 値を CPU 待機に指定しました (Value: {}, Last signaled: {})",
                value, m_lastSignaledValue);
            return false;
        }

        const std::uint64_t completedValue = m_fence->GetCompletedValue();
        if (completedValue == std::numeric_limits<std::uint64_t>::max())
        {
            LOG_CRITICAL("[DX12] Device Removed を検出したため Fence 待機を中止しました");
            return false;
        }
        if (completedValue >= value)
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

        if (m_fence->GetCompletedValue() == std::numeric_limits<std::uint64_t>::max())
        {
            LOG_CRITICAL("[DX12] Fence 待機中に Device Removed を検出しました");
            return false;
        }

        return true;
    }

    bool DX12Fence::isComplete(const std::uint64_t value) const noexcept
    {
        if (m_fence == nullptr)
            return false;

        const std::uint64_t completedValue = m_fence->GetCompletedValue();
        return completedValue != std::numeric_limits<std::uint64_t>::max()
            && completedValue >= value;
    }

    std::uint64_t DX12Fence::getCompletedValue() const noexcept
    {
        return m_fence != nullptr ? m_fence->GetCompletedValue() : 0;
    }
} // namespace Engine