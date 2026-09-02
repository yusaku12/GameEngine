#pragma once
#include "Core\CoreDefines.h"
#include "Core\Threading\ThreadUtility.h"

namespace Engine
{
    /**
     * @brief ジョブのキャンセル状態を表すトークン
     * 依存するジョブ群が破棄されるときに使用する
     */
    class CancellationToken
    {
    public:

        CancellationToken() = default;

        GE_DISABLE_COPY_AND_MOVE(CancellationToken);

        /**
         * @brief キャンセルを要求する
         */
        void cancel() { m_cancelled.store(true, std::memory_order_release); }

        /**
         * @brief キャンセル済みかを取得する
         * @return bool キャンセル済みならtrue
         */
        bool isCancelled() const { return m_cancelled.load(std::memory_order_acquire); }

    private:

        std::atomic<bool> m_cancelled{ false }; //!< キャンセル状態
    };

    /**
     * @brief ジョブ依存関係の完了判定
     * 依存元が終わるまで続行を待機できる
     */
    class JobDependency
    {
    public:

        explicit JobDependency(uint32_t count = 0);

        GE_DISABLE_COPY_AND_MOVE(JobDependency);

        /**
         * @brief 依存カウントを増やす
         * @param count 追加する依存数
         */
        void addDependency(uint32_t count = 1);

        /**
         * @brief 依存の1つを完了したとみなす
         * @param count 完了数
         */
        void complete(uint32_t count = 1);

        /**
         * @brief 依存が解決済みかを取得する
         * @return bool 完了していればtrue
         */
        bool isReady() const;

        /**
         * @brief 依存が解決されるまで待機する
         */
        void wait() const;

    private:

        mutable std::mutex      m_mutex;          //!< 条件変数待機用
        mutable std::condition_variable m_condition; //!< 依存完了通知
        std::atomic<uint32_t>   m_remaining{ 0 }; //!< 残り依存数
    };

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
     * @brief 連続実行されるジョブのコールバック
     * 依存ジョブが完了したあとに呼ばれる
     */
    class Continuation
    {
    public:

        using Function = std::function<void()>;

        Continuation() = default;
        explicit Continuation(Function function)
        {
            m_state = std::make_shared<State>();
            if (m_state != nullptr)
            {
                std::lock_guard<std::mutex> lock(m_state->mutex);
                m_state->function = std::move(function);
            }
        }

        Continuation(const Continuation&) = default;
        Continuation& operator=(const Continuation&) = default;
        Continuation(Continuation&&) noexcept = default;
        Continuation& operator=(Continuation&&) noexcept = default;

        /**
         * @brief 継続処理を設定する
         * @param function 実行する関数
         */
        void set(Function function)
        {
            ensureState();
            std::lock_guard<std::mutex> lock(m_state->mutex);
            m_state->function = std::move(function);
        }

        /**
         * @brief 継続処理を実行する
         */
        void run()
        {
            if (m_state == nullptr)
                return;

            Function function;
            {
                std::lock_guard<std::mutex> lock(m_state->mutex);
                function = m_state->function;
            }

            if (function)
                function();
        }

        /**
         * @brief 有効な依存があるかを取得する
         * @return bool 定義済みならtrue
         */
        bool isValid() const
        {
            if (m_state == nullptr)
                return false;

            std::lock_guard<std::mutex> lock(m_state->mutex);
            return static_cast<bool>(m_state->function);
        }

    private:

        /**
         * @brief 継続処理の共有状態
         */
        struct State
        {
            Function function; //!< 実行する関数
            mutable std::mutex mutex; //!< 関数の排他制御
        };

        /**
         * @brief 共有状態を確保する
         */
        void ensureState()
        {
            if (m_state == nullptr)
                m_state = std::make_shared<State>();
        }

        std::shared_ptr<State> m_state; //!< 実行する継続処理の共有状態
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
        bool isInitialized() const { return m_running.load(std::memory_order_acquire); }

        /**
         * @brief ワーカースレッド数を取得する
         * @return uint32_t スレッド数
         */
        uint32_t getWorkerCount() const { return static_cast<uint32_t>(m_workers.size()); }

        /**
         * @brief ジョブを投入する
         * @param function 実行する処理
         * @param counter 完了を数えるカウンタ（不要ならnullptr）
         * @param cancellationToken キャンセル可能なジョブトークン
         */
        void schedule(JobFunction function, JobCounter* counter = nullptr, const std::shared_ptr<CancellationToken>& cancellationToken = nullptr);

        /**
         * @brief 依存関係付きジョブを投入する
         * @param function 実行する処理
         * @param dependency 依存先のジョブ依存
         * @param counter 完了を数えるカウンタ
         * @param cancellationToken キャンセル可能なジョブトークン
         */
        void scheduleWithDependency(JobFunction function, const std::shared_ptr<JobDependency>& dependency,
            JobCounter* counter = nullptr, const std::shared_ptr<CancellationToken>& cancellationToken = nullptr);

