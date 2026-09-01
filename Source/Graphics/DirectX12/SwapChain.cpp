#include "Pch.h"
#include "Graphics\DirectX12\SwapChain.h"
#include "Graphics\DirectX12\Fence.h"

namespace Engine
{
    bool DX12SwapChain::initialize(
        ID3D12Device& device,
        IDXGIFactory6& factory,
        ID3D12CommandQueue& queue,
        const HWND hwnd,
        const std::uint32_t width,
        const std::uint32_t height,
        const DX12SwapChainConfig& config)
    {
        if (width == 0 || height == 0 || config.bufferCount < 2)
        {
            LOG_ERROR("[DX12] SwapChain のサイズと Back Buffer 数が不正です");
            return false;
        }

        if (m_swapChain != nullptr)
        {
            LOG_ERROR("[DX12] 初期化済みの SwapChain を再初期化しようとしました");
            return false;
        }

        BOOL tearingSupported = FALSE;
        if (config.allowTearing)
        {
            const HRESULT tearingResult = factory.CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &tearingSupported,
                sizeof(tearingSupported));
            if (FAILED(tearingResult) || !tearingSupported)
            {
                LOG_ERROR("[DX12] Tearing をサポートしていない環境で Tearing を有効化しようとしました");
                return false;
            }
        }

        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = width;
        description.Height = height;
        description.Format = config.format;
        description.Stereo = FALSE;
        description.SampleDesc = { 1, 0 };
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = config.bufferCount;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        description.Flags = config.allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
        const HRESULT result = factory.CreateSwapChainForHwnd(&queue, hwnd, &description, nullptr, nullptr, &swapChain);
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] SwapChain の作成に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        const HRESULT castResult = swapChain.As(&m_swapChain);
        if (FAILED(castResult))
        {
            LOG_ERROR("[DX12] SwapChain4 への変換に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(castResult));
            return false;
        }

        m_config = config;
        if (!createBackBuffers(device))
        {
            releaseBackBuffers();
            m_swapChain.Reset();
            return false;
        }

        m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
        return true;
    }

    bool DX12SwapChain::finalize(const DX12Fence& completionFence, const std::uint64_t lastSubmittedFenceValue)
    {
        if (m_swapChain == nullptr)
            return true;

        if (!waitForGpu(completionFence, lastSubmittedFenceValue))
            return false;

        releaseBackBuffers();
        m_swapChain.Reset();
        m_config = {};
        m_currentBackBufferIndex = 0;
        return true;
    }

    bool DX12SwapChain::resize(
        ID3D12Device& device,
        const DX12Fence& completionFence,
        const std::uint64_t lastSubmittedFenceValue,
        const std::uint32_t width,
        const std::uint32_t height)
    {
        if (m_swapChain == nullptr || width == 0 || height == 0)
            return false;

        if (!waitForGpu(completionFence, lastSubmittedFenceValue))
            return false;

        releaseBackBuffers();
        const UINT flags = m_config.allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        const HRESULT result = m_swapChain->ResizeBuffers(m_config.bufferCount, width, height, m_config.format, flags);
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] SwapChain の ResizeBuffers に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        if (!createBackBuffers(device))
            return false;

        m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
        return true;
    }

    bool DX12SwapChain::present(const bool vsync)
    {
        if (m_swapChain == nullptr)
            return false;

        const UINT syncInterval = vsync ? 1 : 0;
        const UINT flags = !vsync && m_config.allowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
        const HRESULT result = m_swapChain->Present(syncInterval, flags);
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] Present に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        m_currentBackBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
        return true;
    }

    DX12Resource* DX12SwapChain::getCurrentBackBuffer() noexcept
    {
        return m_currentBackBufferIndex < m_backBuffers.size() ? m_backBuffers[m_currentBackBufferIndex].get() : nullptr;
    }

    DX12CpuDescriptorHandle DX12SwapChain::getCurrentRtv() const noexcept
    {
        return m_currentBackBufferIndex < m_rtvs.size() ? m_rtvs[m_currentBackBufferIndex] : DX12CpuDescriptorHandle{};
    }

    bool DX12SwapChain::createBackBuffers(ID3D12Device& device)
    {
        DX12DescriptorHeapConfig rtvConfig{};
        rtvConfig.type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvConfig.capacity = m_config.bufferCount;
        if (!m_rtvHeap.initialize(device, rtvConfig))
            return false;

        m_backBuffers.reserve(m_config.bufferCount);
        m_rtvs.reserve(m_config.bufferCount);
        for (std::uint32_t index = 0; index < m_config.bufferCount; ++index)
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;
            const HRESULT result = m_swapChain->GetBuffer(index, IID_PPV_ARGS(&backBuffer));
            if (FAILED(result))
            {
                LOG_ERROR("[DX12] Back Buffer の取得に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
                releaseBackBuffers();
                return false;
            }

            std::unique_ptr<DX12Resource> resource = std::make_unique<DX12Resource>();
            resource->initializeExisting(*backBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT);
            const std::optional<DX12DescriptorAllocation> rtv = m_rtvHeap.allocate();
            if (!rtv.has_value())
            {
                releaseBackBuffers();
                return false;
            }

            device.CreateRenderTargetView(resource->get(), nullptr, rtv->cpu.native);
            m_backBuffers.push_back(std::move(resource));
            m_rtvs.push_back(rtv->cpu);
        }

        return true;
    }

    void DX12SwapChain::releaseBackBuffers()
    {
        for (const std::unique_ptr<DX12Resource>& backBuffer : m_backBuffers)
            backBuffer->finalize();

        m_backBuffers.clear();
        m_rtvs.clear();
        m_rtvHeap.finalize();
    }

    bool DX12SwapChain::waitForGpu(const DX12Fence& completionFence, const std::uint64_t lastSubmittedFenceValue) const
    {
        return lastSubmittedFenceValue == 0 || completionFence.waitOnCpu(lastSubmittedFenceValue);
    }
} // namespace Engine