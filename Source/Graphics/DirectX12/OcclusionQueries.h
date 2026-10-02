#pragma once

#include "Graphics\DirectX12\Resource.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
    class DX12GraphicsPipeline;
    class RenderQueue;

    /**
     * @brief 同じフレームの深度を使った非透明ModelバッチのGPU可視判定。
     * @details CPUへ戻した結果は統計にのみ使用する。描画判定には過去フレームの結果を使わない。
     * @thread_safety Render threadのみ。再利用とfinalizeの前に、このFrameのFence完了を待つこと。
     */
    class DX12OcclusionQueries
    {
    public:

        GE_DISABLE_COPY_AND_MOVE(DX12OcclusionQueries);
        DX12OcclusionQueries() = default;

        /**
         * @brief 完了済みFrameの結果を回収し、次のFrameの記録状態をリセットする。
         * @param tested 完了済みFrameで判定したバッチ数。
         * @param occluded 完了済みFrameで完全に隠れていたバッチ数。
         * @return 回収成功時true。
         */
        bool collectCompletedResults(std::uint32_t& tested, std::uint32_t& occluded);

        /**
         * @brief 深度Pre-Pass完了後にクエリとResolveを記録する。
         * @param commandList 記録中のCommand List。CameraのDepth TargetとViewportを設定済みであること。
         * @param device Resource作成用Device。
         * @param fence このFrameの完了を追跡するFence。
         * @param pipeline 書き込みを行わないクエリ用Pipeline。
         * @param queue 深度、影、色の順にソートされた描画Queue。
         * @param viewProjection CameraのView Projection。
         * @param viewportSize Camera Viewportのピクセル寸法。
         * @param instancing Rendererと同じバッチ分割設定。
         * @return 記録成功時true。
         */
        bool record(DX12CommandList& commandList, ID3D12Device& device, const DX12Fence& fence,
            const DX12GraphicsPipeline& pipeline, const RenderQueue& queue,
            const Matrix& viewProjection, const Vector2& viewportSize, bool instancing);

        /**
         * @brief バッチに対応するGPU条件を設定する。未判定のバッチは無条件描画に戻す。
         * @param commandList 記録中のCommand List。
         * @param itemIndex 描画Queueのバッチ先頭Index。
         */
        void setPredicate(ID3D12GraphicsCommandList& commandList, std::size_t itemIndex) const noexcept;

        /** @brief クエリ結果のCopyを提出したFence値を記録する。 */
        bool markSubmitted(std::uint64_t fenceValue);

        /** @brief GPU完了後にResourceとCPU側状態を解放する。 */
        bool finalize();

        /** @brief 現在記録したバッチクエリ数を取得する。 */
        std::uint32_t getQueryCount() const noexcept { return static_cast<std::uint32_t>(m_queries.size()); }

    private:

        struct Query
        {
            Vector4 rectangle;
            float nearestDepth = 0.0f;
        };
        static_assert(sizeof(Query) == sizeof(float) * 5);

        bool ensureCapacity(ID3D12Device& device, const DX12Fence& fence, std::uint32_t count);

        static constexpr std::uint32_t MAX_QUERIES = 4096;
        static constexpr std::uint32_t MIN_BATCH_INDICES = 256;

        Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_heap;
        DX12Resource m_results;
        DX12ReadbackBuffer m_readback;
        std::vector<Query> m_queries;
        std::vector<std::uint32_t> m_itemQueries;
        std::vector<std::uint64_t> m_completedResults;
        std::uint32_t m_capacity = 0;
        std::uint32_t m_pendingCount = 0;
    };
} // namespace Engine