        /**
         * @brief 継続実行付きジョブを投入する
         * @param function 実行する処理
         * @param continuation 継続処理
         * @param counter 完了カウンタ
         * @param cancellationToken キャンセル可能なジョブトークン
         */
        void scheduleWithContinuation(JobFunction function, Continuation continuation,
            JobCounter* counter = nullptr, const std::shared_ptr<CancellationToken>& cancellationToken = nullptr);

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
            JobFunction function;                          //!< 実行する処理
            JobCounter* counter = nullptr;                 //!< 完了を数えるカウンタ
            std::shared_ptr<CancellationToken> cancellationToken; //!< キャンセルトークン
            std::shared_ptr<JobDependency> dependency;     //!< 実行依存
        };

        JobSystem() = default;
        ~JobSystem() { finalize(); }

        GE_DISABLE_COPY_AND_MOVE(JobSystem);

        /**
         * @brief ワーカースレッドのループ処理
         * @param index スレッド番号
         */
        void workerLoop(uint32_t index);

        /**
         * @brief ジョブを1つ取り出す
         * @param outJob 取り出したジョブ
         * @return bool ジョブがあればtrue
         */
        bool tryPopJob(Job& outJob);

        /**
         * @brief ジョブを実行する
         * @param job 実行するジョブ
         */
        void executeJob(Job& job);

        std::vector<std::thread> m_workers;             //!< ワーカースレッド
        std::deque<Job>          m_jobs;                //!< 投入されたジョブ
        mutable std::mutex       m_mutex;               //!< ジョブキューの排他制御
        std::condition_variable  m_condition;           //!< ジョブの到着を待つ条件変数
        std::atomic<uint32_t>    m_pendingCount{ 0 };   //!< 未完了のジョブ数
        std::atomic<bool>        m_running{ false };    //!< 稼働中か
    };

    /**
     * @brief 複数ジョブをまとめて実行するタスクグループ
     * 追加したジョブは同一カウンタで管理され、キャンセル可能
     */
    class TaskGroup
    {
    public:

        TaskGroup() = default;

        GE_DISABLE_COPY_AND_MOVE(TaskGroup);

        /**
         * @brief タスクを追加する
         * @param task 実行する処理
         */
        void add(const JobSystem::JobFunction& task)
        {
            if (!task)
                return;

            m_tasks.push_back(task);
            m_counter.increment();
        }

        /**
         * @brief 範囲を分割したタスクを追加する
         * @param count 要素数
         * @param body 各要素の処理
         * @param grainSize 1ジョブが処理する要素数
         */
        void addRange(size_t count, const std::function<void(size_t)>& body, size_t grainSize = 1)
        {
            if (count == 0 || !body)
                return;

            if (grainSize == 0)
                grainSize = 1;

            const size_t actualGrain = std::max<size_t>(1u, grainSize);
            for (size_t begin = 0; begin < count; begin += actualGrain)
            {
                const size_t end = std::min(begin + actualGrain, count);
                add([body, begin, end]()
                    {
                        for (size_t index = begin; index < end; ++index)
                            body(index);
                    });
            }
        }

        /**
         * @brief 追加したタスクを実行する
         * @param jobSystem ジョブシステム
         */
        void run(JobSystem& jobSystem)
        {
            if (m_tasks.empty() || m_cancelled.load(std::memory_order_acquire))
                return;

            const auto token = std::make_shared<CancellationToken>();
            m_cancellationToken = token;

            for (const auto& task : m_tasks)
                jobSystem.schedule(task, &m_counter, token);
        }

        /**
         * @brief 追加済みのジョブを並列実行用に分割して実行する
         * @param jobSystem ジョブシステム
         * @param grainSize 1ジョブが処理する要素数
         */
        void runParallel(JobSystem& jobSystem, size_t grainSize = 1)
        {
            if (m_tasks.empty() || m_cancelled.load(std::memory_order_acquire))
                return;

            const auto token = std::make_shared<CancellationToken>();
            m_cancellationToken = token;

            const size_t effectiveGrain = std::max<size_t>(1u, grainSize);
            const size_t chunkCount = (m_tasks.size() + effectiveGrain - 1u) / effectiveGrain;

            for (size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
            {
                const size_t begin = chunkIndex * effectiveGrain;
                const size_t end = std::min(begin + effectiveGrain, m_tasks.size());

                jobSystem.schedule([this, begin, end, token]()
                    {
                        if (token != nullptr && token->isCancelled())
                            return;

                        for (size_t index = begin; index < end; ++index)
                        {
                            const auto& task = m_tasks[index];
                            if (task)
                                task();
                        }
                    }, &m_counter, token);
            }
        }

        /**
         * @brief すべてのタスクの完了を待つ
         */
        void wait()
        {
            while (!m_counter.isComplete())
                std::this_thread::yield();
        }

        /**
         * @brief タスクグループをキャンセルする
         */
        void cancel()
        {
            m_cancelled.store(true, std::memory_order_release);
            if (m_cancellationToken != nullptr)
                m_cancellationToken->cancel();
        }

        /**
         * @brief キャンセル済みかを取得する
         * @return bool キャンセル済みならtrue
         */
        bool isCancelled() const { return m_cancelled.load(std::memory_order_acquire); }

    private:

        std::vector<JobSystem::JobFunction> m_tasks;               //!< 実行するジョブ群
        JobCounter m_counter;                                      //!< 完了数カウンタ
        std::shared_ptr<CancellationToken> m_cancellationToken;     //!< グループ全体のキャンセル状態
        std::atomic<bool> m_cancelled{ false };                    //!< グループがキャンセルされたか
    };
} // namespace Engine
