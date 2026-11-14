#pragma once

#include <span>

#include "Core\CoreDefines.h"
#include "Core\System\ExecutionMode.h"
#include "Graphics\DirectX12\Command\Command.h"
#include "Editor\ImGui\ImGuiSystem.h"
#include "Graphics\DirectX12\Device\Device.h"
#include "Graphics\DirectX12\Command\Fence.h"
#include "Graphics\DirectX12\Pipeline\Pipeline.h"
#include "Graphics\DirectX12\Command\Queue.h"
#include "Graphics\DirectX12\Resource\Resource.h"
#include "Graphics\DirectX12\Culling\GpuOcclusionCulling.h"
#include "Graphics\DirectX12\Diagnostics\GpuProfiler.h"
#include "Graphics\DirectX12\Rendering\ModelCommandRecorder.h"
#include "Graphics\Shader\ShaderManager.h"
#include "Graphics\DirectX12\Presentation\SwapChain.h"
#include "Graphics\Camera\CameraData.h"
#include "Graphics\Debug\DebugPrimitive.h"
#include "Graphics\Model\ModelGpuCache.h"
#include "Graphics\Renderer\ModelRenderSubmission.h"
#include "Graphics\Renderer\RenderQueue.h"
#include "Graphics\Material\MaterialGpuCache.h"
#include "Graphics\Lighting\LightingFrame.h"
#include "Graphics\UI\RuntimeUIRenderer.h"
#include "Graphics\PostProcess\PostProcessPipeline.h"

namespace Engine
{
    struct DX12FrameCapture;

    /** @brief Deferred検証表示。FinalLighting以外はPostEffectを迂回する。 */
    enum class DeferredDebugView
    {
        FinalLighting, Albedo, Normal, Metallic, Roughness, MaterialAO, Emission, Depth, ShadowMap,
    };

    /**
     * @brief Model描画処理のフレーム統計。
     */
    struct RendererStatistics
    {
        std::uint32_t visibleObjects = 0;            //!< 直近フレームの視錐台内に存在するModelHandle数
        std::uint32_t culledObjects = 0;             //!< 直近フレームの視錐台外に存在するModelHandle数
        std::uint32_t renderItemCount = 0;           //!< 直近フレームの描画対象となるModelRenderSubmission数
        std::uint32_t drawCallCount = 0;             //!< CPUで提出したDirect Draw数。Indirectは別集計
        std::uint32_t indirectCallCount = 0;         //!< 色描画ExecuteIndirectのCPU提出数。GPU Draw数ではない
        std::uint32_t batchCount = 0;                //!< 直近フレームの描画バッチ数
        std::uint32_t instanceCount = 0;             //!< 直近フレームの描画インスタンス数
        std::uint32_t psoSwitchCount = 0;            //!< 直近フレームのGraphics PSO切り替え回数
        std::uint32_t materialSwitchCount = 0;       //!< 直近フレームのMaterial切り替え回数
        std::uint32_t textureSwitchCount = 0;        //!< 直近フレームのTexture切り替え回数
        std::uint32_t vertexBufferSwitchCount = 0;   //!< 直近フレームのVertex Buffer切り替え回数
        std::uint32_t indexBufferSwitchCount = 0;    //!< 直近フレームのIndex Buffer切り替え回数
        std::uint32_t occlusionBatchCount = 0;       //!< 現在FrameのHi-Z色描画候補バッチ数
        GpuOcclusionStatistics completedOcclusion;  //!< 再利用したFrame slotのGPU完了済み色描画統計
        std::uint32_t modelCommandListCount = 0;     //!< Model描画を記録した独立Command List数
        std::uint32_t modelRecordingThreadCount = 0; //!< 実際にModelコマンド記録を実行したThread数
        std::uint32_t terrainVisibleChunks = 0;     //!< Current view's frustum-visible terrain chunks before GPU occlusion.
        std::uint32_t terrainCulledChunks = 0;      //!< Current view's layer/frustum-culled terrain chunks.
        std::uint32_t terrainShadowChunks = 0;      //!< Terrain chunks submitted inside the independent shadow frustum.
        std::uint64_t terrainTriangles = 0;         //!< Current view's selected color triangles before GPU occlusion.
        std::uint64_t terrainFullTriangles = 0;     //!< Full-resolution baseline for those visible chunks.
        std::uint64_t terrainShadowTriangles = 0;   //!< Full-resolution triangles submitted for independent shadows.
        std::array<std::uint32_t, 5> terrainLodChunks{}; //!< Visible chunk counts by selected level.
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
         * @param mode Editor UI or direct Runtime presentation.
         * @param enableRuntimeUi Runtime UI機器を作成するか。UIを含まないGPU検証ではfalse。
         * @return 初期化に成功した場合は true
         */
        bool initialize(HWND hwnd, std::uint32_t width, std::uint32_t height,
            ExecutionMode mode = ExecutionMode::Editor, bool enableRuntimeUi = true);

