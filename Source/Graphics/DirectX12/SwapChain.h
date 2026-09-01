#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>

#include "Core\CoreDefines.h"
#include "Graphics\DirectX12\Descriptor.h"
#include "Graphics\DirectX12\Resource.h"

namespace Engine
{
    class DX12Fence;

    /**
     * @brief SwapChain の作成設定
     */
    struct DX12SwapChainConfig
    {
        std::uint32_t bufferCount = 2; //!< Back Buffer 数
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM; //!< Back Buffer Format
        bool allowTearing = false; //!< VSync 無効時の Tearing を許可するか
    };

    /**
     * @brief DirectX 12 SwapChain と Back Buffer RTV を管理するクラス
     * @thread_safety Not thread-safe. Access must be synchronized externally.
     */
    class DX12SwapChain
    {
    public:

        DX12SwapChain() = default;
        ~DX12SwapChain() = default;

        GE_DISABLE_COPY_AND_MOVE(DX12SwapChain);

        /**
         * @brief HWND 向けの Flip Model SwapChain と Back Buffer RTV を作成する
         * @param device RTV を作成するデバイス
         * @param factory SwapChain を作成する DXGI Factory
         * @param queue Present を実行する Direct Command Queue
         * @param hwnd 出力先ウィンドウ
         * @param width Back Buffer 幅
         * @param height Back Buffer 高さ
         * @param config SwapChain の作成設定
         * @return 作成に成功した場合は true
         */
        bool initialize(
            ID3D12Device& device,
            IDXGIFactory6& factory,
            ID3D12CommandQueue& queue,
            HWND hwnd,
            std::uint32_t width,
            std::uint32_t height,
            const DX12SwapChainConfig& config = DX12SwapChainConfig{});

        /**
         * @brief GPU の完了を確認してから Back Buffer と SwapChain を解放する
         * @param completionFence Direct Queue の Fence
         * @param lastSubmittedFenceValue 解放前に完了が必要な Fence 値
         * @return GPU 完了確認と解放に成功した場合は true
         */
        bool finalize(const DX12Fence& completionFence, std::uint64_t lastSubmittedFenceValue);

        /**
         * @brief GPU の完了を確認して Back Buffer のサイズと RTV を再作成する
         * @param device RTV を作成するデバイス
         * @param completionFence Direct Queue の Fence
         * @param lastSubmittedFenceValue Resize 前に完了が必要な Fence 値
         * @param width 新しい Back Buffer 幅
         * @param height 新しい Back Buffer 高さ
         * @return Resize に成功した場合は true
         */
        bool resize(
            ID3D12Device& device,
            const DX12Fence& completionFence,
            std::uint64_t lastSubmittedFenceValue,
            std::uint32_t width,
            std::uint32_t height);

        /**
         * @brief 現在の Back Buffer を表示する
         * @param vsync VSync を有効にするか
         * @return Present に成功した場合は true
         */
        bool present(bool vsync);

        /**
         * @brief 現在の Back Buffer Resource を取得する
         * @return 現在の Back Buffer。未初期化時は nullptr
         */
        DX12Resource* getCurrentBackBuffer() noexcept;

        /**
         * @brief 現在の Back Buffer RTV を取得する
         * @return 現在の Back Buffer RTV
         */
        DX12CpuDescriptorHandle getCurrentRtv() const noexcept;

        /**
         * @brief 現在の Back Buffer Index を取得する
         * @return 現在の Back Buffer Index
         */
        std::uint32_t getCurrentBackBufferIndex() const noexcept { return m_currentBackBufferIndex; }

    private:

        bool createBackBuffers(ID3D12Device& device);
        void releaseBackBuffers();
        bool waitForGpu(const DX12Fence& completionFence, std::uint64_t lastSubmittedFenceValue) const;

        Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapChain; //!< DXGI SwapChain
        DX12DescriptorHeap m_rtvHeap; //!< Back Buffer RTV を保持する Descriptor Heap
        std::vector<std::unique_ptr<DX12Resource>> m_backBuffers; //!< State を追跡する Back Buffer
        std::vector<DX12CpuDescriptorHandle> m_rtvs; //!< Back Buffer RTV
        DX12SwapChainConfig m_config; //!< SwapChain の作成設定
        std::uint32_t m_currentBackBufferIndex = 0; //!< 現在の Back Buffer Index
    };
} // namespace Engine
