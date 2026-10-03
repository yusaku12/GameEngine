#pragma once

#include <array>
#include <memory>

#include "Graphics\DirectX12\Command.h"
#include "Graphics\Renderer\CommandRecordingRange.h"
#include "Graphics\Renderer\RenderItem.h"

namespace Engine
{
    class DX12Fence;
    class DX12GraphicsPipeline;
    class DX12OcclusionQueries;

    /** @brief Fully resolved draw batch. Referenced resources remain alive until frame completion. */
    struct DX12PreparedModelDraw
    {
        const RenderItem* item = nullptr;
        const DX12GraphicsPipeline* pipeline = nullptr;
        std::size_t itemIndex = 0;
        std::uint32_t instanceCount = 0;
        D3D12_GPU_VIRTUAL_ADDRESS instances = 0;
        D3D12_GPU_VIRTUAL_ADDRESS material = 0;
        D3D12_GPU_VIRTUAL_ADDRESS bones = 0;
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 5> textures{};
    };

    /** @brief Immutable frame state shared by command recording tasks. */
    struct DX12ModelRecordingState
    {
        ID3D12DescriptorHeap* textureHeap = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE colorTarget{};
        D3D12_CPU_DESCRIPTOR_HANDLE depthTarget{};
        D3D12_CPU_DESCRIPTOR_HANDLE shadowTarget{};
        D3D12_VIEWPORT viewport{};
        D3D12_RECT scissor{};
        D3D12_VIEWPORT shadowViewport{};
        D3D12_RECT shadowScissor{};
        const DX12OcclusionQueries* queries = nullptr;
    };

    /** @brief Recording statistics reduced only after every task has completed. */
    struct DX12ModelRecordingStatistics
    {
        std::uint32_t drawCallCount = 0;
        std::uint32_t instanceCount = 0;
        std::uint32_t psoSwitchCount = 0;
        std::uint32_t materialSwitchCount = 0;
        std::uint32_t textureSwitchCount = 0;
        std::uint32_t vertexBufferSwitchCount = 0;
        std::uint32_t indexBufferSwitchCount = 0;
        std::uint32_t recordingThreadCount = 0;
    };

    /**
     * @brief Frame-local pool of independently owned allocators and command lists.
     * @details Workers only read prepared draws and write their own context. No cache, upload,
     * descriptor allocation, resource transition, clear or GPU submission occurs on workers.
     * @thread_safety Render thread only. record joins all tasks before returning or unwinding.
     */
    class DX12ModelCommandRecorder
    {
    public:
        DX12ModelCommandRecorder() = default;
        GE_DISABLE_COPY_AND_MOVE(DX12ModelCommandRecorder);

        /**
         * @brief Record contiguous pass ranges in parallel, preserving their submission order.
         * @param device Device used to grow the persistent context pool.
         * @param fence Fence guarding allocator reuse.
         * @param draws Immutable, sorted draw batches.
         * @param state Immutable render targets, viewport and query bindings.
         * @param parallel Enable JobSystem recording; disabled records one list per pass.
         * @return True only when every range was successfully closed.
         */
        bool record(ID3D12Device& device, const DX12Fence& fence,
            std::span<const DX12PreparedModelDraw> draws, const DX12ModelRecordingState& state, bool parallel);

        /** @brief Release the pool after the owning frame's GPU fence has completed. */
        void finalize();

        /**
         * @brief Mark every list recorded this frame with its submission fence.
         * @param fenceValue Nonzero fence signaled after GPU submission.
         * @return True when all recorded lists accepted the fence value.
         */
        bool markSubmitted(std::uint64_t fenceValue);

        /** @brief Return recorded ranges in GPU order. Valid until the next record or finalize. */
        std::span<const CommandRecordingRange> getRanges() const noexcept { return m_ranges; }

        /**
         * @brief Return a closed, non-owning command list for a valid range index.
         * @param index Index in getRanges().
         * @return Closed list, or nullptr for an invalid index or unclosed list.
         */
        ID3D12CommandList* getForExecution(std::size_t index) const noexcept;

        /** @brief Return the sum of the task-local recording statistics. */
        const DX12ModelRecordingStatistics& getStatistics() const noexcept { return m_statistics; }

    private:
        static constexpr std::size_t MAX_TASKS_PER_PASS = 8;
        static constexpr std::size_t MIN_DRAWS_PER_TASK = 64;

        struct Context
        {
            DX12CommandList commandList;
            DX12ModelRecordingStatistics statistics;
            bool succeeded = false;
            std::uint32_t threadId = 0;
        };

        bool recordRange(Context& context, const DX12Fence& fence,
            std::span<const DX12PreparedModelDraw> draws, const DX12ModelRecordingState& state);

        std::vector<std::unique_ptr<Context>> m_contexts;
        std::vector<CommandRecordingRange> m_ranges;
        DX12ModelRecordingStatistics m_statistics;
    };
} // namespace Engine