        /** @brief Shows editor grid overlays only in editing frames. */
        void setEditorOverlayEnabled(bool enabled) noexcept { m_editorOverlayEnabled = enabled; }

        /**
         * @brief GPU 完了を待機して描画リソースを安全に解放する
         * @return 解放に成功した場合は true
         */
        bool finalize();

        /**
         * @brief Game Viewを描画し、Editor UIまたはRuntime画像をBack BufferへPresentする
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
        void setFrustum(const CameraFrustum& frustum) noexcept { m_frustum = frustum; }

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
         * @brief 同一FrameのHi-Zに基づくGPU Indirect色描画を有効化する。
         * @param enabled 有効時は隠れたInstanceを圧縮し、全Instanceが隠れたバッチのDrawを実行しない。
         * @details 深度・影・透明描画は維持する。アニメーションには同じ描画Paletteで算出したBoundsを使用する。
         * Boundsを安全に算出できない場合とNear Plane交差は可視扱い。
         * DepthをクリアしないCameraClearMode::Nothingでは無効。
         * @thread_safety Render threadのみ。フレーム開始前に設定すること。
         */
        void setOcclusionCullingEnabled(bool enabled) noexcept { m_enableOcclusionCulling = enabled; }

        /**
         * @brief Model描画のCommand List記録を既存JobSystemへ分散する。
         * @param enabled 無効時も同じ準備済みDrawをPass順に同期記録する。
         * @thread_safety Render threadのみ。フレーム開始前に設定すること。
         */
        void setParallelModelRecordingEnabled(bool enabled) noexcept { m_enableParallelModelRecording = enabled; }

        /**
         * @brief GPU完了を待ち、直近の最終表示画像とDeferred時のHDRを読み戻す。
         * @param destination 読み戻した画像。失敗時は空。
         * @details Forward時はHDRが空。HDRにはPostEffectとUI適用前のScene Colorを保持する。
         * @return 読み戻し成功時true。フレーム間の検証用で、毎フレームの使用は非推奨。
         * @thread_safety Render thread only. No concurrent rendering or resource mutation.
         */
        bool captureFrame(DX12FrameCapture& destination);

        /**
         * @brief HDR/LDRのColorパス列を設定する。空の列は必須表示変換だけを実行する。
         * @param passes Shader Snapshotとパラメータを所有コピーするパス定義。
         * @return PSO構築成功時true。失敗時は既存列を維持する。
         * @thread_safety Render thread only. フレーム間で呼ぶこと。GPU完了を待機する。
         */
        bool setPostProcessPasses(std::span<const PostProcessPassDescription> passes)
        {
            return m_postProcess.configure(passes);
        }

        /**
         * @brief OCIOのコンパイル済みShaderとLUTをPostEffectへ設定する。
         * @param description 設定値。nullptrの場合はOCIOパスを解除する。
         * @return 設定または解除に成功した場合はtrue。
         * @thread_safety Render thread only. フレーム間で呼ぶこと。GPU完了を待機する。
         */
        bool setPostProcessOcio(const PostProcessOcioDescription* description)
        {
            return m_postProcess.configureOcio(description);
        }

        /** @brief 直近のPostEffect記録統計。Render threadまたはフレーム完了後に読むこと。 */
        const PostProcessStatistics& getPostProcessStatistics() const noexcept
        {
            return m_postProcess.statistics();
        }
        /** @brief Returns the last submitted Camera PostEffect snapshot. */
        const PostProcessCameraSnapshot& getCameraPostProcessSnapshot() const noexcept { return m_cameraPostProcess; }

        /** @brief Deferred/Forward経路を選択する。フレーム間のRender threadから呼ぶこと。 */
        void setDeferredRenderingEnabled(bool enabled) noexcept { m_deferredRenderingEnabled = enabled; }

        /** @brief 検証表示を選択する。不正値はログとfalse。フレーム間で呼ぶこと。 */
        bool setDeferredDebugView(DeferredDebugView view);

    private:

