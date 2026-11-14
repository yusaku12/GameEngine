#include "Pch.h"
#include "Graphics\Lighting\LightingFrame.h"
#include "Core\Scene\Scene.h"
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\Math\Transform.h"

namespace Engine
{
    LightingFrame& LightingFrame::instance() noexcept
    {
        static LightingFrame frame;
        return frame;
    }

    void LightingFrame::capture(const Scene* active, const Scene& persistent) noexcept
    {
        m_constants = {};
        m_shadowIndex = -1;
        m_atmosphere = {};
        m_atmosphereCenter = Vector3::Zero;
        m_atmosphereSuns = {};
        m_hasAtmosphere = false;
        m_hasCloud = false;
        m_hasFog = false;
        m_hasLightShafts = false;
        std::array<bool, 2> sunIndices{};
        bool duplicateAtmosphere = false;
        bool duplicateCloud = false;
        bool duplicateFog = false;
        bool duplicateLightShafts = false;
        bool duplicateSun = false;
        std::size_t count = 0;
        bool overflow = false;
        const auto collect = [&](const Scene& scene)
        {
            for (const auto& object : scene.getGameObjects())
            {
                if (!object->isActiveInHierarchy() || object->isDestroyRequested())
                    continue;
                if (const auto* atmosphere = object->getComponent<SkyAtmosphereComponent>();
                    atmosphere && atmosphere->isEnabled() && atmosphere->settings().enabled)
                {
                    if (!m_hasAtmosphere)
                    {
                        m_atmosphere = atmosphere->settings();
                        m_atmosphereCenter = object->getWorldPosition();
                        m_hasAtmosphere = true;
                    }
                    else
                    {
                        duplicateAtmosphere = true;
                    }
                }
                if (const auto* cloud = object->getComponent<VolumetricCloudComponent>();
                    cloud && cloud->isEnabled() && cloud->settings().enabled)
                {
                    if (!m_hasCloud)
                    {
                        m_cloud = cloud->settings();
                        m_cloudAnchor = object->getWorldPosition();
                        m_hasCloud = true;
                    }
                    else
                    {
                        duplicateCloud = true;
                    }
                }
                if (const auto* fog = object->getComponent<VolumetricFogComponent>();
                    fog && fog->isEnabled() && fog->settings().enabled)
                {
                    if (!m_hasFog)
                    {
                        m_fog = fog->settings();
                        m_fogHeight = object->getWorldPosition().y;
                        m_hasFog = true;
                    }
                    else
                    {
                        duplicateFog = true;
                    }
                }
                const auto* light = object->getComponent<LightComponent>();
                if (!light || !light->isEnabled())
                    continue;
                const auto& settings = light->settings();
                if (settings.atmosphereSunLight && settings.kind == LightKind::Directional)
                {
                    const std::size_t sunIndex = settings.atmosphereSunIndex;
                    if (sunIndices[sunIndex])
                    {
                        duplicateSun = true;
                    }
                    else
                    {
                        Vector3 direction = Vector3::Transform(Vector3::UnitZ, object->getWorldRotation());
                        direction.Normalize();
                        m_atmosphereSuns[sunIndex] = {
                            Vector4(-direction.x, -direction.y, -direction.z, 1.0f),
                            Vector4(settings.color.x * settings.intensity, settings.color.y * settings.intensity,
                                settings.color.z * settings.intensity, 1.0f) };
                        sunIndices[sunIndex] = true;
                    }
                }
                if (count == m_constants.lights.size())
                {
                    overflow = true;
                    continue;
                }
                Vector3 direction = Vector3::Transform(Vector3::UnitZ, object->getWorldRotation());
                direction.Normalize();
                if (settings.kind == LightKind::Directional
                    && (settings.lightShaftBloomEnabled || settings.lightShaftOcclusionEnabled))
                {
                    if (!m_hasLightShafts)
                    {
                        m_lightShaftLight = settings;
                        m_lightShaftDirection = -direction;
                        m_hasLightShafts = true;
                    }
                    else
                    {
                        duplicateLightShafts = true;
                    }
                }
                m_constants.lights[count] = {
                    Vector4(object->getWorldPosition().x, object->getWorldPosition().y,
                        object->getWorldPosition().z, static_cast<float>(settings.kind)),
                    Vector4(settings.color.x * settings.intensity, settings.color.y * settings.intensity,
                        settings.color.z * settings.intensity, settings.range),
                    Vector4(direction.x, direction.y, direction.z,
                        std::cos(DirectX::XMConvertToRadians(settings.innerAngle * 0.5f))),
                    Vector4(std::cos(DirectX::XMConvertToRadians(settings.outerAngle * 0.5f)), 0,
                        settings.volumetricScatteringIntensity, 0) };
                if (settings.kind == LightKind::Directional && settings.castsShadow && settings.intensity > 0
                    && m_shadowIndex < 0)
                {
                    m_shadowIndex = static_cast<int>(count);
                    m_shadow = settings;
                    m_constants.lights[count].parameters.y = 1;
                }
                ++count;
            }
        };
        if (active)
            collect(*active);
        collect(persistent);
        m_constants.environment.w = static_cast<float>(count);
        if (overflow)
            LOG_ERROR("[Light] More than 32 active lights; excess lights are not rendered.");
        if (duplicateAtmosphere && !m_duplicateAtmosphereReported)
            LOG_ERROR("[Sky Atmosphere] Multiple active components were found; only the first is rendered.");
        if (duplicateSun && !m_duplicateSunReported)
            LOG_ERROR("[Sky Atmosphere] Multiple directional lights use the same atmosphere sun index; only the first is used.");
        if (duplicateCloud && !m_duplicateCloudReported)
            LOG_ERROR("[Volumetric Cloud] Multiple active components were found; only the first is rendered.");
        if (duplicateFog && !m_duplicateFogReported)
            LOG_ERROR("[Volumetric Fog] Multiple active components were found; only the first is rendered.");
        if (duplicateLightShafts && !m_duplicateLightShaftsReported)
            LOG_ERROR("[Light Shafts] Multiple directional lights were configured; only the first is rendered.");
        m_duplicateAtmosphereReported = duplicateAtmosphere;
        m_duplicateSunReported = duplicateSun;
        m_duplicateCloudReported = duplicateCloud;
        m_duplicateFogReported = duplicateFog;
        m_duplicateLightShaftsReported = duplicateLightShafts;
    }

