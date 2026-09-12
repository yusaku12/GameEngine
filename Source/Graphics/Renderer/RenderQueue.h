#pragma once

#include "Core\CoreDefines.h"
#include "Graphics\Renderer\RenderItem.h"

namespace Engine
{
    /**
     * @brief RenderQueueのフレーム統計。
     */
    struct RenderQueueStatistics
    {
        std::uint32_t visibleItems = 0; //!< 可視判定されたItem数
        std::uint32_t culledItems = 0;  //!< Cullingで除外されたItem数
    };

    /**
     * @brief 可視RenderItemを保持し、描画状態と透明深度で並べ替えるQueue。
     * @thread_safety Not thread-safe. Render thread only.
     */
    class RenderQueue
    {
    public:

        /**
         * @brief Queueを空にして統計をリセットする。
         */
        void clear() noexcept;

        /**
         * @brief 再allocationを抑えるための容量を予約する。
         * @param capacity 予約する容量
         */
        void reserve(std::size_t capacity);

        /**
         * @brief Itemを可視判定してQueueへ追加する。
         * @param item 追加する描画Item
         * @param frustum Cullingに使う視錐台。nullptrの場合はCullingしない
         * @return Itemが可視で追加された場合はtrue
         */
        bool submit(const RenderItem& item, const Frustum* frustum = nullptr);

        /**
         * @brief Opaqueは状態順、Transparentは背面から前面へ並べ替える。
         */
        void sort();

        /**
         * @brief Queue内のItemを取得する。
         * @return Queue内のItem
         */
        std::span<const RenderItem> getItems() const noexcept { return m_items; }

        /**
         * @brief 現在フレームの統計を取得する。
         * @return 現在フレームの統計
         */
        const RenderQueueStatistics& getStatistics() const noexcept { return m_statistics; }

        /**
         * @brief Opaque Item用の64bit Sort Keyを作成する。
         * @param item Sort Keyを作成する描画Item
         */
        static std::uint64_t createSortKey(const RenderItem& item) noexcept;

    private:

        std::vector<RenderItem> m_items;    //!< 描画Itemの配列
        RenderQueueStatistics m_statistics; //!< 現在フレームの統計
    };
} // namespace Engine