        ExecutionMode m_mode = ExecutionMode::Editor;
        bool m_editorOverlayEnabled = true;
        bool m_runtimeUiEnabled = true;
        /** @brief Tracks every scene resource as soon as its GPU command lists are submitted. */
        bool markSceneSubmitted(std::uint32_t frameIndex, std::uint64_t fenceValue,
            std::span<const ModelHandle> models, std::span<const MaterialHandle> materials);

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

        /** @brief 準備済みのアニメーションメッシュを描画前に一度だけスキニングする。 */
        bool recordSkinning(DX12CommandList& commandList, std::uint32_t frameIndex);

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
         * @brief Deferred用のG-BufferとHDR Lighting Targetを作成または再作成する。
         * @param width Texture幅。
         * @param height Texture高さ。
         * @return 作成に成功した場合はtrue。
         */
        bool createDeferredRenderTargets(std::uint32_t width, std::uint32_t height);

        /** @brief G-Bufferを読み、HDR Lighting TargetへDeferred Lightingを記録する。 */
        bool recordDeferredLighting(std::uint32_t frameIndex);

        /** @brief G-Bufferデバッグ画像を表示する。通常PostEffect列を迂回する。 */
        bool recordDeferredDebug(DX12CommandList& commandList, std::uint32_t frameIndex);

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
        DX12UploadBuffer* uploadSkinningPalette(std::uint32_t frameIndex,
            const SkinningPaletteSnapshot& snapshot, const SkinningPaletteSnapshot* previousSnapshot);

        /**
         * @brief Model 描画の Instance Buffer に転送するデータ構造
         */
        struct ModelInstanceConstants
        {
            Matrix worldViewProjection; //!< Current World View Projection matrix.
            Matrix worldMatrix; //!< Current World matrix.
            Matrix previousWorldViewProjection; //!< Previous submitted World View Projection matrix.
        };
        static_assert(sizeof(ModelInstanceConstants) == sizeof(float) * 48);

        struct DeferredPassConstants
        {
            Matrix inverseViewProjection = Matrix::Identity;
            Vector4 viewport{};
            Vector4 renderSize{};
            Vector4 backgroundColor{};
            Vector4 clipAndDebug{};
        };
        static_assert(sizeof(DeferredPassConstants) == sizeof(float) * 32);

        //! Frame In Flight 数
        static constexpr std::uint32_t FRAME_COUNT = 2;

