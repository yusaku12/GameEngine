#pragma once

#include "Core\CoreDefines.h"
#include "Graphics\DirectX12\Command.h"
#include "Editor\ImGui\ImGuiSystem.h"
#include "Graphics\DirectX12\Device.h"
#include "Graphics\DirectX12\Fence.h"
#include "Graphics\DirectX12\Pipeline.h"
#include "Graphics\DirectX12\Queue.h"
#include "Graphics\DirectX12\Resource.h"
#include "Graphics\Shader\ShaderManager.h"
#include "Graphics\DirectX12\SwapChain.h"
#include "Graphics\Camera\CameraData.h"
#include "Graphics\Model\ModelGpuCache.h"
#include "Graphics\Renderer\ModelRenderSubmission.h"
#include "Graphics\Renderer\RenderQueue.h"

namespace Engine
{
    /**
     * @brief Model描画処理のフレーム統計。
     */
    struct RendererStatistics
    {
        std::uint32_t visibleObjects = 0;          //!< 直近フレームの視錐台内に存在するModelHandle数
        std::uint32_t culledObjects = 0;           //!< 直近フレームの視錐台外に存在するModelHandle数
        std::uint32_t renderItemCount = 0;         //!< 直近フレームの描画対象となるModelRenderSubmission数
        std::uint32_t drawCallCount = 0;           //!< 直近フレームの描画コール数
        std::uint32_t batchCount = 0;              //!< 直近フレームの描画バッチ数
        std::uint32_t instanceCount = 0;           //!< 直近フレームの描画インスタンス数
        std::uint32_t psoSwitchCount = 0;          //!< 直近フレームのGraphics PSO切り替え回数
        std::uint32_t materialSwitchCount = 0;     //!< 直近フレームのMaterial切り替え回数
        std::uint32_t textureSwitchCount = 0;      //!< 直近フレームのTexture切り替え回数
        std::uint32_t vertexBufferSwitchCount = 0; //!< 直近フレームのVertex Buffer切り替え回数
        std::uint32_t indexBufferSwitchCount = 0;  //!< 直近フレームのIndex Buffer切り替え回数
    };

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

        /**
         * @brief Model描画に使用するView Projection行列を設定する。
         * @param viewProjection View Projection行列
         */
        void setViewProjection(const Matrix& viewProjection) noexcept { m_viewProjection = viewProjection; }

        /**
         * @brief Transparent sortに使用するCameraのWorld座標を設定する。
         * @param position CameraのWorld座標
         */
        void setCameraPosition(const Vector3& position) noexcept { m_cameraPosition = position; }

        /**
         * @brief Model描画に使用するWorld Space視錐台を設定する。
         * @param frustum World Space視錐台
         */
        void setFrustum(const Frustum& frustum) noexcept { m_frustum = frustum; }

        /**
         * @brief 視錐台Cullingを無効化する。
         */
        void clearFrustum() noexcept { m_frustum.reset(); }

        /**
         * @brief Camera snapshotを現在の描画設定へ反映する。
         * @param view Game threadから分離されたRenderView。
         */
        void setRenderView(const RenderView& view) noexcept;

        /**
         * @brief 直近フレームのModel描画統計を取得する。
         * @return 直近フレームのModel描画統計
         */
        const RendererStatistics& getStatistics() const noexcept { return m_statistics; }

    private:

        /**
         * @brief Graphics PSO を再生成する
         * @return 再生成に成功した場合は true
         */
        bool rebuildGraphicsPipelines();

        /**
         * @brief 現在フレームの Model 描画 Queue を構築する
         * @param usedModels 描画対象となる ModelHandle のリスト
         */
        void buildModelRenderQueue(std::vector<ModelHandle>& usedModels);

        /**
         * @brief 現在フレームの Model 描画 Queue を GPU に提出する
         * @param commandList 提出先の Command List
         * @return 提出に成功した場合は true
         */
        bool renderModelQueue(DX12CommandList& commandList);

        //! Frame In Flight 数
        static constexpr std::uint32_t FRAME_COUNT = 2;

        DX12Device m_device;                                             //!< DirectX 12 デバイス
        DX12CommandQueue m_directQueue;                                  //!< 描画コマンドキュー
        DX12Fence m_directFence;                                         //!< 描画コマンドの完了 Fence
        DX12SwapChain m_swapChain;                                       //!< 画面出力用 SwapChain
        ShaderManager m_shaderManager;                                   //!< Shader のロード・キャッシュ・Hot Reload 管理
        ShaderID m_modelVertexShaderID = 0;                              //!< Model頂点Shader ID
        ShaderID m_modelPixelShaderID = 0;                               //!< Model Pixel Shader ID
        bool m_psoRebuildPending = false;                                //!< Shader 更新に伴う Graphics PSO 再生成要求フラグ
        DX12GraphicsPipeline m_modelPipeline;                            //!< Model描画用Graphics PSO
        DX12GraphicsPipeline m_transparentModelPipeline;                 //!< 透明Model描画用Graphics PSO
        ModelGpuCache m_modelGpuCache;                                   //!< ModelHandle単位のGPU Resource Cache
        RenderQueue m_modelRenderQueue;                                  //!< 現在フレームのModel描画Queue
        std::vector<ModelRenderSubmission> m_modelSubmissions;           //!< Frame間で容量を再利用する提出Buffer
        Matrix m_viewProjection = Matrix::Identity;                      //!< CameraのView Projection行列
        Vector3 m_cameraPosition = Vector3::Zero;                        //!< Transparent sort用Camera座標
        std::optional<Frustum> m_frustum;                                //!< World Space Camera Frustum
        CameraViewport m_cameraViewport{};                               //!< 描画先に対する正規化Camera Viewport
        CameraClearMode m_cameraClearMode = CameraClearMode::SolidColor; //!< CameraのClear方式
        Color m_cameraClearColor = Color(0.08f, 0.16f, 0.24f, 1.0f);     //!< Cameraの背景Clear Color
        std::uint32_t m_cameraCullingMask = UINT32_MAX;                  //!< 描画対象LayerのBit Mask
        RendererStatistics m_statistics;                                 //!< 現在構築中フレームの描画統計
        std::array<DX12CommandList, FRAME_COUNT> m_commandLists;         //!< Frame ごとの Command List
        std::unique_ptr<ImGuiSystem> m_imguiSystem;                      //!< Editor UI のライフサイクル
        std::array<std::uint64_t, FRAME_COUNT> m_frameFenceValues{};     //!< Frame ごとの提出 Fence 値
        std::uint64_t m_lastSubmittedFenceValue = 0;                     //!< 直近に提出した Fence 値
        std::uint32_t m_renderWidth = 0;                                 //!< 現在の描画領域の幅
        std::uint32_t m_renderHeight = 0;                                //!< 現在の描画領域の高さ
        RendererStatistics m_frameStatistics;                            //!< 直近フレームの描画統計
    };
} // namespace Engine