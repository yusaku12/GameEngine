#include "Pch.h"
#include "Graphics\DirectX12\GpuProfiler.h"
#include "Graphics\DirectX12\Command.h"
#include "Core\Logging\Logging.h"

namespace Engine
{
    bool DX12GpuProfiler::initialize(
        ID3D12Device& device,
        ID3D12CommandQueue& queue,
        const DX12Fence& fence)
    {
        if (!finalize())
            return false;

        UINT64 timestampFrequency = 0;
        const HRESULT frequencyResult = queue.GetTimestampFrequency(&timestampFrequency);
        if (FAILED(frequencyResult) || timestampFrequency == 0)
        {
            LOG_ERROR("[GPU Profiler] Failed to query Direct Queue timestamp frequency (HRESULT: 0x{:08X}).",
                static_cast<unsigned long>(frequencyResult));
            return false;
        }

        for (std::uint32_t index = 0; index < FRAME_COUNT; ++index)
        {
            D3D12_QUERY_HEAP_DESC description{};
            description.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            description.Count = 2;
            Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
            const HRESULT result = device.CreateQueryHeap(&description, IID_PPV_ARGS(&heap));
            if (FAILED(result))
            {
                LOG_ERROR("[GPU Profiler] Failed to create timestamp query heap {} (HRESULT: 0x{:08X}).",
                    index, static_cast<unsigned long>(result));
                return false;
            }

            if (!m_frames[index].readback.initialize(device, fence, sizeof(std::uint64_t) * 2))
                return false;
            m_frames[index].heap = std::move(heap);
        }

        m_timestampFrequency = static_cast<double>(timestampFrequency);
        return true;
    }

    bool DX12GpuProfiler::beginFrame(
        const std::uint32_t frameIndex,
        const std::uint64_t frameId,
        DX12CommandList& commandList)
    {
        if (frameIndex >= m_frames.size() || frameId == 0)
        {
            LOG_ERROR("[GPU Profiler] Invalid Frame slot or Frame identifier.");
            return false;
        }

        FrameQueries& frame = m_frames[frameIndex];
        ID3D12GraphicsCommandList* const native = commandList.getForRecording();
        if (frame.heap == nullptr || frame.pending || frame.recording || native == nullptr)
        {
            LOG_ERROR("[GPU Profiler] Frame slot {} is not ready to record a GPU measurement.", frameIndex);
            return false;
        }

        frame.frameId = frameId;
        frame.recording = true;
        native->EndQuery(frame.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        return true;
    }

    bool DX12GpuProfiler::endFrame(
        const std::uint32_t frameIndex,
        DX12CommandList& commandList)
    {
        if (frameIndex >= m_frames.size())
        {
            LOG_ERROR("[GPU Profiler] Invalid Frame slot while recording the ending timestamp.");
            return false;
        }

        FrameQueries& frame = m_frames[frameIndex];
        ID3D12GraphicsCommandList* const native = commandList.getForRecording();
        if (!frame.recording || frame.heap == nullptr || frame.readback.get() == nullptr || native == nullptr)
        {
            LOG_ERROR("[GPU Profiler] GPU measurement was not started for Frame slot {}.", frameIndex);
            return false;
        }

        native->EndQuery(frame.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        native->ResolveQueryData(frame.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
            frame.readback.get(), 0);
        frame.recording = false;
        return true;
    }

    bool DX12GpuProfiler::markSubmitted(const std::uint32_t frameIndex, const std::uint64_t fenceValue)
    {
        if (frameIndex >= m_frames.size())
        {
            LOG_ERROR("[GPU Profiler] Invalid Frame slot while marking a GPU measurement as submitted.");
            return false;
        }

        FrameQueries& frame = m_frames[frameIndex];
        if (frame.recording || frame.pending || frame.frameId == 0
            || !frame.readback.markPending(fenceValue))
        {
            LOG_ERROR("[GPU Profiler] Failed to track the submitted GPU measurement for Frame slot {}.",
                frameIndex);
            return false;
        }
        frame.pending = true;
        return true;
    }

    bool DX12GpuProfiler::collect(
        const std::uint32_t frameIndex,
        bool& available,
        std::uint64_t& frameId,
        double& gpuMilliseconds)
    {
        available = false;
        frameId = 0;
        gpuMilliseconds = 0.0;
        if (frameIndex >= m_frames.size())
        {
            LOG_ERROR("[GPU Profiler] Invalid Frame slot while collecting GPU timestamps.");
            return false;
        }

        FrameQueries& frame = m_frames[frameIndex];
        if (!frame.pending)
            return true;

        std::array<std::uint64_t, 2> timestamps{};
        if (!frame.readback.read(std::as_writable_bytes(std::span(timestamps))))
            return false;
        if (timestamps[1] < timestamps[0] || m_timestampFrequency <= 0.0)
        {
            LOG_ERROR("[GPU Profiler] GPU timestamps are invalid for Frame {}.", frame.frameId);
            return false;
        }

        frameId = frame.frameId;
        gpuMilliseconds = static_cast<double>(timestamps[1] - timestamps[0])
            * 1000.0 / m_timestampFrequency;
        available = true;
        frame.frameId = 0;
        frame.pending = false;
        return true;
    }

    bool DX12GpuProfiler::finalize()
    {
        bool succeeded = true;
        for (FrameQueries& frame : m_frames)
        {
            const bool frameFinalized = frame.readback.finalize();
            succeeded = frameFinalized && succeeded;
            if (frameFinalized)
            {
                frame.heap.Reset();
                frame.frameId = 0;
                frame.recording = false;
                frame.pending = false;
            }
        }
        if (!succeeded)
        {
            LOG_ERROR("[GPU Profiler] Failed to safely release GPU timestamp resources.");
            return false;
        }
        m_timestampFrequency = 0.0;
        return true;
    }
} // namespace Engine