    LightingConstants LightingFrame::forCamera(const Vector3& position, const Vector3& forward,
        const Matrix& viewProjection, const Matrix& inverseUnjitteredViewProjection, const Matrix& unjitteredProjection,
        const float nearClip, const float farClip, const bool hasCameraView) const noexcept
    {
        LightingConstants result = m_constants;
        result.cameraPosition = Vector4(position.x, position.y, position.z, 1);
        if (m_hasCloud)
        {
            const Vector3 cameraFromAnchor = (position - m_cloudAnchor) / m_cloud.worldUnitsPerKilometer;
            result.cloud.options = Vector4(1.0f, m_cloud.baseHeightKm, m_cloud.thicknessKm,
                m_cloud.maxDistanceKm);
            result.cloud.shape = Vector4(m_cloud.coverage, m_cloud.density, m_cloud.noiseScaleKm,
                static_cast<float>(m_cloud.sampleCount));
            result.cloud.cameraFromAnchor = Vector4(cameraFromAnchor.x, cameraFromAnchor.y, cameraFromAnchor.z,
                m_cloud.worldUnitsPerKilometer);
        }
        if (m_hasFog)
        {
            result.fog.options = Vector4(1.0f, m_fog.density, m_fog.heightFalloff, m_fog.extinctionScale);
            result.fog.appearance = Vector4(m_fog.albedo.x, m_fog.albedo.y, m_fog.albedo.z,
                m_fog.scatteringDistribution);
            result.fog.emissive = Vector4(m_fog.emissive.x, m_fog.emissive.y, m_fog.emissive.z,
                m_fog.maxOpacity);
            result.fog.distances = Vector4(m_fog.startDistance, m_fog.nearFadeInDistance,
                m_fog.viewDistance, m_fogHeight);
            result.fog.samples = Vector4(static_cast<float>(m_fog.sampleCount), 0, 0, 0);
        }
        if (m_hasLightShafts)
        {
            const Vector4 clipPosition = Vector4::Transform(Vector4(m_lightShaftDirection.x,
                m_lightShaftDirection.y, m_lightShaftDirection.z, 0.0f), viewProjection);
            float sourceX = 0.5f;
            float sourceY = 0.5f;
            bool sourceVisible = false;
            if (std::isfinite(clipPosition.x) && std::isfinite(clipPosition.y)
                && std::isfinite(clipPosition.z) && std::isfinite(clipPosition.w))
            {
                if (clipPosition.w > 0.0001f)
                {
                    const float inverseW = 1.0f / clipPosition.w;
                    sourceX = clipPosition.x * inverseW * 0.5f + 0.5f;
                    sourceY = 0.5f - clipPosition.y * inverseW * 0.5f;
                    sourceVisible = true;
                }
                else if (clipPosition.z > 0.0001f)
                {
                    const float screenDirectionLength = std::sqrt(
                        clipPosition.x * clipPosition.x + clipPosition.y * clipPosition.y);
                    if (screenDirectionLength > 0.0001f)
                    {
                        sourceX += clipPosition.x / screenDirectionLength * 0.75f;
                        sourceY -= clipPosition.y / screenDirectionLength * 0.75f;
                    }
                    sourceVisible = true;
                }
            }
            if (sourceVisible && std::isfinite(sourceX) && std::isfinite(sourceY))
            {
                result.lightShafts.options = Vector4(
                    m_lightShaftLight.lightShaftBloomEnabled ? 1.0f : 0.0f,
                    m_lightShaftLight.lightShaftOcclusionEnabled ? 1.0f : 0.0f,
                    m_lightShaftLight.lightShaftBloomScale,
                    m_lightShaftLight.lightShaftBloomThreshold);
                result.lightShafts.tint = Vector4(m_lightShaftLight.lightShaftBloomTint.x,
                    m_lightShaftLight.lightShaftBloomTint.y, m_lightShaftLight.lightShaftBloomTint.z,
                    m_lightShaftLight.lightShaftOcclusionMaskDarkness);
                result.lightShafts.screenPosition = Vector4(sourceX, sourceY, 1.0f, 0.0f);
            }
        }
        if (m_hasAtmosphere)
        {
            const float unitsPerKilometer = m_atmosphere.worldUnitsPerKilometer;
            result.atmosphere.options = Vector4(
                (m_atmosphereSuns[0].directionEnabled.w > 0.0f
                    || m_atmosphereSuns[1].directionEnabled.w > 0.0f) ? 1.0f : 0.0f,
                m_atmosphere.groundRadiusKm, m_atmosphere.atmosphereHeightKm, unitsPerKilometer);
            result.atmosphere.rayleigh = Vector4(m_atmosphere.rayleighScattering.x,
                m_atmosphere.rayleighScattering.y, m_atmosphere.rayleighScattering.z,
                m_atmosphere.rayleighScaleHeightKm);
            result.atmosphere.mie = Vector4(m_atmosphere.mieScattering, m_atmosphere.mieAbsorption,
                m_atmosphere.mieAnisotropy, m_atmosphere.mieScaleHeightKm);
            result.atmosphere.absorption = Vector4(m_atmosphere.ozoneAbsorption.x,
                m_atmosphere.ozoneAbsorption.y, m_atmosphere.ozoneAbsorption.z, m_atmosphere.absorptionScale);
            result.atmosphere.artistic = Vector4(m_atmosphere.aerialPerspectiveViewDistanceScale,
                m_atmosphere.sunDiskIntensity, m_atmosphere.sunAngularRadiusDegrees,
                m_atmosphere.sunLightIntensityScale);
            const Vector3 centerFromCamera = (m_atmosphereCenter - position) / unitsPerKilometer;
            result.atmosphere.planetCenterFromCamera = Vector4(centerFromCamera.x, centerFromCamera.y,
                centerFromCamera.z, 0);
            result.atmosphere.suns = m_atmosphereSuns;
        }
        if (m_shadowIndex < 0 || !hasCameraView)
            return result;
        const auto& light = result.lights[static_cast<std::size_t>(m_shadowIndex)];
        const Vector3 direction(light.directionInner.x, light.directionInner.y, light.directionInner.z);
        CascadedShadowData cascades;
        if (!buildCascadedShadow(inverseUnjitteredViewProjection, unjitteredProjection, nearClip, farClip,
            m_shadow.shadowDistance, m_shadow.normalBias, direction, cascades))
        {
            LOG_ERROR("[Shadow] Cannot construct cascades from the camera and light configuration.");
            return result;
        }
        result.shadowViewProjection = cascades.viewProjections;
        result.shadowSplits = Vector4(cascades.splits[0], cascades.splits[1], cascades.splits[2], SHADOW_CASCADE_BLEND);
        result.shadowCameraForward = Vector4(forward.x, forward.y, forward.z, nearClip);
        result.shadowTexelWorld = Vector4(cascades.texelWorld[0], cascades.texelWorld[1], cascades.texelWorld[2], 0.1f);
        result.shadowOptions = Vector4(1, m_shadow.shadowBias, m_shadow.normalBias,
            1.0f / static_cast<float>(SHADOW_CASCADE_SIZE));
        return result;
    }
}
