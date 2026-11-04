#pragma once

#include <atomic>
#include <mutex>
#include "Graphics\DirectX12\Command\Command.h"
#include "Graphics\DirectX12\Command\Fence.h"
#include "Graphics\DirectX12\Pipeline\Pipeline.h"
#include "Graphics\DirectX12\Resource\Resource.h"
#include "Terrain\Generation\TerrainNoise.h"

namespace Engine
{
    class Heightmap;
    enum class TerrainGenerationStage;

    /**
     * @brief Independent compute queue for seeded noise; no render resources or scene objects are accessed.
     * @thread_safety generate calls are serialized internally. initialize/shutdown require external
     * exclusion from all callers, with the terrain worker joined before shutdown.
     */
    class TerrainGpuNoise final
    {
    public:
        GE_DISABLE_COPY_AND_MOVE(TerrainGpuNoise);
        /** @brief Gets the backend shared by the dedicated terrain worker and explicit CPU-blocking callers. */
        static TerrainGpuNoise& instance();
        /** @brief Creates a compute pipeline/queue on the renderer device; loads precompiled TerrainNoise CSO. */
        bool initialize(ID3D12Device& device);
        /** @brief Drains submitted GPU work before releasing resources; logs and retains resources on failure. */
        bool shutdown();
        /** @brief True after successful initialization; never acquires the worker's execution lock. */
        bool isAvailable() const noexcept { return m_available.load(std::memory_order_acquire); }
        /**
         * @brief Dispatches noise and reads normalized heights after an independent compute fence.
         * @details Blocks only the caller (the terrain worker for component requests); no renderer frames/pumping required.
         * CPU double-precision axis decomposition preserves large/negative coordinates; shader interpolation
         * uses float and is not bit-identical to CPU noise. There is no implicit CPU fallback.
         * @param settings Existing complete CPU noise range, including rectangular 2..1025 grids.
         * @param destination Replaced only on success; source image or current terrain is not needed.
         * @param cancelled Optional cancellation flag; submitted GPU work retires before buffers are reused.
         * @param stage Optional progress sink; set to GpuReadback after submission, before the worker waits.
         * @return True on successful readback/assignment; false on logged error or cancellation.
         */
        bool generate(const TerrainNoiseSettings& settings, Heightmap& destination, const std::atomic<bool>* cancelled = nullptr,
            std::atomic<TerrainGenerationStage>* stage = nullptr);
    private:
        TerrainGpuNoise() = default;
        bool drain();
        Microsoft::WRL::ComPtr<ID3D12Device> m_device;
        DX12CommandQueue m_queue;
        DX12Fence m_fence;
        DX12CommandList m_commands;
        DX12ComputePipeline m_pipeline;
        DX12Resource m_output;
        DX12UploadBuffer m_coordinates;
        DX12ReadbackBuffer m_readback;
        std::mutex m_mutex;
        std::atomic<bool> m_available{ false };
        bool m_submitted = false;
        std::uint64_t m_pendingFence = 0;
    };
}
