#pragma once

#include "Graphics\DirectX12\Command\Command.h"
#include "Graphics\DirectX12\Pipeline\Pipeline.h"
#include "Graphics\DirectX12\Resource\Resource.h"
#include "Graphics\DirectX12\Resource\Descriptor.h"
#include "Graphics\Effects\ParticleRenderSubmission.h"
#include "Graphics\Texture\TextureTypes.h"
#include "Assets\Model\ModelTypes.h"
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Engine
{
    class DX12Device;
    class DX12Fence;
    class DX12Shader;
    class DX12GpuProfiler;
    struct ModelResource;

    /** @brief Depth resource and read-only views required by soft particles. */
    struct ParticleSceneDepth
    {
        DX12Resource* resource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv{};
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};
        Matrix inverseViewProjection = Matrix::Identity;
    };

    /**
     * @brief Uploads and renders sorted world-space particle billboards and trails.
     * @thread_safety Render thread only. The submission queue is thread-safe.
     */
    class ParticleRenderer
    {
    public:
        ParticleRenderer() = default;
        ~ParticleRenderer() = default;
        GE_DISABLE_COPY_AND_MOVE(ParticleRenderer);

        /** @brief Connects frame buffers and command lists to the renderer device and fence. */
        bool initialize(DX12Device& device, const DX12Fence& fence);
        /** @brief Waits for any particle draw to finish before releasing its resources. */
        bool finalize();
        /** @brief Rebuilds the forward and deferred transparent pipelines transactionally. */
        bool rebuildPipeline(const DX12Shader& vertexShader, const DX12Shader& meshVertexShader,
            const DX12Shader& pixelShader, const DX12Shader& gpuVertexShader,
            const DX12Shader& gpuPixelShader, const DX12Shader& simulationShader,
            const DX12Shader& compactionShader);
        /** @brief Records the current particle snapshot into the selected scene color target. */
        bool record(std::uint32_t frameIndex, const Matrix& viewProjection, const Matrix& inverseView,
            const Vector3& cameraPosition, const Vector3& cameraForward,
            D3D12_CPU_DESCRIPTOR_HANDLE colorTarget, D3D12_CPU_DESCRIPTOR_HANDLE depthTarget,
            const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor, DXGI_FORMAT targetFormat,
            bool enabled, DX12GpuProfiler* profiler = nullptr, std::uint64_t profileFrameId = 0,
            const ParticleSceneDepth* sceneDepth = nullptr, DX12Resource* sceneColor = nullptr);
        /** @brief Returns recorded direct batches and indirect submissions, including empty indirect draws. */
        std::size_t drawCount() const noexcept { return m_batches.size() + m_gpuEmitterDrawCount; }
        /** @brief Returns the number of CPU-simulated particle instances submitted in the last recorded pass. */
        std::size_t renderedCpuParticleCount() const noexcept { return m_renderedParticles; }
        /** @brief Returns the number of trail segments in the last recorded pass. */
        std::size_t renderedTrailCount() const noexcept { return m_renderedTrails; }
        /** @brief Returns the number of independent ribbons in the last recorded pass. */
        std::size_t renderedRibbonCount() const noexcept { return m_renderedRibbons; }
        /** @brief Returns the closed particle command list, or nullptr if this frame had no particles. */
        ID3D12CommandList* getForExecution(std::uint32_t frameIndex) const noexcept;
        /** @brief Associates frame buffers and the command list with the submitted frame fence. */
        bool markSubmitted(std::uint32_t frameIndex, std::uint64_t fenceValue);

    private:
        struct QuadInstance
        {
            Vector3 origin;
            std::uint32_t isBand = 0;
            Vector3 rightOrEnd;
            float frameBlend = 0.0f;
            Vector3 upOrStartSide;
            Vector3 endSide = Vector3::Zero;
            Vector2 uvOffset = Vector2::Zero;
            Vector4 color;
            Vector4 endColor = Vector4::Zero;
            Vector4 uvBounds = Vector4(0, 0, 1, 1);
            Vector4 nextUvBounds = Vector4(0, 0, 1, 1);
        };
        struct MeshData;
        struct GpuEmitterBuffers
        {
            DX12Resource particles;
            DX12Resource drawData;
        };
        struct RetiredGpuBuffer
        {
            std::unique_ptr<GpuEmitterBuffers> buffers;
            std::uint64_t fenceValue = 0;
        };
        struct GpuEmitterState
        {
            std::unique_ptr<GpuEmitterBuffers> buffers;
            std::vector<RetiredGpuBuffer> retiredBuffers;
            std::uint32_t capacity = 0;
            std::uint32_t particleLimit = 0;
            std::uint64_t lastFenceValue = 0;
            bool retired = false;
            bool needsClear = true;
        };
        struct DrawBatch
        {
            std::uint32_t vertexCount = 0;
            D3D12_GPU_DESCRIPTOR_HANDLE texture{};
            ParticleBlendMode blendMode = ParticleBlendMode::Alpha;
            std::uint32_t appearance = 0;
            float softDistance = 0.0f;
            std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 3> auxiliaryTextures{};
            std::uint32_t textureFlags = 0;
            float uvDistortion = 0.0f;
            float refractionPixels = 0.0f;
            const MeshData* mesh = nullptr;
            std::uint32_t firstInstance = 0;
            std::uint32_t instanceCount = 0;
        };
        struct DrawItem
        {
            enum class Kind : std::uint8_t { Particle, Trail, Ribbon };
            std::size_t index = 0;
            float depth = 0;
            Kind kind = Kind::Particle;
        };
        struct MeshVertex
        {
            Vector3 position;
            Vector2 uv;
        };
        struct MeshData
        {
            ModelHandle handle;
            std::shared_ptr<const ModelResource> source;
            std::vector<MeshVertex> vertices;
            std::unique_ptr<DX12UploadBuffer> vertexBuffer;
        };
        struct MeshInstance
        {
            Vector3 position;
            Vector3 right;
            Vector3 up;
            Vector3 normal;
            Vector4 color;
            Vector4 uvBounds;
            Vector2 uvOffset;
            Vector4 nextUvBounds;
            float frameBlend;
            float padding;
        };
        struct ResolvedVisual
        {
            const ParticleRenderVisual* visual = nullptr;
            D3D12_GPU_DESCRIPTOR_HANDLE texture{};
            std::uint32_t width = 1;
            std::uint32_t height = 1;
            bool textured = false;
            bool valid = false;
            std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 3> auxiliaryTextures{};
            std::uint32_t textureFlags = 0;
            const MeshData* mesh = nullptr;
        };
        static constexpr std::uint32_t FRAME_COUNT = 2;
        static constexpr std::size_t MAX_RENDER_VERTICES = 1048576;
        static constexpr std::uint32_t QUAD_VERTEX_COUNT = 6;
        static constexpr std::size_t MAX_QUAD_INSTANCES = MAX_RENDER_VERTICES / QUAD_VERTEX_COUNT;

        bool ensureQuadInstanceCapacity(std::uint32_t frameIndex, std::size_t instanceCount);
        bool ensureMeshInstanceCapacity(std::uint32_t frameIndex, std::size_t instanceCount);
        bool ensureSceneCopy(std::uint32_t frameIndex, const DX12Resource& source);
        bool ensureGpuEmitterBuffers(GpuEmitterState& state, std::uint32_t capacity);
        bool resolveGpuTexture(const std::filesystem::path& path, D3D12_GPU_DESCRIPTOR_HANDLE& texture);
        const MeshData* resolveMesh(const std::filesystem::path& path);

        DX12Device* m_device = nullptr;
        const DX12Fence* m_fence = nullptr;
        static constexpr std::size_t BLEND_MODE_COUNT = 5;
        std::array<DX12GraphicsPipeline, BLEND_MODE_COUNT> m_forwardPipelines;
        std::array<DX12GraphicsPipeline, BLEND_MODE_COUNT> m_deferredPipelines;
        std::array<DX12GraphicsPipeline, BLEND_MODE_COUNT> m_forwardMeshPipelines;
        std::array<DX12GraphicsPipeline, BLEND_MODE_COUNT> m_deferredMeshPipelines;
        std::array<DX12GraphicsPipeline, BLEND_MODE_COUNT> m_forwardGpuPipelines;
        std::array<DX12GraphicsPipeline, BLEND_MODE_COUNT> m_deferredGpuPipelines;
        DX12ComputePipeline m_gpuSimulationPipeline;
        DX12ComputePipeline m_gpuCompactionPipeline;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_gpuDrawSignature;
        std::array<DX12CommandList, FRAME_COUNT> m_commandLists;
        std::array<DX12UploadBuffer, FRAME_COUNT> m_quadInstanceBuffers;
        std::array<DX12UploadBuffer, FRAME_COUNT> m_meshInstanceBuffers;
        std::array<DX12Resource, FRAME_COUNT> m_sceneCopies;
        std::array<std::optional<DX12DescriptorAllocation>, FRAME_COUNT> m_sceneCopyViews;
        std::array<std::size_t, FRAME_COUNT> m_quadInstanceCapacities{};
        std::array<std::size_t, FRAME_COUNT> m_meshInstanceCapacities{};
        std::array<std::vector<const MeshData*>, FRAME_COUNT> m_usedMeshes;
        std::array<bool, FRAME_COUNT> m_frameRecorded{};
        std::array<bool, FRAME_COUNT> m_frameUsesQuadInstanceBuffer{};
        std::array<bool, FRAME_COUNT> m_frameUsesMeshInstanceBuffer{};
        std::array<std::vector<std::uint64_t>, FRAME_COUNT> m_frameGpuEmitterIds;
        std::vector<ParticleRenderParticle> m_particles;
        std::vector<ParticleRenderTrailSegment> m_trails;
        std::vector<ParticleRenderRibbon> m_ribbons;
        std::vector<ParticleGpuEmitterSubmission> m_gpuEmitterSubmissions;
        std::vector<std::uint64_t> m_removedGpuEmitters;
        std::vector<DrawItem> m_drawItems;
        std::vector<QuadInstance> m_quadInstances;
        std::vector<MeshInstance> m_meshInstances;
        std::vector<DrawBatch> m_batches;
        std::vector<ResolvedVisual> m_resolvedVisuals;
        std::unordered_map<std::filesystem::path, TextureHandle> m_textureCache;
        std::unordered_map<std::filesystem::path, TextureHandle> m_linearTextureCache;
        std::array<std::unordered_map<std::filesystem::path, std::uint64_t>, 2> m_failedTextures;
        std::unordered_map<std::filesystem::path, MeshData> m_meshCache;
        std::unordered_map<std::filesystem::path, std::uint64_t> m_failedMeshes;
        std::unordered_map<std::uint64_t, std::unique_ptr<GpuEmitterState>> m_gpuEmitterStates;
        std::uint64_t m_frameNumber = 0;
        std::size_t m_renderedParticles = 0;
        std::size_t m_renderedTrails = 0;
        std::size_t m_renderedRibbons = 0;
        std::size_t m_gpuEmitterDrawCount = 0;
    };
}
