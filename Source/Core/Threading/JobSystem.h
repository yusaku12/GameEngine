#pragma once

#include "Core\CoreDefines.h"
#include "Core\Threading\ThreadUtility.h"

namespace Engine
{
    /**
     * @brief ジョブの完了を数えるカウンタ
     * 投入したジョブが完了するたびに減り、0になれば完了とみなす
     */
    class JobCounter
    {
    public:

        JobCounter() = default;

        GE_DISABLE_COPY_AND_MOVE(JobCounter);

        /**
         * @brief 待機対象を増やす
         * @param count 増やす数
         */
        void increment(uint32_t count = 1) { m_value.fetch_add(count, std::memory_order_relaxed); }

        /**
         * @brief 待機対象を1つ減らす
         */
        void decrement() { m_value.fetch_sub(1, std::memory_order_release); }

        /**
         * @brief 残っている待機対象の数を取得する
         * @return uint32_t 残数
         */
        uint32_t getValue() const { return m_value.load(std::memory_order_acquire); }

        /**
         * @brief すべて完了したかを取得する
         * @return bool 完了していればtrue
         */
        bool isComplete() const { return getValue() == 0; }

    private:

        std::atomic<uint32_t> m_value{ 0 }; //!< 残っている待機対象の数
    };

    /**
     * @brief ジョブシステム
     * ワーカースレッド群へ処理を分散し、カウンタで完了を待ち合わせる
     * 待機中のスレッドも他のジョブを実行するため、入れ子の待機でも停止しない
     */
    class JobSystem
    {
    public:

        //! ジョブの本体
        using JobFunction = std::function<void()>;

        /**
         * @brief インスタンスを取得する
         * @return JobSystem& インスタンス
         */
        static JobSystem& instance()
        {
            static JobSystem instance;
            return instance;
        }

        /**
         * @brief ジョブシステムを初期化する
         * @param workerCount ワーカースレッド数（0なら論理プロセッサ数-1）
         * @return bool 成功したらtrue
         */
        bool initialize(uint32_t workerCount = 0);

        /**
         * @brief ジョブシステムを終了する
         * 実行中のジョブが完了するまで待ってからスレッドを停止する
         */
        void finalize();

        /**
         * @brief 初期化済みかを取得する
         * @return bool 初期化済みならtrue
         */
        bool isInitialized() const { return m_running; }

        /**
         * @brief ワーカースレッド数を取得する
         * @return uint32_t スレッド数
         */
        uint32_t getWorkerCount() const { return static_cast<uint32_t>(m_workers.size()); }

        /**
         * @brief ジョブを投入する
         * @param function 実行する処理
         * @param counter 完了を数えるカウンタ（不要ならnullptr）
         */
        void schedule(JobFunction function, JobCounter* counter = nullptr);

        /**
         * @brief 範囲を分割して並列に実行する
         * @param count 処理する要素数
         * @param body 要素ごとの処理
         * @param grainSize 1ジョブが担当する最小要素数
         */
        void parallelFor(size_t count, const std::function<void(size_t)>& body, size_t grainSize = 1);

        /**
         * @brief カウンタが0になるまで待つ（待機中も他のジョブを実行する）
         * @param counter 待機するカウンタ
         */
        void wait(JobCounter& counter);

        /**
         * @brief 投入済みのジョブがすべて完了するまで待つ
         */
        void waitForAll();

        /**
         * @brief 現在のスレッドがワーカースレッドかを取得する
         * @return bool ワーカースレッドならtrue
         */
        static bool isWorkerThread();

    private:

        /**
         * @brief 投入されたジョブ
         */
        struct Job
        {
            JobFunction function; //!< 実行する処理
            JobCounter* counter;  //!< 完了を数えるカウンタ
        };

        JobSystem() = default;
        ~JobSystem() { finalize(); }

        GE_DISABLE_COPY_AND_MOVE(JobSystem);

        void workerLoop(uint32_t index);
        bool tryPopJob(Job& outJob);
        void executeJob(Job& job);

        std::vector<std::thread> m_workers;             //!< ワーカースレッド
        std::deque<Job>          m_jobs;                //!< 投入されたジョブ
        mutable std::mutex       m_mutex;               //!< ジョブキューの排他制御
        std::condition_variable  m_condition;           //!< ジョブの到着を待つ条件変数
        std::atomic<uint32_t>    m_pendingCount{ 0 };   //!< 未完了のジョブ数
        bool                     m_running = false;     //!< 稼働中か
    };
} // namespace Engine
