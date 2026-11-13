#pragma once
#include <array>
#include "Components\Rendering\LightComponent.h"
#include "Components\Rendering\SkyAtmosphereComponent.h"
#include "Components\Rendering\VolumetricCloudComponent.h"

namespace Engine
{
    class Scene;
    /** @brief GPU light layout shared with Model.hlsl. */
    struct GpuLight
    {
        Vector4 positionKind;
        Vector4 colorRange;
        Vector4 directionInner;
        Vector4 parameters;
    };
    /** @brief Directional light data consumed by the atmosphere integrator. */
    struct GpuAtmosphereSun
    {
        Vector4 directionEnabled;
        Vector4 radiance;
    };
    /** @brief Packed, validated atmosphere inputs shared with Deferred.hlsl. */
    struct GpuSkyAtmosphere
    {
        Vector4 options;
        Vector4 rayleigh;
        Vector4 mie;
        Vector4 absorption;
        Vector4 artistic;
        Vector4 planetCenterFromCamera;
        std::array<GpuAtmosphereSun, 2> suns{};
    };
    /** @brief Camera-relative cloud inputs shared with Deferred.hlsl. */
    struct GpuVolumetricCloud
    {
        Vector4 options;
        Vector4 shape;
        Vector4 cameraFromAnchor;
    };
    /** @brief Immutable frame-barrier snapshot; bounded to 32 direct lights and one directional shadow. */
    struct LightingConstants
    {
        Matrix shadowViewProjection = Matrix::Identity;
        Vector4 cameraPosition;
        Vector4 shadowOptions;
        Vector4 environment = Vector4(0.15f, 0.15f, 0.15f, 0);
        std::array<GpuLight, 32> lights{};
        GpuSkyAtmosphere atmosphere;
        GpuVolumetricCloud cloud;
    };
    static_assert(sizeof(GpuLight) == sizeof(float) * 16);
    static_assert(sizeof(GpuAtmosphereSun) == sizeof(float) * 8);
    static_assert(sizeof(GpuSkyAtmosphere) == sizeof(float) * 40);
    static_assert(sizeof(GpuVolumetricCloud) == sizeof(float) * 12);
    static_assert(sizeof(LightingConstants) == sizeof(float) * (16 + 12 + 32 * 16 + 40 + 12));
    /**
     * @brief Builds lighting from active and persistent scenes independently of gameplay lifecycle.
     * @thread_safety Not thread-safe. Capture on GameUpdate, read only after joining that job.
     */
    class LightingFrame
    {
    public:
        /** @brief Returns the process-wide frame snapshot. */
        static LightingFrame& instance() noexcept;
        /** @brief Captures active lights and their world transforms without allocating. */
        void capture(const Scene* active, const Scene& persistent) noexcept;
        /** @brief Builds camera-relative, texel-stabilized directional shadow constants. */
        LightingConstants forCamera(const Vector3& position, const Vector3& forward) const noexcept;
        /** @brief True when a directional shadow is configured. */
        bool hasShadow() const noexcept { return m_shadowIndex >= 0; }
    private:
        LightingConstants m_constants;
        LightSettings m_shadow;
        SkyAtmosphereSettings m_atmosphere;
        VolumetricCloudSettings m_cloud;
        Vector3 m_atmosphereCenter = Vector3::Zero;
        Vector3 m_cloudAnchor = Vector3::Zero;
        std::array<GpuAtmosphereSun, 2> m_atmosphereSuns{};
        int m_shadowIndex = -1;
        bool m_hasAtmosphere = false;
        bool m_hasCloud = false;
        bool m_duplicateAtmosphereReported = false;
        bool m_duplicateCloudReported = false;
        bool m_duplicateSunReported = false;
    };
}
