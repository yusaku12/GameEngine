#pragma once

#include "Core\CoreDefines.h"
#include "Graphics\DirectX12\Command.h"
#include "Editor\ImGui\ImGuiSystem.h"
#include "Graphics\DirectX12\Device.h"
#include "Graphics\DirectX12\Fence.h"
#include "Graphics\DirectX12\Pipeline.h"
#include "Graphics\DirectX12\Queue.h"
#include "Graphics\DirectX12\Resource.h"
#include "Graphics\DirectX12\Shader.h"
#include "Graphics\DirectX12\SwapChain.h"

namespace Engine
{
    /**
     * @brief DirectX 12 の初期フレーム描画を管理するクラス
     * @details 2 Frame In Flight で Back Buffer の Clear と Present を実行する。
     * @thread_safety Not thread-safe. Access must be synchronized externally.
     */
    class DX12Renderer
    {
    public:

        DX12Renderer() = default;
        ~DX12Renderer() = default;

        GE_DISABLE_COPY_AND_MOVE(DX12Renderer);

        /**
         * @brief DirectX 12 描画に必要な Device、Queue、Fence、SwapChain を初期化する
         * @param hwnd 描画先ウィンドウ
         * @param width Back Buffer 幅
         * @param height Back Buffer 高さ
         * @return 初期化に成功した場合は true
         */
        bool initialize(HWND hwnd, std::uint32_t width, std::uint32_t height);

        /**
         * @brief GPU 完了を待機して描画リソースを安全に解放する
         * @return 解放に成功した場合は true
         */
        bool finalize();

        /**
         * @brief 現在の Back Buffer を Clear して Present する
         * @return 描画と Present に成功した場合は true
         */
        bool render();

        /**
         * @brief GPU 完了を確認して SwapChain の Back Buffer をリサイズする
         * @param width 新しい Back Buffer 幅
         * @param height 新しい Back Buffer 高さ
         * @return リサイズに成功した場合は true
         */
        bool resize(std::uint32_t width, std::uint32_t height);

        /**
         * @brief Win32メッセージをImGuiへ転送する
         * @return ImGuiがメッセージを処理した場合はtrue
         */
        bool processImGuiMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    private:

        static constexpr std::uint32_t FRAME_COUNT = 2; //!< Frame In Flight 数

        DX12Device m_device; //!< DirectX 12 デバイス
        DX12CommandQueue m_directQueue; //!< 描画コマンドキュー
        DX12Fence m_directFence; //!< 描画コマンドの完了 Fence
        DX12SwapChain m_swapChain; //!< 画面出力用 SwapChain
        DX12Shader m_vertexShader; //!< 頂点 Shader
        DX12Shader m_pixelShader; //!< Pixel Shader
        DX12GraphicsPipeline m_graphicsPipeline; //!< キャッシュ済み Graphics PSO
        DX12UploadBuffer m_vertexBuffer; //!< 頂点データを保持する Upload Buffer
        D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView{}; //!< 頂点 Buffer View
        std::array<DX12CommandList, FRAME_COUNT> m_commandLists; //!< Frame ごとの Command List
        std::unique_ptr<ImGuiSystem> m_imguiSystem; //!< Editor UI のライフサイクル
        std::array<std::uint64_t, FRAME_COUNT> m_frameFenceValues{}; //!< Frame ごとの提出 Fence 値
        std::uint64_t m_lastSubmittedFenceValue = 0; //!< 直近に提出した Fence 値
        std::uint32_t m_renderWidth = 0; //!< 現在の描画領域の幅
        std::uint32_t m_renderHeight = 0; //!< 現在の描画領域の高さ
    };
} // namespace Engine