        DX12Device m_device;                                                                              //!< DirectX 12 デバイス
        DX12CommandQueue m_directQueue;                                                                   //!< 描画コマンドキュー
        DX12Fence m_directFence;                                                                          //!< 描画コマンドの完了 Fence
        DX12GpuProfiler m_gpuProfiler;                                                                    //!< Direct QueueのFrame Timestamp計測
        DX12SwapChain m_swapChain;                                                                        //!< 画面出力用 SwapChain
        DX12DescriptorHeap m_gameRtvHeap;                                                                 //!< Game Texture用 RTV Heap
        DX12Resource m_gameRenderTarget;                                                                  //!< ImGui Game Viewへ表示するScene描画Texture
        DX12CpuDescriptorHandle m_gameRenderTargetView;                                                   //!< Game TextureのRTV
        std::array<DX12Resource, 5> m_gbufferTargets;                                                     //!< Albedo、Normal、Material、Emission、Velocity
        std::array<DX12CpuDescriptorHandle, 5> m_gbufferRenderTargetViews{};                              //!< G-Buffer RTV
        std::array<std::optional<DX12DescriptorAllocation>, 5> m_gbufferShaderViews{};                    //!< G-Buffer SRV
        DX12Resource m_deferredLightingTarget;                                                           //!< HDR Lighting / Transparent target
        DX12CpuDescriptorHandle m_deferredLightingRenderTargetView{};                                   //!< HDR Lighting RTV
        std::optional<DX12DescriptorAllocation> m_deferredLightingShaderView;                            //!< HDR Lighting SRV
        std::optional<DX12DescriptorAllocation> m_deferredLightingUnorderedView;                         //!< HDR Lighting UAV
        std::optional<DX12DescriptorAllocation> m_depthShaderView;                                      //!< 深度再構築用SRV
        DX12DescriptorHeap m_dsvHeap;                                                                     //!< 深度バッファ用 DSV Heap
        DX12Resource m_depthBuffer;                                                                       //!< 画面描画用深度バッファ
        DX12CpuDescriptorHandle m_depthStencilView;                                                       //!< 深度バッファの DSV
        DX12Resource m_shadowMap;                                                                         //!< ShadowCaster pass用Depth Buffer
        DX12CpuDescriptorHandle m_shadowDepthStencilView;                                                 //!< Shadow MapのDSV
        std::array<DX12UploadBuffer, FRAME_COUNT> m_lightingBuffers;
        std::array<DX12UploadBuffer, FRAME_COUNT> m_deferredConstantsBuffers;
        DeferredPassConstants m_deferredConstants;
        LightingConstants m_lighting;
        std::optional<DX12DescriptorAllocation> m_shadowSrv;
        bool m_queryListRecorded = false;
        RuntimeUIRenderer m_runtimeUi;
        PostProcessPipeline m_postProcess;
        bool m_postProcessRecorded = false;
        PostProcessCameraSnapshot m_cameraPostProcess; //!< Value-only settings from the current RenderView.
        std::array<DX12CommandList, FRAME_COUNT> m_presentationLists;
        ShaderManager m_shaderManager;                                                                    //!< Shader のロード・キャッシュ・Hot Reload 管理
        ShaderID m_modelVertexShaderID = 0;                                                               //!< Model頂点Shader ID
        ShaderID m_skinningComputeShaderID = 0;                                                          //!< Skinning Compute Shader ID
        ShaderID m_modelPixelShaderID = 0;                                                                //!< Model Pixel Shader ID
        ShaderID m_alphaTestPixelShaderID = 0;                                                            //!< Alpha Test Pixel Shader ID
        ShaderID m_depthAlphaTestPixelShaderID = 0;                                                       //!< Depth Alpha Test Pixel Shader ID
        ShaderID m_gbufferPixelShaderID = 0;
        ShaderID m_gbufferAlphaTestPixelShaderID = 0;
        ShaderID m_deferredVertexShaderID = 0;
        ShaderID m_deferredLightingComputeShaderID = 0;
        ShaderID m_deferredDebugPixelShaderID = 0;
        ShaderID m_postProcessVertexShaderID = 0;
        ShaderID m_postProcessToneMappingShaderID = 0;
        ShaderID m_postProcessOutputShaderID = 0;
        std::array<ShaderID, POST_PROCESS_BUILTIN_ENTRIES.size()> m_postProcessBuiltinShaderIDs{};
        std::array<ShaderID, POST_PROCESS_BLOOM_ENTRIES.size()> m_postProcessBloomShaderIDs{};
        ShaderID m_lightShaftsComputeShaderID = 0;
        ShaderID m_lightShaftsFallbackShaderID = 0;
        ShaderID m_autoExposureComputeShaderID = 0;
        ShaderID m_debugVertexShaderID = 0;                                                               //!< Debug Primitive頂点Shader ID
        ShaderID m_debugShapeVertexShaderID = 0;                                                          //!< Debug Primitive形状頂点Shader ID
        ShaderID m_debugPixelShaderID = 0;                                                                //!< Debug Primitive Pixel Shader ID
        ShaderID m_hizShaderID = 0;                                                                       //!< Hi-Z生成Compute Shader ID
        ShaderID m_occlusionShaderID = 0;                                                                 //!< 可視判定Compute Shader ID
        bool m_psoRebuildPending = false;                                                                 //!< Shader 更新に伴う Graphics PSO 再生成要求フラグ
        DX12GraphicsPipeline m_modelPipeline;                                                             //!< Model描画用Graphics PSO
        DX12ComputePipeline m_skinningPipeline;                                                            //!< Skinning Compute PSO
        DX12GraphicsPipeline m_alphaTestModelPipeline;                                                    //!< Alpha Test Model描画用Graphics PSO
        DX12GraphicsPipeline m_transparentModelPipeline;                                                  //!< 透明Model描画用Graphics PSO
        DX12GraphicsPipeline m_depthOnlyModelPipeline;                                                    //!< Opaque Depth/Shadow描画用Graphics PSO
        DX12GraphicsPipeline m_depthAlphaTestModelPipeline;                                               //!< Alpha Test Depth/Shadow描画用Graphics PSO
        DX12GraphicsPipeline m_gbufferModelPipeline;
        DX12GraphicsPipeline m_gbufferAlphaTestModelPipeline;
        DX12GraphicsPipeline m_deferredTransparentModelPipeline;
        DX12ComputePipeline m_deferredLightingPipeline;
        DX12GraphicsPipeline m_deferredDebugPipeline;
        DX12ComputePipeline m_hizPipeline;                                                                //!< Hi-Z生成PSO
        DX12ComputePipeline m_occlusionPipeline;                                                          //!< 可視Instance圧縮・Indirect生成PSO
        std::array<DX12GpuOcclusionCulling, FRAME_COUNT> m_occlusionCulling;                               //!< GPU完了後に再利用するFrame別Hi-Z Resource
        bool m_enableOcclusionCulling = true;                                                             //!< 同一FrameでのHi-Z Indirect色描画
        ModelGpuCache m_modelGpuCache;                                                                    //!< ModelHandle単位のGPU Resource Cache
        MaterialGpuCache m_materialGpuCache;                                                              //!< MaterialHandle単位のGPU Resource Cache
        RenderQueue m_modelRenderQueue;                                                                   //!< 現在フレームのModel描画Queue
        std::vector<ModelRenderSubmission> m_modelSubmissions;                                            //!< Frame間で容量を再利用する提出Buffer
        struct AnimatedMeshBounds
        {
            AABB worldBounds{};
            bool valid = false;
        };
        std::vector<AnimatedMeshBounds> m_animatedMeshBounds;                                              //!< 現在SubmissionのPose Bounds。容量を再利用する
        std::array<std::vector<std::unique_ptr<DX12UploadBuffer>>, FRAME_COUNT> m_skinningPaletteBuffers; //!< Frame間で容量を再利用するSkinning Palette Buffer
        std::array<std::size_t, FRAME_COUNT> m_skinningPaletteBufferCursors{};                            //!< Frame間で容量を再利用するSkinning Palette Bufferのカーソル
        struct CachedSkinningBuffer
        {
            DX12Resource resource;
            D3D12_VERTEX_BUFFER_VIEW vertices{};
            D3D12_VERTEX_BUFFER_VIEW extra{};
            D3D12_GPU_VIRTUAL_ADDRESS source = 0;
            D3D12_GPU_VIRTUAL_ADDRESS palette = 0;
            std::uint32_t vertexCount = 0;
        };
        std::array<std::vector<std::unique_ptr<CachedSkinningBuffer>>, FRAME_COUNT> m_cachedSkinningBuffers;
        std::array<std::size_t, FRAME_COUNT> m_cachedSkinningBufferCursors{};
        std::array<DX12UploadBuffer, FRAME_COUNT> m_modelInstanceBuffers;                                 //!< Fence完了後に再利用するInstance Buffer
        std::vector<ModelInstanceConstants> m_modelInstanceConstants;                                     //!< CPU側のInstance転送データ。容量を再利用する
        std::unordered_map<std::uint64_t, Matrix> m_previousObjectWorldMatrices;
        std::unordered_map<std::uint64_t, Matrix> m_currentObjectWorldMatrices;
        std::unordered_map<std::uint32_t, std::shared_ptr<const SkinningPaletteSnapshot>> m_previousSkinningPalettes;
        std::unordered_map<std::uint32_t, std::shared_ptr<const SkinningPaletteSnapshot>> m_currentSkinningPalettes;
        bool m_enableModelInstancing = true;                                                              //!< 非透明Modelの互換Drawをまとめる
        Matrix m_viewProjection = Matrix::Identity;                                                       //!< CameraのView Projection行列
        Matrix m_inverseViewProjection = Matrix::Identity;
        Matrix m_previousViewProjection = Matrix::Identity;
        Matrix m_lastSubmittedViewProjection = Matrix::Identity;
        ObjectGUID m_previousRenderCamera;
        ObjectGUID m_currentRenderCamera;
        Vector2 m_currentRenderSize = Vector2::Zero;
        Vector2 m_previousRenderSize = Vector2::Zero;
        std::uint32_t m_temporalAAJitterIndex = 0;
        bool m_hasPreviousRenderView = false;
        bool m_temporalAAActive = false;
        float m_cameraNearClip = 0.1f;
        float m_cameraFarClip = 1000.0f;
        Vector3 m_cameraPosition = Vector3::Zero;                                                         //!< Transparent sort用Camera座標
        Vector3 m_cameraForward = Vector3::UnitZ;                                                         //!< Transparent sort用Camera視線方向
        std::optional<CameraFrustum> m_frustum;                                                           //!< World Space Camera Clip Planes
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
        std::array<DX12CommandList, FRAME_COUNT> m_deferredLightingCommandLists;
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
        bool m_deferredFrameActive = false;
        bool m_deferredPassRecorded = false;
        int m_deferredDebugView = 0;
        bool m_deferredRenderingEnabled = true;
        std::uint32_t m_requestedGameWidth = 0;                                                           //!< ImGui Game Viewが要求する描画幅
        std::uint32_t m_requestedGameHeight = 0;                                                          //!< ImGui Game Viewが要求する描画高さ
        RendererStatistics m_frameStatistics;                                                             //!< 直近フレームの描画統計
    };
} // namespace Engine