#include "Pch.h"
#include "Core\Threading\JobSystem.h"
#include "Core\Containers\String.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Core\Time\TimeManager.h"
#include <iostream>
#include <cstdlib>
#include <source_location>

thread_local int allocationBudget = -1;
thread_local std::size_t allocationCount = 0;

void* operator new(std::size_t size)
{
    if (allocationBudget == 0)
    {
        allocationBudget = -1;
        throw std::bad_alloc();
    }
    if (allocationBudget > 0) --allocationBudget;
    ++allocationCount;
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
    std::size_t checks = 0;
    void require(bool value, const std::source_location location = std::source_location::current())
    {
        ++checks;
        if (!value)
        {
            std::cerr << location.file_name() << ':' << location.line()
                << ": failed check " << checks << std::endl;
            std::_Exit(3);
        }
    }
}

int main(int argc, char** argv)
{
    using namespace Engine;
    LoggerConfig config;
    config.useFile = false;
    config.useDebugOutput = false;
    config.level = LogLevel::ERROR;
    require(Logger::instance().initialize(config));
    auto& jobs = JobSystem::instance();
    require(jobs.initialize(1));
    if (argc > 1 && std::string_view(argv[1]) == "dependency")
    {
        auto firstReady = std::make_shared<JobDependency>(1);
        auto secondReady = std::make_shared<JobDependency>(1);
        std::atomic<bool> workerStarted = false;
        std::atomic<bool> releaseWorker = false;
        JobCounter counter;
        jobs.schedule([&]
        {
            workerStarted = true;
            while (!releaseWorker.load()) std::this_thread::yield();
        }, &counter);
        while (!workerStarted.load()) std::this_thread::yield();
        std::atomic<int> completed = 0;
        jobs.scheduleWithDependency([&] { ++completed; secondReady->complete(); }, firstReady, &counter);
        jobs.scheduleWithDependency([&] { ++completed; releaseWorker = true; }, secondReady, &counter);
        jobs.schedule([&] { firstReady->complete(); }, &counter);
        jobs.wait(counter);
        require(completed == 2);
    }
    else if (argc > 1 && std::string_view(argv[1]) == "group")
    {
        std::atomic<bool> started = false;
        std::atomic<bool> done = false;
        {
            TaskGroup group;
            group.add([&]
            {
                started = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                done = true;
            });
            group.runParallel(jobs);
            while (!started.load()) std::this_thread::yield();
        }
        require(done.load());
        require(jobs.getWorkerCount() == 1);
        jobs.waitForAll();
        require(true);
    }
    else if (argc > 1 && std::string_view(argv[1]) == "allocation")
    {
        jobs.finalize();
        require(jobs.initialize(8));
        std::atomic<int> workersStarted = 0;
        std::atomic<bool> releaseWorkers = false;
        JobCounter blockers;
        for (int index = 0; index < 8; ++index)
            jobs.schedule([&]
            {
                ++workersStarted;
                while (!releaseWorkers.load()) std::this_thread::yield();
            }, &blockers);
        while (workersStarted.load() != 8) std::this_thread::yield();
        int injected = 0;
        for (int budget = 0; budget < 20; ++budget)
        {
            int executed = 0;
            const std::function<void(std::size_t)> body = [&](std::size_t) { ++executed; };
            allocationBudget = budget;
            try { jobs.parallelFor(1000, body); }
            catch (const std::bad_alloc&) { ++injected; }
            allocationBudget = -1;
            const int afterReturn = executed;
            JobCounter barrier;
            jobs.schedule([] {}, &barrier);
            jobs.wait(barrier);
            require(executed == afterReturn);
        }
        require(injected > 0);
        for (bool parallel : {false, true})
        {
            int failures = 0;
            for (int budget = 0; budget < 10; ++budget)
            {
                TaskGroup group;
                std::array<int, 256> large{};
                group.add([large] { (void)large; });
                group.add([large] { (void)large; });
                allocationBudget = budget;
                try
                {
                    if (parallel) group.runParallel(jobs); else group.run(jobs);
                }
                catch (const std::bad_alloc&) { ++failures; }
                allocationBudget = -1;
                group.wait();
            }
            require(failures > 0);
        }
        releaseWorkers = true;
        jobs.wait(blockers);
    }
    else
    {
        for (std::size_t size : {0U, 1U, 31U, 32U, 64U, 1000U})
        {
            const std::string expected(size, 'x');
            String text(expected);
            String copy(text);
            String moved(std::move(copy));
            require(moved.view() == expected && copy.isEmpty());
            moved = moved.c_str();
            require(moved.view() == expected);
            text += text;
            require(text.view() == expected + expected);
        }
        std::atomic<int> completed = 0;
        jobs.parallelFor(1000, [&](std::size_t) { ++completed; });
        require(completed == 1000);
        JobCounter parent;
        std::atomic<bool> workerStarted = false;
        std::atomic<bool> releaseWorker = false;
        jobs.schedule([&]
        {
            workerStarted = true;
            while (!releaseWorker.load()) std::this_thread::yield();
        }, &parent);
        while (!workerStarted.load()) std::this_thread::yield();
        jobs.schedule([&]
        {
            TaskGroup nested;
            nested.add([&] { ++completed; releaseWorker = true; });
            nested.run(jobs);
            nested.wait();
        }, &parent);
        jobs.wait(parent);
        require(completed == 1001);
        auto blocked = std::make_shared<JobDependency>(1);
        auto token = std::make_shared<CancellationToken>();
        JobCounter cancellation;
        jobs.scheduleWithDependency([&] { ++completed; }, blocked, &cancellation, token);
        token->cancel();
        jobs.wait(cancellation);
        require(completed == 1001);
        JobCounter failure;
        jobs.schedule([] { throw std::runtime_error("expected test failure"); }, &failure);
        jobs.wait(failure);
        require(failure.isComplete());
        bool continuationRan = false;
        jobs.scheduleWithContinuation([] { throw std::runtime_error("expected continuation failure"); },
            Continuation([&] { continuationRan = true; }), &failure);
        jobs.wait(failure);
        require(continuationRan);
        JobCounter overflow;
        require(overflow.increment((std::numeric_limits<std::uint32_t>::max)()));
        require(!overflow.increment());
        require(overflow.getValue() == (std::numeric_limits<std::uint32_t>::max)());

        String invalid("unchanged");
        invalid.reserve(128);
        invalid.append(invalid.data() + 64, 100);
        require(invalid == "unchanged");
        invalid.append(invalid.data() + 1, 100);
        require(invalid == "unchanged");
        invalid.append(nullptr, 1);
        require(invalid == "unchanged");
        invalid.append(" literal");
        require(invalid == "unchanged literal");
        const auto oldCapacity = invalid.capacity();
        allocationBudget = 0;
        invalid.reserve(1024);
        allocationBudget = -1;
        require(invalid.capacity() == oldCapacity && invalid == "unchanged literal");
        const std::wstring longWide(1024, L'x');
        allocationBudget = 0;
        const String failedWide = String::fromWide(longWide.c_str());
        allocationBudget = -1;
        require(failedWide.isEmpty());
        const wchar_t malformed[] = {static_cast<wchar_t>(0xd800), L'\0'};
        require(String::fromWide(malformed).isEmpty());
        require(String::fromWide(L"\u65e5\u672c\u8a9e").toWide() == L"\u65e5\u672c\u8a9e");
        require(String("\xff").toWide().empty());
        std::atomic<bool> taskStarted = false;
        std::atomic<bool> releaseTask = false;
        int groupRuns = 0;
        TaskGroup protectedGroup;
        protectedGroup.add([&]
        {
            taskStarted = true;
            while (!releaseTask.load()) std::this_thread::yield();
            ++groupRuns;
        });
        protectedGroup.run(jobs);
        while (!taskStarted.load()) std::this_thread::yield();
        protectedGroup.add([&] { groupRuns += 100; });
        protectedGroup.addRange(1, [&](std::size_t) { groupRuns += 100; });
        protectedGroup.run(jobs);
        protectedGroup.runParallel(jobs);
        releaseTask = true;
        protectedGroup.wait();
        require(groupRuns == 1);
        protectedGroup.run(jobs);
        protectedGroup.wait();
        require(groupRuns == 2);
        TaskGroup cancelledChunk;
        int chunkRuns = 0;
        cancelledChunk.add([&] { ++chunkRuns; cancelledChunk.cancel(); });
        cancelledChunk.add([&] { ++chunkRuns; });
        cancelledChunk.runParallel(jobs, 2);
        cancelledChunk.wait();
        require(chunkRuns == 1);
        require(cancelledChunk.isCancelled());
        std::mt19937 random(42);
        String actual;
        std::string expected;
        for (int round = 0; round < 10000; ++round)
        {
            const auto choice = random() % 4;
            const std::size_t size = random() % 128;
            if (choice == 0) { actual.resize(size, 'x'); expected.resize(size, 'x'); }
            else if (choice == 1) { actual.append('a'); expected += 'a'; }
            else if (choice == 2) { actual.clear(); expected.clear(); }
            else if (expected.size() < 256) { actual += actual; expected += std::string(expected); }
            require(actual.view() == expected);
            require(actual.c_str()[actual.size()] == '\0');
        }

        MainThreadDispatcher dispatcher;
        int dispatched = 0;
        dispatcher.post([&]
        {
            ++dispatched;
            dispatcher.post([&] { ++dispatched; });
            dispatcher.dispatchPending();
            require(dispatched == 1);
        });
        dispatcher.dispatchPending();
        require(dispatched == 1);
        dispatcher.dispatchPending();
        require(dispatched == 2);
        dispatcher.post([] { throw std::runtime_error("expected dispatcher failure"); });
        dispatcher.post([&] { ++dispatched; });
        dispatcher.dispatchPending();
        require(dispatched == 3);
        for (int round = 0; round < 3; ++round)
        {
            for (int index = 0; index < 8; ++index) dispatcher.post([&] { ++dispatched; });
            dispatcher.dispatchPending();
        }
        const auto allocationsBefore = allocationCount;
        for (int index = 0; index < 8; ++index) dispatcher.post([&] { ++dispatched; });
        dispatcher.dispatchPending();
        require(allocationCount == allocationsBefore);

        auto& time = TimeManager::instance();
        time.initialize();
        std::vector<double> samples;
        for (int frame = 0; frame < 20; ++frame)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            time.beginFrame();
            samples.push_back(time.unscaledDeltaTimePrecise());
            const std::size_t first = samples.size() > 10 ? samples.size() - 10 : 0;
            double sum = 0;
            for (std::size_t index = first; index < samples.size(); ++index) sum += samples[index];
            require(std::abs(time.smoothDeltaTime() - sum / static_cast<double>(samples.size() - first)) < 1e-7);
        }
        time.initialize();
        time.beginFrame(false, true);
        require(std::abs(time.deltaTimePrecise() - static_cast<double>(time.fixedDeltaTime())) < 1e-8);
        require(time.hasFixedUpdate());
        time.consumeFixedUpdate();
        require(!time.hasFixedUpdate());
        jobs.finalize();
        int inlineRuns = 0;
        jobs.schedule([&] { ++inlineRuns; });
        require(inlineRuns == 1);
        TaskGroup inlineGroup;
        inlineGroup.add([&] { ++inlineRuns; });
        inlineGroup.run(jobs);
        inlineGroup.wait();
        require(inlineRuns == 2);
        allocationBudget = 0;
        require(!jobs.initialize(2));
        allocationBudget = -1;
        require(!jobs.isInitialized() && jobs.getWorkerCount() == 0);
        require(jobs.initialize(2));
        jobs.finalize();
        std::atomic<bool> stopObserver = false;
        std::atomic<bool> invalidCount = false;
        std::atomic<int> lifecycleRuns = 0;
        JobCounter lifecycleCounter;
        std::thread observer([&]
        {
            while (!stopObserver.load())
                if (jobs.getWorkerCount() > 2) invalidCount = true;
        });
        std::thread submitter([&]
        {
            for (int index = 0; index < 2000; ++index)
                jobs.schedule([&] { ++lifecycleRuns; }, &lifecycleCounter);
        });
        for (int round = 0; round < 25; ++round)
        {
            require(jobs.initialize(2));
            jobs.finalize();
        }
        stopObserver = true;
        observer.join();
        submitter.join();
        jobs.wait(lifecycleCounter);
        require(!invalidCount.load());
        require(lifecycleRuns == 2000);
    }
    jobs.finalize();
    require(jobs.getWorkerCount() == 0);
    Logger::instance().finalize();
    std::cout << checks << " core checks passed" << std::endl;
}
