#pragma once

#include "Core\CoreDefines.h"

namespace Engine
{
    /**
     * @brief スレッドの役割
     * Main / Render / Worker / Background を識別する。
     */
    enum class ThreadRole
    {
        Main,
        Render,
        Worker,
        Background,
    };

    /**
     * @brief 現在のスレッドに紐づくローカルなコンテキスト
     * Job / Logging / Scratch Memory の情報を保持する
     */
    struct ThreadContext
    {
        uint32_t threadId = 0;     //!< 現在のスレッドID
        std::string name;          //!< スレッド名
        uint32_t jobCount = 0;     //!< このスレッドで処理したジョブ数
        uint32_t affinityIndex = 0; //!< 固定したCPUコア番号
        ThreadRole role = ThreadRole::Main; //!< 現在の役割
    };

    /**
     * @brief 現在のスレッドのコンテキストを取得する
     * @return ThreadContext& 現在のスレッドに紐づくコンテキスト
     */
    ThreadContext& getCurrentThreadContext();

    /**
     * @brief 現在のスレッドの役割を設定する
     * @param role 役割
     */
    void setCurrentThreadRole(ThreadRole role);

    /**
     * @brief 現在のスレッドがMain Threadかを取得する
     * @return bool Main Threadならtrue
     */
    bool isMainThread();

    /**
     * @brief 現在のスレッドがRender Threadかを取得する
     * @return bool Render Threadならtrue
     */
    bool isRenderThread();

    /**
     * @brief 現在のスレッドに名前を付ける（デバッガに表示される）
     * @param name スレッド名
     */
    void setCurrentThreadName(const char* name);

    /**
     * @brief 現在のスレッドIDを取得する
     * @return uint32_t スレッドID
     */
    uint32_t getCurrentThreadId();

    /**
     * @brief 同時に実行できるスレッド数を取得する
     * @return uint32_t スレッド数（取得できない場合は1）
     */
    uint32_t getHardwareConcurrency();

    /**
     * @brief 現在のスレッドを指定した論理プロセッサへ固定する
     * @param processorIndex 論理プロセッサの番号
     * @return bool 成功したらtrue
     */
    bool setCurrentThreadAffinity(uint32_t processorIndex);

    /**
     * @brief 現在のスレッドを指定時間だけ休止する
     * @param milliseconds 休止する時間（ミリ秒）
     */
    inline void sleepFor(uint32_t milliseconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    /**
     * @brief 現在のスレッドの実行権を譲る
     */
    inline void yieldThread()
    {
        std::this_thread::yield();
    }
} // namespace Engine
