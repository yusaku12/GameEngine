#pragma once

#include "Core\CoreDefines.h"
#include "Graphics\Camera\CameraData.h"

namespace Engine
{
    /**
     * @brief Game updateからRender threadへCamera snapshotを渡すQueue。
     * @thread_safety Thread-safe.
     */
    class CameraRenderSubmissionQueue
    {
    public:

        /**
         * @brief QueueのSingletonを取得する。
         * @return Queueの参照。
         */
        static CameraRenderSubmissionQueue& instance() noexcept;

        CameraRenderSubmissionQueue(const CameraRenderSubmissionQueue&) = delete;
        CameraRenderSubmissionQueue& operator=(const CameraRenderSubmissionQueue&) = delete;

        /**
         * @brief 最新のCamera snapshotを提出する。
         * @param view 提出するRenderView。
         */
        void submit(const RenderView& view);

        /**
         * @brief 最新のCamera snapshotを取得する。
         * @param destination 取得先。
         * @return Snapshotを取得できた場合はtrue。
         */
        bool consume(RenderView& destination);

        /**
         * @brief 描画時にジッターが加わらない場合、未消費Cameraの視錐台を取得する。
         * @pre 呼び出し側は、同じsnapshotが描画に使われるようフレーム処理を同期すること。
         * @param destination 視錐台の取得先。
         * @return 視錐台を安全に利用できる場合はtrue。
         */
        bool peekUnjitteredFrustum(CameraFrustum& destination);

        /**
         * @brief 未処理のCamera snapshotを破棄する。
         */
        void clear() noexcept;

    private:

        CameraRenderSubmissionQueue() = default;
        ~CameraRenderSubmissionQueue() = default;

        std::mutex m_mutex;                  //!< Queueの排他制御用Mutex
        std::optional<RenderView> m_pending; //!< 未処理のCamera snapshot
    };
} // namespace Engine
