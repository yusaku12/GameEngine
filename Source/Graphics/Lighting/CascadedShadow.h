#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "Core\Math\Geometry.h"

namespace Engine
{
    inline constexpr std::size_t SHADOW_CASCADE_COUNT = 3;
    inline constexpr std::uint32_t SHADOW_CASCADE_SIZE = 1024;
    inline constexpr float SHADOW_CASCADE_BLEND = 0.1f;

    /** @brief Camera-relative directional shadow receiver volumes. */
    struct CascadedShadowData
    {
        std::array<Matrix, SHADOW_CASCADE_COUNT> viewProjections{};
        std::array<float, SHADOW_CASCADE_COUNT> splits{};
        std::array<float, SHADOW_CASCADE_COUNT> texelWorld{};
    };

    /** @brief Accumulates caster depth around an existing cascade receiver volume. */
    struct ShadowDepthRange
    {
        double minimum = 0.0;
        double maximum = 1.0;

        /**
         * @brief Includes the projected extent of a world-space caster.
         * @param viewProjection Initial cascade projection.
         * @param bounds Conservative world-space caster bounds.
         * @return True when the projected bounds are finite and valid.
         */
        bool include(const Matrix& viewProjection, const AABB& bounds) noexcept;

        /**
         * @brief Expands a cascade's depth range while preserving its XY projection.
         * @param viewProjection Initial projection, replaced only on success.
         * @param biasScale Output factor converting the original normalized depth bias.
         * @return True if the adjusted projection is finite.
         */
        bool apply(Matrix& viewProjection, float& biasScale) const noexcept;
    };

    /**
     * @brief Builds stable, overlapping receiver volumes for perspective or orthographic cameras.
     * @param inverseViewProjection Inverse unjittered camera view-projection matrix.
     * @param projection Unjittered camera projection matrix.
     * @param nearClip Camera near plane in view-space units.
     * @param farClip Camera far plane in view-space units.
     * @param shadowDistance Maximum directional shadow distance.
     * @param normalBias First cascade's world-space receiver normal bias.
     * @param lightDirection Normalized direction of light travel.
     * @param result Output, changed only on success.
     * @return True if all cascade volumes could be constructed.
     * @thread_safety Thread-safe for independent output values.
     */
    bool buildCascadedShadow(const Matrix& inverseViewProjection, const Matrix& projection,
        float nearClip, float farClip, float shadowDistance, float normalBias, const Vector3& lightDirection,
        CascadedShadowData& result) noexcept;
}
