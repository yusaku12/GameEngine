#pragma once

#include <atomic>
#include <mutex>
#include "Graphics\DirectX12\Command\Command.h"
#include "Graphics\DirectX12\Command\Fence.h"
#include "Graphics\DirectX12\Pipeline\Pipeline.h"
#include "Graphics\DirectX12\Resource\Resource.h"

namespace Engine
{
    class Heightmap;
    struct ModelResource;
    struct TerrainMeshSettings;
    struct TerrainMaterialSettings;
    enum class TerrainGenerationStage;

    /**
     * @brief Builds the complete terrain grid and layer weights on an independent compute queue.
     * @thread_safety Builds are serialized. Initialization and shutdown require external exclusion;
     * the terrain worker must be joined before shutdown.
     */
    class TerrainGpuMesh final
    {
    public:
        GE_DISABLE_COPY_AND_MOVE(TerrainGpuMesh);
        /** @brief Returns the shared terrain mesh compute backend. */
        static TerrainGpuMesh& instance();
        /** @brief Creates the pipeline and compute queue on the renderer device. */
        bool initialize(ID3D12Device& device);
        /** @brief Waits for submitted work before releasing GPU resources. */
        bool shutdown();
        /** @brief Reports whether the renderer initialized this backend, including a failed submission state. */
        bool isInitialized() const noexcept { return m_initialized.load(std::memory_order_acquire); }
        /**
         * @brief Generates and reads back a complete model for chunking and physics.
         * @details Narrow slope bands and subnormal material/height values use CPU weight correction
         * to avoid discontinuities caused by GPU arithmetic precision and denormal handling.
         * @param heights Canonical normalized height samples.
         * @param mesh Validated terrain dimensions and UV repeats.
         * @param material Validated automatic layer settings.
         * @param destination Replaced only on success.
         * @param cancelled Optional cancellation flag; submitted work is drained before returning.
         * @param stage Optional progress sink.
         * @return True on success, false on logged failure or cancellation.
         */
        bool generate(const Heightmap& heights, const TerrainMeshSettings& mesh,
            const TerrainMaterialSettings& material, ModelResource& destination,
            const std::atomic<bool>* cancelled = nullptr, std::atomic<TerrainGenerationStage>* stage = nullptr);
    private:
        TerrainGpuMesh() = default;
        bool drain();
        Microsoft::WRL::ComPtr<ID3D12Device> m_device;
        DX12CommandQueue m_queue;
        DX12Fence m_fence;
        DX12CommandList m_commands;
        DX12ComputePipeline m_pipeline;
        DX12Resource m_output;
        DX12UploadBuffer m_samples;
        DX12UploadBuffer m_axes;
        DX12UploadBuffer m_layers;
        DX12ReadbackBuffer m_readback;
        std::mutex m_mutex;
        std::atomic<bool> m_initialized{ false };
        std::atomic<bool> m_available{ false };
        bool m_submitted = false;
        std::uint64_t m_pendingFence = 0;
    };
}
