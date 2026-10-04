#pragma once

#include "Core\CoreDefines.h"
#include "Graphics\DirectX12\Command.h"
#include "Editor\ImGui\ImGuiSystem.h"
#include "Graphics\DirectX12\Device.h"
#include "Graphics\DirectX12\Fence.h"
#include "Graphics\DirectX12\Pipeline.h"
#include "Graphics\DirectX12\Queue.h"
#include "Graphics\DirectX12\Resource.h"
#include "Graphics\DirectX12\OcclusionQueries.h"
#include "Graphics\DirectX12\ModelCommandRecorder.h"
#include "Graphics\Shader\ShaderManager.h"
#include "Graphics\DirectX12\SwapChain.h"
#include "Graphics\Camera\CameraData.h"
#include "Graphics\Debug\DebugPrimitive.h"
#include "Graphics\Model\ModelGpuCache.h"
#include "Graphics\Renderer\ModelRenderSubmission.h"
#include "Graphics\Renderer\RenderQueue.h"
#include "Graphics\Material\MaterialGpuCache.h"

namespace Engine
{
    /**
     * @brief Model描画処理のフレーム統計。
     */
    struct RendererStatistics
    {
        std::uint32_t visibleObjects = 0;            //!< 直近フレームの視錐台内に存在するModelHandle数
        std::uint32_t culledObjects = 0;             //!< 直近フレームの視錐台外に存在するModelHandle数
        std::uint32_t renderItemCount = 0;           //!< 直近フレームの描画対象となるModelRenderSubmission数
        std::uint32_t drawCallCount = 0;             //!< 直近フレームの描画コール数
        std::uint32_t batchCount = 0;                //!< 直近フレームの描画バッチ数
        std::uint32_t instanceCount = 0;             //!< 直近フレームの描画インスタンス数
        std::uint32_t psoSwitchCount = 0;            //!< 直近フレームのGraphics PSO切り替え回数
        std::uint32_t materialSwitchCount = 0;       //!< 直近フレームのMaterial切り替え回数
        std::uint32_t textureSwitchCount = 0;        //!< 直近フレームのTexture切り替え回数
        std::uint32_t vertexBufferSwitchCount = 0;   //!< 直近フレームのVertex Buffer切り替え回数
        std::uint32_t indexBufferSwitchCount = 0;    //!< 直近フレームのIndex Buffer切り替え回数
        std::uint32_t occlusionQueryCount = 0;       //!< 現在Frameで記録したOcclusionバッチクエリ数
        std::uint32_t completedOcclusionQueries = 0; //!< 再利用したFrame slotのGPU完了済みクエリ数
        std::uint32_t occludedBatches = 0;           //!< GPU完了済み結果で色描画を省略したバッチ数
        std::uint32_t modelCommandListCount = 0;     //!< Model描画を記録した独立Command List数
        std::uint32_t modelRecordingThreadCount = 0; //!< 実際にModelコマンド記録を実行したThread数
    };

    /**
     * @brief DirectX 12 の初期フレーム描画を管理するクラス
     * @details 2 Frame In Flight。Model command recordingをJobSystemへ分散し、Pass順に提出する。
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
         * @brief Game ViewとImGuiを描画して Back Buffer を Present する
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
         * @brief ImGui Game Viewの描画ピクセル寸法を取得する。
         * @return 幅と高さ。Game View未表示時は直近の有効寸法。
         * @thread_safety Render frame完了後にMain threadから呼び出すこと。
         */
        std::array<std::uint32_t, 2> getGameViewSize() const noexcept
        {
            return { m_requestedGameWidth, m_requestedGameHeight };
        }

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
         * @brief ShadowCaster passで使用するLight View Projectionを設定する。
         * @param viewProjection Light View Projection行列
         */
        void setShadowViewProjection(const Matrix& viewProjection) noexcept { m_shadowViewProjection = viewProjection; }

        /**
         * @brief ShadowCaster passを無効化する。
         */
        void clearShadowViewProjection() noexcept { m_shadowViewProjection.reset(); }

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

