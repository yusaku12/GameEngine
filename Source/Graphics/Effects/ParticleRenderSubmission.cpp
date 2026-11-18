#include "Pch.h"
#include "Graphics\Effects\ParticleRenderSubmission.h"
#include "Core\Logging\Logger.h"

namespace Engine
{
    namespace
    {
        bool isFinite(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool isFinite(const Vector4& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y)
                && std::isfinite(value.z) && std::isfinite(value.w);
        }

        bool isFinite(const Matrix& value) noexcept
        {
            const std::array elements{
                value._11, value._12, value._13, value._14,
                value._21, value._22, value._23, value._24,
                value._31, value._32, value._33, value._34,
                value._41, value._42, value._43, value._44,
            };
            return std::all_of(elements.begin(), elements.end(),
                [](const float element) { return std::isfinite(element); });
        }

        bool validColor(const Vector4& color) noexcept
        {
            return isFinite(color) && color.x >= 0.0f && color.x <= 100.0f
                && color.y >= 0.0f && color.y <= 100.0f && color.z >= 0.0f && color.z <= 100.0f
                && color.w >= 0.0f && color.w <= 1.0f;
        }
    }

    ParticleRenderSubmissionQueue& ParticleRenderSubmissionQueue::instance() noexcept
    {
        static ParticleRenderSubmissionQueue queue;
        return queue;
    }

