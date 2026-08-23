#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

#include "Core\CoreDefines.h"

namespace Engine
{

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
