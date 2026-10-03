#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace Engine
{
    /** @brief Contiguous, batch-aligned range assigned to one recording task. */
    struct CommandRecordingRange
    {
        std::size_t first = 0;
        std::size_t count = 0;
    };

    /**
     * @brief Partition sorted draws without splitting batches or crossing pass boundaries.
     * @param draws Sorted, immutable draw batches.
     * @param concurrency Maximum tasks per pass; zero is treated as one.
     * @param grain Minimum target draws per task; zero is treated as one.
     * @param passOf Projection returning a draw's pass.
     * @param ranges Output in GPU execution order; capacity is reused.
     * @thread_safety Thread-safe for disjoint output vectors and immutable input.
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
