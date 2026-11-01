#pragma once
#include <array>
#include "Core\GameObject\Component\LightComponent.h"

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
    /** @brief Immutable frame-barrier snapshot; bounded to 32 direct lights and one directional shadow. */
    struct LightingConstants
    {
        Matrix shadowViewProjection = Matrix::Identity;
        Vector4 cameraPosition;
        Vector4 shadowOptions;
        Vector4 environment = Vector4(0.15f, 0.15f, 0.15f, 0);
        std::array<GpuLight, 32> lights{};
    };
    static_assert(sizeof(GpuLight) == sizeof(float) * 16);
    static_assert(sizeof(LightingConstants) == sizeof(float) * (16 + 12 + 32 * 16));
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
        int m_shadowIndex = -1;
    };
}
