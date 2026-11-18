#pragma once

#include "Core\CoreDefines.h"
#include "Core\Math\MathTypes.h"
#include "Assets\Effects\ParticleEffectAsset.h"
#include <array>
#include <memory>
#include <span>
#include <unordered_set>
#include <vector>

namespace Engine
{
    /** @brief Resource usage used to distinguish independently loaded particle assets. */
    enum class ParticleResourceKind
    {
        ColorTexture,
        LinearTexture,
        Mesh,
        Count
    };

    /** @brief Immutable visual configuration shared by particles in a submission. */
    struct ParticleRenderVisual
    {
        std::filesystem::path texturePath;
        ParticleBlendMode blendMode = ParticleBlendMode::Alpha;
        std::uint32_t flipbookColumns = 1;
        std::uint32_t flipbookRows = 1;
        ParticleOrientation orientation = ParticleOrientation::Camera;
        Vector3 orientationDirection = Vector3::Up;
        float stretchBySpeed = 0.0f;
        float softDistance = 0.0f;
        std::filesystem::path secondaryTexturePath;
        std::filesystem::path maskTexturePath;
        std::filesystem::path distortionTexturePath;
        float uvDistortion = 0.0f;
        float refractionPixels = 0.0f;
        std::filesystem::path meshPath;
    };

    /** @brief Particle data with an immutable visual snapshot copied to rendering. */
    struct ParticleRenderParticle
    {
        Vector3 position = Vector3::Zero;
        Vector4 color = Vector4::One;
        float size = 0;
        float rotation = 0;
        Vector2 uvMin = Vector2::Zero;
        Vector2 uvMax = Vector2::One;
        std::shared_ptr<const ParticleRenderVisual> visual;
        Vector2 sizeScale = Vector2::One; //!< Horizontal and vertical billboard scales.
        Vector3 velocity = Vector3::Zero; //!< Current world-space particle velocity.
        Vector2 uvOffset = Vector2::Zero; //!< Scroll in flipbook-cell units.
        Vector2 nextUvMin = Vector2::Zero;
        Vector2 nextUvMax = Vector2::One;
        float frameBlend = 0.0f;
    };

    /** @brief One camera-facing, world-space trail segment with neighboring points for its joins. */
    struct ParticleRenderTrailSegment
    {
        Vector3 previous = Vector3::Zero;
        Vector3 start = Vector3::Zero;
        Vector3 end = Vector3::Zero;
        Vector3 next = Vector3::Zero;
        Vector4 startColor = Vector4::One;
        Vector4 endColor = Vector4::One;
        float startWidth = 0;
        float endWidth = 0;
        ParticleBlendMode blendMode = ParticleBlendMode::Alpha;
    };

    /** @brief Validated render-space control point for an independent ribbon. */
    struct ParticleRenderRibbonPoint
    {
        Vector3 position = Vector3::Zero;
        Vector4 color = Vector4::One;
        float width = 0.1f;
    };

    /** @brief Independent polyline submitted as a camera-facing ribbon. */
    /** @brief Independent polyline submitted as a camera-facing ribbon. */
    struct ParticleRenderRibbon
    {
        std::vector<ParticleRenderRibbonPoint> points;
        ParticleBlendMode blendMode = ParticleBlendMode::Alpha;
    };

    /** @brief Frame-local command for a persistent GPU-simulated particle emitter. */
    struct ParticleGpuEmitterSubmission
    {
        std::uint64_t id = 0;
        ParticleEffectSettings settings;
        Matrix worldMatrix = Matrix::Identity;
        float deltaTime = 0.0f;
        std::uint32_t spawnCount = 0;
        std::uint32_t spawnCursor = 0;
        std::uint32_t spawnSeed = 1;
        bool clear = false;
        bool emitting = false;
    };

    /** @brief Most recently completed GPU measurement for the combined particle pass. */
    struct ParticleGpuStatistics
    {
        std::uint64_t frameId = 0;
        double milliseconds = 0.0;
        std::size_t drawCalls = 0;
        std::size_t cpuParticles = 0;
        std::size_t trailSegments = 0;
        std::size_t ribbons = 0;
    };

