#include "Pch.h"
#include "Physics\Internal\PhysXConversions.h"

namespace Engine::PhysicsConversions
{
    physx::PxVec3 toPx(const Vector3& value) noexcept
    {
        return { value.x, value.y, value.z };
    }

    Vector3 fromPx(const physx::PxVec3& value) noexcept
    {
        return { value.x, value.y, value.z };
    }

    physx::PxQuat toPx(const Quaternion& value) noexcept
    {
        return { value.x, value.y, value.z, value.w };
    }

    Quaternion fromPx(const physx::PxQuat& value) noexcept
    {
        return { value.x, value.y, value.z, value.w };
    }

    physx::PxTransform toPxPose(const Transform& value) noexcept
    {
        return { toPx(value.getPosition()), toPx(value.getRotation()) };
    }
} // namespace Engine::PhysicsConversions
