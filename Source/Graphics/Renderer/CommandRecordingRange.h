#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace Engine
{
    /*
    * @brief コマンド記録範囲を表す構造体
    */
    struct CommandRecordingRange
    {
        std::size_t first = 0; //!< コマンド記録範囲の最初のインデックス
        std::size_t count = 0; //!< コマンド記録範囲の要素数
    };

    /*
    * @brief コマンド記録範囲を構築する
    * @param draws ソート済みの描画バッチ
    * @param concurrency パスごとの最大タスク数。ゼロは 1 とみなされる
    * @param grain タスクごとの最小ターゲット描画数。ゼロは 1 とみなされる
    * @param passOf 描画のパスを返す射影
    * @param ranges 出力（GPU 実行順）。容量は再利用される
    * @thread_safety 不変の入力に対して、異なる出力ベクターに対してスレッドセーフ
    */
    template<class Draw, class PassProjection>
    void buildCommandRecordingRanges(const std::span<const Draw> draws, std::size_t concurrency,
        std::size_t grain, PassProjection passOf, std::vector<CommandRecordingRange>& ranges)
    {
        ranges.clear();
        concurrency = std::max(std::size_t{ 1 }, concurrency);
        grain = std::max(std::size_t{ 1 }, grain);
        for (std::size_t first = 0; first < draws.size();)
        {
            std::size_t end = first + 1;
            while (end < draws.size() && passOf(draws[end]) == passOf(draws[first]))
                ++end;
            const std::size_t count = end - first;
            const std::size_t tasks = std::min(concurrency, 1 + (count - 1) / grain);
            const std::size_t base = count / tasks;
            const std::size_t extra = count % tasks;
            for (std::size_t task = 0; task < tasks; ++task)
            {
                const std::size_t size = base + (task < extra ? 1 : 0);
                ranges.push_back({ first, size });
                first += size;
            }
        }
    }
} // namespace Engine