    /**
     * @brief Game updateからRender threadへ一時的なParticle snapshotを渡すQueue。
     * @thread_safety Thread-safe.
     */
    class ParticleRenderSubmissionQueue
    {
    public:
        static constexpr std::size_t MAX_RENDER_PARTICLES = 65536;
        static constexpr std::size_t MAX_RENDER_TRAIL_SEGMENTS = 65536;
        static constexpr std::size_t MAX_RENDER_RIBBONS = 4096;
        static constexpr std::size_t MAX_RENDER_RIBBON_POINTS = 65536;
        static constexpr std::size_t MAX_RENDER_GPU_PARTICLES = 65536;

        static ParticleRenderSubmissionQueue& instance() noexcept;

        GE_DISABLE_COPY_AND_MOVE(ParticleRenderSubmissionQueue);

        /** @brief Atomically appends validated particle and trail snapshots. */
        bool submit(std::span<const ParticleRenderParticle> particles,
            std::span<const ParticleRenderTrailSegment> trails = {},
            std::span<const ParticleRenderRibbon> ribbons = {});
        /** @brief Queues one GPU emitter update, replacing an earlier update for the same emitter. */
        bool submitGpuEmitter(const ParticleGpuEmitterSubmission& emitter);
        /** @brief Queues removal of persistent state for a disabled or destroyed emitter. */
        void removeGpuEmitter(std::uint64_t id);
        /** @brief Moves pending particles and discards any pending trail segments. */
        void consume(std::vector<ParticleRenderParticle>& destination);
        /** @brief Moves both pending snapshot types to reusable render-thread buffers. */
        void consume(std::vector<ParticleRenderParticle>& particles,
            std::vector<ParticleRenderTrailSegment>& trails);
        /** @brief Moves all pending snapshot types to reusable render-thread buffers. */
        void consume(std::vector<ParticleRenderParticle>& particles,
            std::vector<ParticleRenderTrailSegment>& trails, std::vector<ParticleRenderRibbon>& ribbons);
        /** @brief Moves all CPU and GPU particle submissions to the render thread. */
        void consume(std::vector<ParticleRenderParticle>& particles,
            std::vector<ParticleRenderTrailSegment>& trails, std::vector<ParticleRenderRibbon>& ribbons,
            std::vector<ParticleGpuEmitterSubmission>& gpuEmitters, std::vector<std::uint64_t>& removedGpuEmitters);
        /** @brief Discards queued particles during renderer shutdown. */
        void clear() noexcept;
        /** @brief Publishes the last completed combined particle GPU measurement. */
        void publishGpuStatistics(ParticleGpuStatistics statistics);
        /** @brief Returns the last completed combined particle GPU measurement. */
        ParticleGpuStatistics gpuStatistics();
        /** @brief Tracks resource load failures for editor diagnostics. */
        void setResourceFailed(const std::filesystem::path& path, ParticleResourceKind kind, bool failed);
        /** @brief Returns whether the last load for this resource usage failed. */
        bool resourceFailed(const std::filesystem::path& path, ParticleResourceKind kind);

    private:
        ParticleRenderSubmissionQueue() = default;
        ~ParticleRenderSubmissionQueue() = default;

        std::mutex m_mutex;
        std::vector<ParticleRenderParticle> m_pending;
        std::vector<ParticleRenderTrailSegment> m_pendingTrails;
        std::vector<ParticleRenderRibbon> m_pendingRibbons;
        std::vector<ParticleGpuEmitterSubmission> m_pendingGpuEmitters;
        std::vector<std::uint64_t> m_removedGpuEmitters;
        std::array<std::unordered_set<std::filesystem::path>, static_cast<std::size_t>(ParticleResourceKind::Count)>
            m_failedResources;
        ParticleGpuStatistics m_gpuStatistics;
    };
}
