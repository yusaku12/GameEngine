#pragma once

#include "Graphics\DirectX12\Resource.h"

namespace Engine
{
    class DX12CommandList;
    class DX12Fence;

    /**
     * @brief Direct Queue上のFrame全体をTimestamp Queryで計測する。
     * @details ReadbackはFrame slot再利用時に回収し、GPU完了前にはCPUを待たせない。
     * @thread_safety Not thread-safe. Render thread only.
     */
    class DX12GpuProfiler
    {
    public:

        static constexpr std::uint32_t FRAME_COUNT = 2;

        DX12GpuProfiler() = default;
        ~DX12GpuProfiler() = default;
        GE_DISABLE_COPY_AND_MOVE(DX12GpuProfiler);

        /**
         * @brief Timestamp Query HeapとFrame別Readback Bufferを作成する。
         */
        bool initialize(ID3D12Device& device, ID3D12CommandQueue& queue, const DX12Fence& fence);

        /**
         * @brief GPU全体計測の開始Timestampを記録する。
         */
        bool beginFrame(std::uint32_t frameIndex, std::uint64_t frameId, DX12CommandList& commandList);

        /**
         * @brief 終了TimestampとReadbackへのResolveを記録する。
         */
        bool endFrame(std::uint32_t frameIndex, DX12CommandList& commandList);

        /**
         * @brief Query Resolveを提出したFence値をFrame slotへ記録する。
         */
        bool markSubmitted(std::uint32_t frameIndex, std::uint64_t fenceValue);

        /**
         * @brief GPU完了済みのFrame時間を読み取る。未回収結果がなければavailableはfalse。
         */
        bool collect(std::uint32_t frameIndex, bool& available, std::uint64_t& frameId,
            double& gpuMilliseconds);

        /** @brief GPU QueryとReadback Bufferを解放する。 */
        bool finalize();

    private:

        struct FrameQueries
        {
            Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
            DX12ReadbackBuffer readback;
            std::uint64_t frameId = 0;
            bool recording = false;
            bool pending = false;
        };

        std::array<FrameQueries, FRAME_COUNT> m_frames{};
        double m_timestampFrequency = 0.0;
    };
} // namespace Engine