    bool ParticleRenderSubmissionQueue::submit(const std::span<const ParticleRenderParticle> particles,
        const std::span<const ParticleRenderTrailSegment> trails,
        const std::span<const ParticleRenderRibbon> ribbons)
    {
        if (particles.empty() && trails.empty() && ribbons.empty())
            return true;

        for (const ParticleRenderParticle& particle : particles)
        {
            if (!isFinite(particle.position) || !isFinite(particle.velocity) || !isFinite(particle.color)
                || particle.color.x < 0.0f || particle.color.y < 0.0f || particle.color.z < 0.0f
                || particle.color.x > 100.0f || particle.color.y > 100.0f || particle.color.z > 100.0f
                || particle.color.w < 0.0f || particle.color.w > 1.0f
                || !std::isfinite(particle.size) || particle.size <= 0.0f
                || !std::isfinite(particle.sizeScale.x) || particle.sizeScale.x <= 0.0f
                || !std::isfinite(particle.sizeScale.y) || particle.sizeScale.y <= 0.0f
                || !std::isfinite(particle.rotation)
                || !std::isfinite(particle.uvOffset.x) || !std::isfinite(particle.uvOffset.y)
                || !std::isfinite(particle.uvMin.x) || !std::isfinite(particle.uvMin.y)
                || !std::isfinite(particle.uvMax.x) || !std::isfinite(particle.uvMax.y)
                || !std::isfinite(particle.nextUvMin.x) || !std::isfinite(particle.nextUvMin.y)
                || !std::isfinite(particle.nextUvMax.x) || !std::isfinite(particle.nextUvMax.y)
                || !std::isfinite(particle.frameBlend) || particle.frameBlend < 0.0f || particle.frameBlend > 1.0f
                || particle.uvMin.x < 0.0f || particle.uvMin.y < 0.0f
                || particle.uvMax.x > 1.0f || particle.uvMax.y > 1.0f
                || particle.uvMin.x >= particle.uvMax.x || particle.uvMin.y >= particle.uvMax.y
                || particle.nextUvMin.x < 0.0f || particle.nextUvMin.y < 0.0f
                || particle.nextUvMax.x > 1.0f || particle.nextUvMax.y > 1.0f
                || particle.nextUvMin.x >= particle.nextUvMax.x
                || particle.nextUvMin.y >= particle.nextUvMax.y
                || (particle.visual
                    && (!isParticleBlendModeValid(particle.visual->blendMode)
                        || particle.visual->flipbookColumns == 0 || particle.visual->flipbookColumns > 64
                        || particle.visual->flipbookRows == 0 || particle.visual->flipbookRows > 64
                        || particle.visual->orientation > ParticleOrientation::Stretched
                        || !isFinite(particle.visual->orientationDirection)
                        || !std::isfinite(particle.visual->orientationDirection.LengthSquared())
                        || particle.visual->orientationDirection.LengthSquared() < 1e-8f
                        || !std::isfinite(particle.visual->stretchBySpeed)
                        || particle.visual->stretchBySpeed < 0.0f || particle.visual->stretchBySpeed > 10000.0f
                        || !std::isfinite(particle.visual->softDistance)
                        || particle.visual->softDistance < 0.0f || particle.visual->softDistance > 10000.0f
                        || !std::isfinite(particle.visual->uvDistortion)
                        || std::abs(particle.visual->uvDistortion) > 10.0f
                        || !std::isfinite(particle.visual->refractionPixels)
                        || std::abs(particle.visual->refractionPixels) > 1024.0f
                        || particle.visual->secondaryTexturePath.native().find(L'\0') != std::wstring::npos
                        || particle.visual->maskTexturePath.native().find(L'\0') != std::wstring::npos
                        || particle.visual->distortionTexturePath.native().find(L'\0') != std::wstring::npos
                        || particle.visual->meshPath.native().find(L'\0') != std::wstring::npos
                        || particle.visual->texturePath.native().find(L'\0') != std::wstring::npos)))
            {
                LOG_ERROR("[ParticleEffect] Render submission contains invalid particle data.");
                return false;
            }
        }
        for (const ParticleRenderTrailSegment& trail : trails)
        {
            if (!isFinite(trail.previous) || !isFinite(trail.start) || !isFinite(trail.end)
                || !isFinite(trail.next) || !validColor(trail.startColor) || !validColor(trail.endColor)
                || !std::isfinite(trail.startWidth) || trail.startWidth < 0.0f
                || !std::isfinite(trail.endWidth) || trail.endWidth < 0.0f
                || !isParticleBlendModeValid(trail.blendMode))
            {
                LOG_ERROR("[ParticleEffect] Render submission contains invalid trail data.");
                return false;
            }
        }
        std::size_t ribbonPointCount = 0;
        for (const ParticleRenderRibbon& ribbon : ribbons)
        {
            if (ribbon.points.size() < 2 || ribbon.points.size() > 256
                || !isParticleBlendModeValid(ribbon.blendMode)
                || ribbon.points.size() > MAX_RENDER_RIBBON_POINTS - ribbonPointCount)
            {
                LOG_ERROR("[ParticleEffect] Render submission contains invalid ribbon data.");
                return false;
            }
            ribbonPointCount += ribbon.points.size();
            for (const ParticleRenderRibbonPoint& point : ribbon.points)
                if (!isFinite(point.position) || !validColor(point.color)
                    || !std::isfinite(point.width) || point.width <= 0.0f || point.width > 10000.0f)
                {
                    LOG_ERROR("[ParticleEffect] Render submission contains an invalid ribbon point.");
                    return false;
                }
        }

        std::vector<ParticleRenderRibbon> ribbonCopies;
        try
        {
            ribbonCopies.assign(ribbons.begin(), ribbons.end());
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ParticleEffect] Failed to copy submitted ribbon geometry.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ParticleEffect] Submitted ribbon geometry exceeds vector capacity.");
            return false;
        }

        const std::scoped_lock lock(m_mutex);
        if (particles.size() > MAX_RENDER_PARTICLES - std::min(m_pending.size(), MAX_RENDER_PARTICLES))
        {
            LOG_ERROR("[ParticleEffect] Render particle budget of {} was exceeded.", MAX_RENDER_PARTICLES);
            return false;
        }
        if (trails.size() > MAX_RENDER_TRAIL_SEGMENTS
            - std::min(m_pendingTrails.size(), MAX_RENDER_TRAIL_SEGMENTS))
        {
            LOG_ERROR("[ParticleEffect] Render trail segment budget of {} was exceeded.", MAX_RENDER_TRAIL_SEGMENTS);
            return false;
        }
        if (ribbons.size() > MAX_RENDER_RIBBONS
            - std::min(m_pendingRibbons.size(), MAX_RENDER_RIBBONS))
        {
            LOG_ERROR("[ParticleEffect] Render ribbon budget of {} was exceeded.", MAX_RENDER_RIBBONS);
            return false;
        }
        std::size_t pendingRibbonPoints = 0;
        for (const auto& ribbon : m_pendingRibbons)
            pendingRibbonPoints += ribbon.points.size();
        if (ribbonPointCount > MAX_RENDER_RIBBON_POINTS
            - std::min(pendingRibbonPoints, MAX_RENDER_RIBBON_POINTS))
        {
            LOG_ERROR("[ParticleEffect] Render ribbon point budget of {} was exceeded.", MAX_RENDER_RIBBON_POINTS);
            return false;
        }
        try
        {
            const auto particleCount = m_pending.size() + particles.size();
            const auto trailCount = m_pendingTrails.size() + trails.size();
            const auto ribbonCount = m_pendingRibbons.size() + ribbons.size();
            if (particleCount > m_pending.capacity())
                m_pending.reserve(std::min(MAX_RENDER_PARTICLES,
                    std::max(particleCount, m_pending.capacity() * 2)));
            if (trailCount > m_pendingTrails.capacity())
                m_pendingTrails.reserve(std::min(MAX_RENDER_TRAIL_SEGMENTS,
                    std::max(trailCount, m_pendingTrails.capacity() * 2)));
            if (ribbonCount > m_pendingRibbons.capacity())
                m_pendingRibbons.reserve(std::min(MAX_RENDER_RIBBONS,
                    std::max(ribbonCount, m_pendingRibbons.capacity() * 2)));
            m_pending.insert(m_pending.end(), particles.begin(), particles.end());
            m_pendingTrails.insert(m_pendingTrails.end(), trails.begin(), trails.end());
            m_pendingRibbons.insert(m_pendingRibbons.end(),
                std::make_move_iterator(ribbonCopies.begin()), std::make_move_iterator(ribbonCopies.end()));
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ParticleEffect] Failed to allocate render submission storage.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ParticleEffect] Render submission exceeds the maximum capacity.");
            return false;
        }
        return true;
    }

    const char* ParticleRenderSubmissionQueue::gpuSimulationFallbackReason(const ParticleEffectSettings& settings) const noexcept
    {
        if (!settings.meshPath.empty())
            return "GPU simulation does not support Mesh particles.";
        if (!settings.localSpace)
            return "GPU simulation currently requires Local Space.";
        if (settings.burstCount != 0 || !settings.bursts.empty() || !settings.emissionRateKeys.empty()
            || settings.startDelay > 0.0f || settings.prewarm
            || settings.distancePerParticle > 0.0f || settings.inheritVelocity > 0.0f)
            return "GPU simulation does not support bursts, scheduled/distance emission, velocity inheritance, delay, or prewarm.";
        if (!gpuSortingSupported() && (settings.trailEnabled || settings.blendMode != ParticleBlendMode::Additive
            || settings.orientation != ParticleOrientation::Camera
            || settings.softDistance > 0.0f || settings.refractionPixels != 0.0f
            || !settings.secondaryTexturePath.empty() || !settings.maskTexturePath.empty()
            || !settings.distortionTexturePath.empty() || settings.uvDistortion != 0.0f
            || settings.uvScroll != Vector2::Zero || settings.flipbookPlayback != ParticleFlipbookPlayback::Lifetime
            || settings.flipbookLoopCount != 1 || settings.flipbookRandomStartFrame || settings.flipbookFrameBlending))
            return "Advanced GPU particle rendering requires Direct3D 12 Resource Binding Tier 2.";
        return nullptr;
    }

    bool ParticleRenderSubmissionQueue::submitGpuEmitter(const ParticleGpuEmitterSubmission& emitter)
    {
        if (emitter.id == 0 || emitter.settings.simulationBackend != ParticleSimulationBackend::GPU
            || emitter.settings.maxParticles == 0
            || emitter.settings.maxParticles > ParticleRenderSubmissionQueue::MAX_RENDER_PARTICLES
            || emitter.spawnCount > emitter.settings.maxParticles
            || emitter.spawnCursor >= emitter.settings.maxParticles
            || !std::isfinite(emitter.deltaTime) || emitter.deltaTime < 0.0f
            || emitter.deltaTime > 0.25f || !isFinite(emitter.worldMatrix))
        {
            LOG_ERROR("[ParticleEffect] GPU emitter submission contains invalid state.");
            return false;
        }
        const std::scoped_lock lock(m_mutex);
        const auto existing = std::find_if(m_pendingGpuEmitters.begin(), m_pendingGpuEmitters.end(),
            [&emitter](const ParticleGpuEmitterSubmission& item) { return item.id == emitter.id; });
        std::size_t pendingParticleCapacity = 0;
        for (const ParticleGpuEmitterSubmission& item : m_pendingGpuEmitters)
            if (item.id != emitter.id)
                pendingParticleCapacity += item.settings.maxParticles;
        if (emitter.settings.maxParticles > MAX_RENDER_GPU_PARTICLES
            - std::min(pendingParticleCapacity, MAX_RENDER_GPU_PARTICLES))
        {
            LOG_ERROR("[ParticleEffect] GPU particle slot budget of {} was exceeded.", MAX_RENDER_GPU_PARTICLES);
            return false;
        }
        try
        {
            if (existing != m_pendingGpuEmitters.end())
                *existing = emitter;
            else
                m_pendingGpuEmitters.push_back(emitter);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ParticleEffect] Failed to queue GPU emitter state.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ParticleEffect] GPU emitter queue exceeds vector capacity.");
            return false;
        }
        return true;
    }

    void ParticleRenderSubmissionQueue::removeGpuEmitter(const std::uint64_t id)
    {
        if (id == 0)
            return;
        const std::scoped_lock lock(m_mutex);
        std::erase_if(m_pendingGpuEmitters,
            [id](const ParticleGpuEmitterSubmission& emitter) { return emitter.id == id; });
        if (std::find(m_removedGpuEmitters.begin(), m_removedGpuEmitters.end(), id) != m_removedGpuEmitters.end())
            return;
        try
        {
            m_removedGpuEmitters.push_back(id);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ParticleEffect] Failed to queue GPU emitter removal.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ParticleEffect] GPU emitter removal queue exceeds vector capacity.");
        }
    }

    void ParticleRenderSubmissionQueue::consume(std::vector<ParticleRenderParticle>& destination)
    {
        const std::scoped_lock lock(m_mutex);
        destination.clear();
        destination.swap(m_pending);
        m_pendingTrails.clear();
        m_pendingRibbons.clear();
    }

    void ParticleRenderSubmissionQueue::consume(std::vector<ParticleRenderParticle>& particles,
        std::vector<ParticleRenderTrailSegment>& trails)
    {
        const std::scoped_lock lock(m_mutex);
        particles.clear();
        particles.swap(m_pending);
        trails.clear();
        trails.swap(m_pendingTrails);
        m_pendingRibbons.clear();
    }

    void ParticleRenderSubmissionQueue::consume(std::vector<ParticleRenderParticle>& particles,
        std::vector<ParticleRenderTrailSegment>& trails, std::vector<ParticleRenderRibbon>& ribbons)
    {
        const std::scoped_lock lock(m_mutex);
        particles.clear();
        particles.swap(m_pending);
        trails.clear();
        trails.swap(m_pendingTrails);
        ribbons.clear();
        ribbons.swap(m_pendingRibbons);
    }

    void ParticleRenderSubmissionQueue::consume(std::vector<ParticleRenderParticle>& particles,
        std::vector<ParticleRenderTrailSegment>& trails, std::vector<ParticleRenderRibbon>& ribbons,
        std::vector<ParticleGpuEmitterSubmission>& gpuEmitters,
        std::vector<std::uint64_t>& removedGpuEmitters)
    {
        const std::scoped_lock lock(m_mutex);
        particles.clear();
        particles.swap(m_pending);
        trails.clear();
        trails.swap(m_pendingTrails);
        ribbons.clear();
        ribbons.swap(m_pendingRibbons);
        gpuEmitters.clear();
        gpuEmitters.swap(m_pendingGpuEmitters);
        removedGpuEmitters.clear();
        removedGpuEmitters.swap(m_removedGpuEmitters);
    }

    void ParticleRenderSubmissionQueue::clear() noexcept
    {
        const std::scoped_lock lock(m_mutex);
        m_pending.clear();
        m_pendingTrails.clear();
        m_pendingRibbons.clear();
        m_pendingGpuEmitters.clear();
        m_removedGpuEmitters.clear();
        for (auto& failures : m_failedResources)
            failures.clear();
        m_gpuStatistics = {};
    }

    void ParticleRenderSubmissionQueue::publishGpuStatistics(const ParticleGpuStatistics statistics)
    {
        const std::scoped_lock lock(m_mutex);
        m_gpuStatistics = statistics;
    }

    ParticleGpuStatistics ParticleRenderSubmissionQueue::gpuStatistics()
    {
        const std::scoped_lock lock(m_mutex);
        return m_gpuStatistics;
    }

    void ParticleRenderSubmissionQueue::setResourceFailed(const std::filesystem::path& path,
        const ParticleResourceKind kind, const bool failed)
    {
        const std::scoped_lock lock(m_mutex);
        const auto index = static_cast<std::size_t>(kind);
        if (index >= m_failedResources.size())
        {
            LOG_ERROR("[ParticleEffect] Invalid particle resource diagnostic kind.");
            return;
        }
        auto& failures = m_failedResources[index];
        if (failed)
            failures.insert(path);
        else
            failures.erase(path);
    }

    bool ParticleRenderSubmissionQueue::resourceFailed(const std::filesystem::path& path, const ParticleResourceKind kind)
    {
        const std::scoped_lock lock(m_mutex);
        const auto index = static_cast<std::size_t>(kind);
        if (index >= m_failedResources.size())
        {
            LOG_ERROR("[ParticleEffect] Invalid particle resource diagnostic kind.");
            return true;
        }
        return m_failedResources[index].contains(path);
    }
}