        /**
         * @brief 互換性のある非透明Model描画をGPU Instance Drawへまとめるか設定する。
         * @param enabled 有効時は同一Geometry、Material、PassのDrawを統合する。
         * @details 透明物、Skinning Snapshot、Material Parameter Overrideは個別描画を維持する。
         * @thread_safety Render threadのみ。フレーム開始前に設定すること。
         */
        void setModelInstancingEnabled(bool enabled) noexcept { m_enableModelInstancing = enabled; }

        /**
         * @brief 同一Frameの深度に基づくGPU条件付き色描画を有効化する。
         * @param enabled 有効時は完全に隠れた非透明バッチの色描画を省略する。
         * @details 深度と影は省略しない。透明、変形中、近クリップ交差、小規模バッチは判定しない。
         * @thread_safety Render threadのみ。フレーム開始前に設定すること。
         */
        void setOcclusionCullingEnabled(bool enabled) noexcept { m_enableOcclusionCulling = enabled; }

        /**
         * @brief Model描画のCommand List記録を既存JobSystemへ分散する。
         * @param enabled 無効時も同じ準備済みDrawをPass順に同期記録する。
         * @thread_safety Render threadのみ。フレーム開始前に設定すること。
         */
        void setParallelModelRecordingEnabled(bool enabled) noexcept { m_enableParallelModelRecording = enabled; }

    private:

        /**
         * @brief 現在フレームの描画を構築してGPUへ提出する。
         * @return 描画に成功した場合は true
         */
        bool renderFrame();

        /**
         * @brief 現在フレームの描画を構築する。
         * @param frameIndex Fence完了済みのFrame slot。
         * @return 構築に成功した場合は true
         */
        bool prepareModelDraws(std::uint32_t frameIndex);

        /**
         * @brief 描画領域に対応する深度バッファとDSVを作成する。
         * @param width 深度バッファ幅
         * @param height 深度バッファ高さ
         * @return 作成に成功した場合は true
         */
        bool createDepthBuffer(std::uint32_t width, std::uint32_t height);

        /**
         * @brief ImGui Game Viewへ表示するScene Render Targetを作成する。
         * @param width Texture幅。
         * @param height Texture高さ。
         * @return 作成に成功した場合はtrue。
         */
        bool createGameRenderTarget(std::uint32_t width, std::uint32_t height, const Color& clearColor);

        /**
         * @brief ShadowCaster pass用Depth Bufferを作成する。
         * @return 作成に成功した場合は true
         */
        bool createShadowMap();

        /**
         * @brief Graphics PSO を再生成する
         * @return 再生成に成功した場合は true
         */
        bool rebuildGraphicsPipelines();

        /**
         * @brief 現在フレームの Model 描画 Queue を構築する
         * @param usedModels 描画対象となる ModelHandle のリスト
         */
        void buildModelRenderQueue(std::vector<ModelHandle>& usedModels, std::vector<MaterialHandle>& usedMaterials);

        /**
         * @brief Model描画を準備・並列記録し、GPU提出順のCommand List配列を構築する。
         * @param commandList ClearとResource Barrierを記録済みのFrame開始List。ここでCloseする。
         * @param frameIndex Fence完了済みのFrame slot。
         * @return 全記録に成功した場合はtrue。GPUへの提出はrenderFrameが行う。
         */
        bool renderModelQueue(DX12CommandList& commandList, std::uint32_t frameIndex);

        /**
         * @brief 現在フレームの Model 描画 Queue を GPU に提出する際に、Skinning Palette Snapshot を Upload Buffer に転送する
         * @param frameIndex 現在フレームのインデックス
         * @param snapshot 転送する Skinning Palette Snapshot
         * @return 転送に成功した場合は Upload Buffer への非所有参照。失敗時は nullptr。
         */
        DX12UploadBuffer* uploadSkinningPalette(std::uint32_t frameIndex, const SkinningPaletteSnapshot& snapshot);

