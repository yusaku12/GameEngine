#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

#include "Core\CoreDefines.h"

namespace Engine
{

/**
 * @brief 複数のスレッドから安全に出し入れできるキュー
 * 取り出し時に要素が無い場合は待機する
 */
template <class T>
class ConcurrentQueue
{
public:

    ConcurrentQueue() = default;

    GE_DISABLE_COPY_AND_MOVE(ConcurrentQueue);

    /**
     * @brief 末尾へ要素を追加する
     * @param value 追加する値
     */
    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_closed)
                return;

            m_queue.push_back(std::move(value));
        }

        m_condition.notify_one();
    }

    /**
     * @brief 先頭の要素を取り出す（要素が無ければ待機する）
     * @param outValue 取り出した値の格納先
     * @return bool 取り出せたらtrue（キューが閉じられた場合はfalse）
     */
    bool waitAndPop(T& outValue)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [this] { return !m_queue.empty() || m_closed; });

        if (m_queue.empty())
            return false;

        outValue = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }

    /**
     * @brief 先頭の要素を取り出す（待機はしない）
     * @param outValue 取り出した値の格納先
     * @return bool 取り出せたらtrue
     */
    bool tryPop(T& outValue)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty())
            return false;

        outValue = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }

    /**
     * @brief キューを閉じ、待機中のスレッドをすべて起こす
     */
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }

        m_condition.notify_all();
    }

    /**
     * @brief 全要素を破棄する
     */
    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
    }

    /**
     * @brief 要素数を取得する
     * @return size_t 要素数
     */
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    /**
     * @brief 空かどうかを取得する
     * @return bool 空ならtrue
     */
    bool isEmpty() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

private:

    mutable std::mutex      m_mutex;          //!< 排他制御
    std::condition_variable m_condition;      //!< 要素の到着を待つ条件変数
    std::deque<T>           m_queue;          //!< 要素の格納先
    bool                    m_closed = false; //!< 閉じられたか
};

} // namespace Engine
