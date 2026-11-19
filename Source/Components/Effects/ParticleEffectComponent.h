#pragma once

#include <array>
#include <span>
#include "Assets\Effects\ParticleEffectAsset.h"
#include "Core\GameObject\Component.h"
#include "Graphics\Effects\ParticleRenderSubmission.h"

namespace Engine
{
    /**
     * @brief GameObjectに付ける、CPU/GPUシミュレーション式のParticle Effect。
     * @details 設定はScene/Prefabに保存し、粒子の再生状態は保存しない。
     * @thread_safety Main thread only. Rendering receives copied particle snapshots.
     */
    class ParticleEffectComponent final : public Component
    {
    public:
        ParticleEffectComponent() = default;

        /** @brief Returns authored settings, including the texture and flipbook configuration. */
        const ParticleEffectSettings& settings() const noexcept { return m_settings; }
        /** @brief Validates and replaces settings. Existing particles retain their sampled values. */
        bool setSettings(const ParticleEffectSettings& settings);
        /** @brief Assigns a shared asset, or clears the reference and retains current settings. */
        bool setAsset(ParticleEffectAssetHandle handle);
        /** @brief Returns the assigned shared asset handle. */
        ParticleEffectAssetHandle asset() const noexcept { return m_asset; }

        /** @brief Starts from the beginning with the authored seed. */
        bool play();
        /** @brief Stops emission and either clears or retains live particles according to settings. */
        void stop() noexcept;
        /** @brief Stops emission and removes all live particles and trails. */
        void clear() noexcept;
        /** @brief Pauses or resumes simulation without changing playback state. */
        void setPaused(bool paused) noexcept;
        /** @brief Returns whether the effect is emitting. */
        bool isPlaying() const noexcept { return m_emitting; }
        /** @brief Returns whether emission, particles, or fading trails remain. */
        bool isAlive() const noexcept { return m_emitting || !m_particles.empty() || m_activeTrails != 0; }
        /** @brief Submits the current particles and trails without advancing simulation. */
        void submitRenderData();
        /** @brief Starts a fresh, editor-only preview. */
        bool startPreview();
        /** @brief Stops the editor preview and clears its transient particles. */
        void stopPreview() noexcept;
        /** @brief Advances and submits an active editor preview. */
        void updatePreview(float deltaTime);
        /** @brief Suspends emitter-motion sampling while an editor preview is not eligible to update. */
        void suspendPreview() noexcept { resetEmitterMotion(); }
        /**
         * @brief Pauses or resumes an active editor preview.
         * @param paused Whether to pause the preview.
         * @return True if the preview state changed successfully.
         */
        bool setPreviewPaused(bool paused);
        /**
         * @brief Advances a paused preview by one 1/60-second simulation step.
         * @return True if the step was performed.
         */
        bool stepPreview();
        /**
         * @brief Sets the preview time multiplier in the range [0, 4].
         * @param speed Time multiplier.
         * @return True if the multiplier was accepted.
         */
        bool setPreviewSpeed(float speed);
        /** @brief Replays the preview from its seed to the specified time at the current transform. */
        bool seekPreview(float seconds);
        /** @brief Returns the persisted component payload version. */
        std::uint32_t getPayloadVersion() const noexcept override { return 16; }
        /** @brief Serializes authoring settings, excluding runtime particles. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Validates and restores a versioned particle configuration. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
        /** @brief Copies configuration and in-memory asset references without runtime particles. */
        bool copyConfigurationTo(Component& destination) const override;

    protected:
        void onStart() override;
        void onLateUpdate(float deltaTime) override;
        void onImGui() override;
        void onDisable() override;

    private:
        static constexpr std::size_t TRAIL_POINT_CAPACITY = 16;
        static constexpr std::uint32_t NO_TRAIL = UINT32_MAX;

        struct TrailPoint
        {
            Vector3 position = Vector3::Zero;
            Vector4 color = Vector4::One;
            float age = 0;
        };
        struct Trail
        {
            std::array<TrailPoint, TRAIL_POINT_CAPACITY> points{};
            TrailPoint tip;
            float lifetime = 0;
            float width = 0;
            float minDistance = 0;
            ParticleBlendMode blendMode = ParticleBlendMode::Alpha;
            std::uint32_t first = 0;
            std::uint32_t count = 0;
            bool active = false;
            bool attached = false;
        };
        struct Particle
        {
            Vector3 position = Vector3::Zero;
            Vector3 velocity = Vector3::Zero;
            Vector4 startColor = Vector4::One;
            Vector4 endColor = Vector4::One;
            float age = 0;
            float lifetime = 1;
            float startSize = 0;
            float endSize = 0;
            float rotation = 0;
            float angularVelocity = 0;
            std::shared_ptr<const ParticleLifetimeCurves> curves;
            Vector2 sizeScale = Vector2::One;
            std::uint32_t trailIndex = NO_TRAIL;
            std::uint32_t flipbookStartFrame = 0;
        };

