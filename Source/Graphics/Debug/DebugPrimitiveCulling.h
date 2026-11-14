#pragma once

#include "Core\Math\Geometry.h"

namespace Engine::DebugPrimitiveCulling
{
    inline bool isFiniteBounds(const AABB& bounds) noexcept
    {
        return std::isfinite(bounds.Center.x) && std::isfinite(bounds.Center.y)
            && std::isfinite(bounds.Center.z) && std::isfinite(bounds.Extents.x)
            && std::isfinite(bounds.Extents.y) && std::isfinite(bounds.Extents.z);
    }

    inline bool isVisibleLine(const CameraFrustum& frustum, const Vector3& start, const Vector3& end) noexcept
    {
        const AABB bounds = makeAABB(start, end);
        return !isFiniteBounds(bounds) || Engine::isVisible(frustum, bounds);
    }

    inline bool isVisibleBox(
        const CameraFrustum& frustum, const Matrix& world, const Vector3& extents) noexcept
    {
        if (world._14 != 0.0f || world._24 != 0.0f || world._34 != 0.0f || world._44 != 1.0f)
            return true;

        const AABB local(Vector3::Zero, extents);
        if (!isFiniteBounds(local))
            return true;
        AABB bounds;
        local.Transform(bounds, world);
        return !isFiniteBounds(bounds) || Engine::isVisible(frustum, bounds);
    }
}