        /**
         * @brief Model 描画の Instance Buffer に転送するデータ構造
         */
        struct ModelInstanceConstants
        {
            Matrix worldViewProjection; //!< World View Projection行列
            Matrix worldMatrix;         //!< World行列
        };
        static_assert(sizeof(ModelInstanceConstants) == sizeof(float) * 32);

        //! Frame In Flight 数
        static constexpr std::uint32_t FRAME_COUNT = 2;

        DX12Device m_device;                                                                              //!< DirectX 12 デバイス
        DX12CommandQueue m_directQueue;                                                                   //!< 描画コマンドキュー
        DX12Fence m_directFence;                                                                          //!< 描画コマンドの完了 Fence
        DX12SwapChain m_swapChain;                                                                        //!< 画面出力用 SwapChain
        DX12DescriptorHeap m_gameRtvHeap;                                                                 //!< Game Texture用 RTV Heap
        DX12Resource m_gameRenderTarget;                                                                  //!< ImGui Game Viewへ表示するScene描画Texture
        DX12CpuDescriptorHandle m_gameRenderTargetView;                                                   //!< Game TextureのRTV
        DX12DescriptorHeap m_dsvHeap;                                                                     //!< 深度バッファ用 DSV Heap
        DX12Resource m_depthBuffer;                                                                       //!< 画面描画用深度バッファ
        DX12CpuDescriptorHandle m_depthStencilView;                                                       //!< 深度バッファの DSV
        DX12Resource m_shadowMap;                                                                         //!< ShadowCaster pass用Depth Buffer
        DX12CpuDescriptorHandle m_shadowDepthStencilView;                                                 //!< Shadow MapのDSV
        ShaderManager m_shaderManager;                                                                    //!< Shader のロード・キャッシュ・Hot Reload 管理
        ShaderID m_modelVertexShaderID = 0;                                                               //!< Model頂点Shader ID
        ShaderID m_modelPixelShaderID = 0;                                                                //!< Model Pixel Shader ID
        ShaderID m_alphaTestPixelShaderID = 0;                                                            //!< Alpha Test Pixel Shader ID
        ShaderID m_depthAlphaTestPixelShaderID = 0;                                                       //!< Depth Alpha Test Pixel Shader ID
        ShaderID m_debugVertexShaderID = 0;                                                               //!< Debug Primitive頂点Shader ID
        ShaderID m_debugPixelShaderID = 0;                                                                //!< Debug Primitive Pixel Shader ID
        ShaderID m_occlusionVertexShaderID = 0;                                                           //!< 可視判定用Vertex Shader ID
        bool m_psoRebuildPending = false;                                                                 //!< Shader 更新に伴う Graphics PSO 再生成要求フラグ
        DX12GraphicsPipeline m_modelPipeline;                                                             //!< Model描画用Graphics PSO
        DX12GraphicsPipeline m_alphaTestModelPipeline;                                                    //!< Alpha Test Model描画用Graphics PSO
        DX12GraphicsPipeline m_transparentModelPipeline;                                                  //!< 透明Model描画用Graphics PSO
        DX12GraphicsPipeline m_depthOnlyModelPipeline;                                                    //!< Opaque Depth/Shadow描画用Graphics PSO
        DX12GraphicsPipeline m_depthAlphaTestModelPipeline;                                               //!< Alpha Test Depth/Shadow描画用Graphics PSO
        DX12GraphicsPipeline m_occlusionPipeline;                                                         //!< 深度・色を書き込まない可視判定用PSO
        std::array<DX12OcclusionQueries, FRAME_COUNT> m_occlusionQueries;                                 //!< GPU完了後に再利用するFrame別クエリ
        bool m_enableOcclusionCulling = true;                                                             //!< 同一FrameでのGPU条件付き色描画
        ModelGpuCache m_modelGpuCache;                                                                    //!< ModelHandle単位のGPU Resource Cache
        MaterialGpuCache m_materialGpuCache;                                                              //!< MaterialHandle単位のGPU Resource Cache
        RenderQueue m_modelRenderQueue;                                                                   //!< 現在フレームのModel描画Queue
        std::vector<ModelRenderSubmission> m_modelSubmissions;                                            //!< Frame間で容量を再利用する提出Buffer
        std::array<std::vector<std::unique_ptr<DX12UploadBuffer>>, FRAME_COUNT> m_skinningPaletteBuffers; //!< Frame間で容量を再利用するSkinning Palette Buffer
        std::array<std::size_t, FRAME_COUNT> m_skinningPaletteBufferCursors{};                            //!< Frame間で容量を再利用するSkinning Palette Bufferのカーソル
        std::array<DX12UploadBuffer, FRAME_COUNT> m_modelInstanceBuffers;                                 //!< Fence完了後に再利用するInstance Buffer
        std::vector<ModelInstanceConstants> m_modelInstanceConstants;                                     //!< CPU側のInstance転送データ。容量を再利用する
        bool m_enableModelInstancing = true;                                                              //!< 非透明Modelの互換Drawをまとめる
        Matrix m_viewProjection = Matrix::Identity;                                                       //!< CameraのView Projection行列
        Vector3 m_cameraPosition = Vector3::Zero;                                                         //!< Transparent sort用Camera座標
        std::optional<Frustum> m_frustum;                                                                 //!< World Space Camera Frustum
        std::optional<Matrix> m_shadowViewProjection;                                                     //!< Light View Projection。未設定時はShadow passを省略
        CameraViewport m_cameraViewport{};                                                                //!< 描画先に対する正規化Camera Viewport
        CameraClearMode m_cameraClearMode = CameraClearMode::SolidColor;                                  //!< CameraのClear方式
        Color m_cameraClearColor = Color(0.08f, 0.16f, 0.24f, 1.0f);                                      //!< Cameraの背景Clear Color
        Color m_gameRenderTargetClearColor = Color(0.08f, 0.16f, 0.24f, 1.0f);                            //!< Game RT作成時の最適化Clear Color
        std::uint32_t m_cameraCullingMask = UINT32_MAX;                                                   //!< 描画対象LayerのBit Mask
        RendererStatistics m_statistics;                                                                  //!< 現在構築中フレームの描画統計
        std::array<DX12CommandList, FRAME_COUNT> m_commandLists;                                          //!< Frame ごとの Command List
        std::array<DX12CommandList, FRAME_COUNT> m_finishCommandLists;                                    //!< Frame ごとの Command List
        std::array<DX12CommandList, FRAME_COUNT> m_queryCommandLists;                                     //!< Frame ごとの Command List
        std::array<DX12ModelCommandRecorder, FRAME_COUNT> m_modelCommandRecorders;                        //!< Frame ごとの Model Command Recorder
        std::vector<DX12PreparedModelDraw> m_preparedModelDraws;                                          //!< 構築済みの Model Draw
        std::vector<ID3D12CommandList*> m_executionLists;                                                 //!< 実行用 Command List
        bool m_enableParallelModelRecording = true;                                                       //!< モデルの並列記録を有効にするか
        bool m_renderFailed = false;                                                                      //!< 現在フレームの描画が失敗したか
        std::unique_ptr<ImGuiSystem> m_imguiSystem;                                                       //!< Editor UI のライフサイクル
        std::array<std::uint64_t, FRAME_COUNT> m_frameFenceValues{};                                      //!< Frame ごとの提出 Fence 値
        std::uint64_t m_lastSubmittedFenceValue = 0;                                                      //!< 直近に提出した Fence 値
        std::uint32_t m_renderWidth = 0;                                                                  //!< Game Render Targetの幅
        std::uint32_t m_renderHeight = 0;                                                                 //!< Game Render Targetの高さ
        std::uint32_t m_requestedGameWidth = 0;                                                           //!< ImGui Game Viewが要求する描画幅
        std::uint32_t m_requestedGameHeight = 0;                                                          //!< ImGui Game Viewが要求する描画高さ
        RendererStatistics m_frameStatistics;                                                             //!< 直近フレームの描画統計
    };
} // namespace Engine