        std::uint32_t nextRandom() noexcept;
        float randomUnit() noexcept;
        void emitBurst(const ParticleBurst& burst, double occurrences, const Matrix& worldMatrix);
        void emitParticle(const Matrix& worldMatrix, const Vector3& worldOffset = Vector3::Zero);
        void simulate(float deltaTime, const Matrix& worldMatrix);
        void advancePrewarm(int maxSteps);
        bool sampleEmitterMotion(float elapsed, const Matrix& worldMatrix);
        void resetEmitterMotion() noexcept;
        void updateGpuSimulation(float deltaTime, const Matrix& worldMatrix);
        bool submitGpuEmitter(float deltaTime);
        void emitByDistance(float simulationDelta, double initialDelay, double initialElapsed,
            bool wasEmitting, const Matrix& worldMatrix);
        bool buildRenderSnapshot(const Matrix& worldMatrix);
        Vector4 particleColor(const Particle& particle) const noexcept;
        void appendTrailPoint(Trail& trail, const TrailPoint& point) noexcept;
        void detachTrail(Trail& trail) noexcept;
        void updateTrail(Particle& particle, const Matrix& worldMatrix, bool detach) noexcept;
        void clearTrails() noexcept;
        bool synchronizeAsset();

        ParticleEffectSettings m_settings;
        ParticleEffectAssetHandle m_asset;
        std::shared_ptr<const ParticleEffectAsset> m_assetSnapshot;
        std::shared_ptr<const ParticleRenderVisual> m_renderVisual;
        std::shared_ptr<const ParticleLifetimeCurves> m_lifetimeCurves;
        std::vector<std::shared_ptr<const ParticleLifetimeCurves>> m_curveVariants;
        std::vector<Particle> m_particles;
        std::vector<ParticleRenderParticle> m_renderParticles;
        std::vector<Trail> m_trails;
        std::vector<std::uint32_t> m_freeTrails;
        std::vector<ParticleRenderTrailSegment> m_renderTrails;
        std::size_t m_activeTrails = 0;
        double m_emissionRemainder = 0.0;
        double m_emissionElapsed = 0;
        double m_delayRemaining = 0;
        double m_prewarmRemaining = 0;
        Matrix m_prewarmWorld = Matrix::Identity;
        bool m_prewarmFailed = false;
        Vector3 m_previousEmitterPosition = Vector3::Zero;
        Vector3 m_motionStart = Vector3::Zero;
        Vector3 m_motionEnd = Vector3::Zero;
        Vector3 m_inheritedVelocity = Vector3::Zero;
        Matrix m_motionInverse = Matrix::Identity;
        double m_distanceRemainder = 0;
        double m_motionElapsed = 0;
        bool m_hasEmitterPosition = false;
        bool m_motionValid = false;
        std::uint64_t m_previewSelectionRevision = 0;
        std::uint32_t m_randomState = 1;
        std::uint64_t m_gpuEmitterId = 0;
        std::uint32_t m_gpuPendingSeed = 1;
        float m_gpuPendingDeltaTime = 0.0f;
        float m_gpuPendingEmissionTime = 0.0f;
        double m_gpuLiveTimeRemaining = 0.0;
        bool m_emitting = false;
        bool m_burstPending = false;
        bool m_paused = false;
        bool m_deltaCappedReported = false;
        bool m_trailBudgetReported = false;
        bool m_previewActive = false;
        bool m_gpuClearPending = false;
        bool m_previewPaused = false;
        float m_previewSpeed = 1.0f;
        float m_previewSeekTime = 0.0f;
        double m_previewElapsed = 0.0;
        double m_previewRemainder = 0.0;
        double m_previewSeekRemaining = 0.0;
        Matrix m_previewSeekWorld = Matrix::Identity;
        bool m_previewSeeking = false;
        bool m_previewSeekFailed = false;
        int m_curveEditIndex = 0;
        bool m_applyStatusVisible = false;
        bool m_applySucceeded = false;
        bool m_statusWasDiscard = false;
        std::uint64_t m_droppedParticles = 0;
        std::uint64_t m_droppedTrails = 0;
        double m_cpuMilliseconds = 0.0;
        bool m_submissionFailed = false;
    };
}
