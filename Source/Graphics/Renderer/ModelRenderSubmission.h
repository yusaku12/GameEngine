#pragma once

#include "Assets\Model\ModelTypes.h"
#include "Core\CoreDefines.h"
#include "Core\Math\Geometry.h"

namespace Engine
{
    /**
     * @brief Game updateからRender threadへ渡すモデル描画Snapshot。
     */
    struct ModelRenderSubmission
    {
        ModelHandle model;                     //!< 描画するモデルのHandle
        Matrix worldMatrix = Matrix::Identity; //!< モデルのワールド変換行列
        AABB worldBounds{};                    //!< モデルのワールド空間でのAABB
        std::uint32_t objectID = 0;            //!< オブジェクトID。レンダリングパスでの識別に使用される
        bool castShadows = true;               //!< シャドウをキャストするかどうか
    };

    /**
     * @brief ModelRendererComponentの提出結果をRender threadへ安全に渡すQueue。
     * @thread_safety Thread-safe.
     */
    class ModelRenderSubmissionQueue
    {
    public:

        static ModelRenderSubmissionQueue& instance() noexcept;

        GE_DISABLE_COPY_AND_MOVE(ModelRenderSubmissionQueue);

        /**
         * @brief Game update側から描画Snapshotを提出する。
         * @param submission 提出する描画Snapshot
         */
        void submit(const ModelRenderSubmission& submission);

        /**
         * @brief 現在までの提出結果をRender側の再利用可能なBufferへ移動する。
         * @param destination 回収先。以前のcapacityは次フレームの提出用に再利用される
         */
        void consume(std::vector<ModelRenderSubmission>& destination);

        /**
         * @brief 未処理の提出結果を破棄する。
         */
        void clear() noexcept;

    private:

        /**
         * @brief コンストラクタ。初期容量を確保する。
         */
        ModelRenderSubmissionQueue() { m_pending.reserve(256); }
        ~ModelRenderSubmissionQueue() = default;

        std::mutex m_mutex; //!< 提出結果の保護用Mutex
        std::vector<ModelRenderSubmission> m_pending; //!< 提出された描画Snapshotの保管用Buffer
    };
} // namespace Engine