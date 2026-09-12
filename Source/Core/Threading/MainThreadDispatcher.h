#pragma once

#include "Core\CoreDefines.h"

namespace Engine
{
    /**
     * @brief Worker threadからMain threadへ処理を転送するDispatcher。
     * @details post()は任意のthreadから呼び出せる。dispatchPending()はMain threadからのみ呼び出す。
     * @thread_safety post() is thread-safe. dispatchPending() must be called from the main thread.
     */
    class MainThreadDispatcher final
    {
    public:

        /**
         * @brief Dispatcherの共有インスタンスを取得する。
         */
        static MainThreadDispatcher& instance() noexcept;

        MainThreadDispatcher() = default;
        ~MainThreadDispatcher() = default;

        GE_DISABLE_COPY_AND_MOVE(MainThreadDispatcher);

        /**
         * @brief Main threadで実行する処理をキューへ追加する。
         * @param task 実行する処理。空の場合は追加しない。
         */
        void post(std::function<void()> task);

        /**
         * @brief 現在キューにある処理をMain threadで実行する。
         * @details 実行中に追加された処理は次回の呼び出しで実行する。
         */
        void dispatchPending();

    private:

        std::mutex m_mutex;                         //!< Task queueを保護するMutex。
        std::vector<std::function<void()>> m_tasks; //!< Main threadで実行待ちのTask。
    };
} // namespace Engine