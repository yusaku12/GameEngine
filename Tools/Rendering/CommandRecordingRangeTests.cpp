#include "Pch.h"
#include "Graphics\Renderer\CommandRecordingRange.h"
#include <cstdio>

namespace
{
    struct Draw
    {
        unsigned int pass;
        std::size_t instances;
    };

    bool verify(const std::vector<Draw>& draws, const std::size_t concurrency, const std::size_t grain)
    {
        std::vector<Engine::CommandRecordingRange> ranges;
        Engine::buildCommandRecordingRanges(std::span<const Draw>(draws), concurrency, grain,
            [](const Draw& draw) { return draw.pass; }, ranges);
        std::size_t next = 0;
        std::size_t actualInstances = 0;
        for (const auto& range : ranges)
        {
            if (range.first != next || range.count == 0 || range.count > draws.size() - next)
                return false;
            for (std::size_t index = range.first; index < range.first + range.count; ++index)
            {
                if (draws[index].pass != draws[range.first].pass)
                    return false;
                actualInstances += draws[index].instances;
            }
            next += range.count;
        }
        std::size_t expectedInstances = 0;
        for (const Draw& draw : draws)
            expectedInstances += draw.instances;
        if (next != draws.size() || actualInstances != expectedInstances)
            return false;
        for (unsigned int pass = 0; pass < 5; ++pass)
        {
            std::size_t drawCount = 0;
            std::size_t taskCount = 0;
            std::size_t minimum = draws.size();
            std::size_t maximum = 0;
            for (const Draw& draw : draws)
                if (draw.pass == pass)
                    ++drawCount;
            for (const auto& range : ranges)
                if (draws[range.first].pass == pass)
                {
                    ++taskCount;
                    minimum = std::min(minimum, range.count);
                    maximum = std::max(maximum, range.count);
                }
            const auto expectedTasks = drawCount == 0 ? 0
                : std::min(std::max(std::size_t{ 1 }, concurrency),
                    1 + (drawCount - 1) / std::max(std::size_t{ 1 }, grain));
            if (taskCount != expectedTasks || (taskCount != 0 && maximum - minimum > 1))
                return false;
        }
        return true;
    }
}

int runCommandRecordingRangeTests()
{
    std::size_t cases = 0;
    for (const std::size_t count : { 0u, 1u, 63u, 64u, 65u, 127u, 128u, 129u, 511u, 512u, 513u, 1025u })
        for (const std::size_t concurrency : { 0u, 1u, 2u, 3u, 8u, 64u })
            for (const std::size_t grain : { 0u, 1u, 64u, 100u })
            {
                std::vector<Draw> draws;
                for (unsigned int pass = 0; pass < 5; ++pass)
                    for (std::size_t index = 0; index < count; ++index)
                        draws.push_back({ pass, 1 + index % 73 });
                if (!verify(draws, concurrency, grain))
                {
                    std::fprintf(stderr, "Partition failed: draws/pass=%zu concurrency=%zu grain=%zu\n",
                        count, concurrency, grain);
                    return 1;
                }

                #if !defined(GE_RENDERING_INTEGRATION_TESTS)
                int main()
                {
                    return runCommandRecordingRangeTests();
                }
                #endif
                ++cases;
            }
    const std::vector<Draw> sparse = { { 0, 256 }, { 2, 1 }, { 4, 8 }, { 4, 1 }, { 4, 4 } };
    if (!verify(sparse, 8, 1))
        return 1;
    std::printf("PASS: %zu partition cases; coverage, pass boundaries, order, balance and batch preservation.\n",
        cases + 1);
    return 0;
}
