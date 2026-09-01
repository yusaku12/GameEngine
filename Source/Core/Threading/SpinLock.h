#pragma once

#include <shared_mutex>
#include "Core\CoreDefines.h"

namespace Engine
{
    /**
     * @brief スピンロック
     * 保持時間が非常に短い排他制御に用いる。長時間保持する用途には使わないこと
     */
    class SpinLock
    {
    public:

        SpinLock() = default;

        GE_DISABLE_COPY_AND_MOVE(SpinLock);

        /**
         * @brief ロックを取得する
         */
        void lock()
        {
            uint32_t spinCount = 0;

            while (m_flag.test_and_set(std::memory_order_acquire))
            {
                // 短時間の待機ではCPUに待機を伝え、長引く場合はスレッドを譲る
                if (++spinCount < SPIN_LIMIT)
                    YieldProcessor();
                else
                    std::this_thread::yield();
            }
        }

        /**
         * @brief ロックの取得を試みる
         * @return bool 取得できたらtrue
         */
        bool tryLock()
        {
            return !m_flag.test_and_set(std::memory_order_acquire);
        }

        /**
         * @brief ロックを解放する
         */
        void unlock()
        {
            m_flag.clear(std::memory_order_release);
        }

    private:

        //! スレッドを譲るまでのスピン回数
        static constexpr uint32_t SPIN_LIMIT = 1024;

        std::atomic_flag m_flag = ATOMIC_FLAG_INIT; //!< ロック状態
    };

    //! 排他ロック
    using Mutex = std::mutex;

    //! 再帰的に取得できる排他ロック
    using RecursiveMutex = std::recursive_mutex;

    //! 読み書きロック
    using ReadWriteLock = std::shared_mutex;

    //! スコープを抜けるときに自動で解放する排他ロック
    template <class LockType>
    using ScopedLock = std::lock_guard<LockType>;

    //! 条件変数と併用できる排他ロック
    template <class LockType>
    using UniqueLock = std::unique_lock<LockType>;

    //! 読み取り専用の共有ロック
    using SharedLock = std::shared_lock<ReadWriteLock>;
} // namespace Engine
