#pragma once

#include "Core\Math\Transform.h"

#include <PxPhysicsAPI.h>

namespace Engine::PhysicsConversions
{
    /** @brief EngineのVector3をPhysXのベクトルに変換する。 */
    physx::PxVec3 toPx(const Vector3& value) noexcept;
    /** @brief PhysXのベクトルをEngineのVector3に変換する。 */
    Vector3 fromPx(const physx::PxVec3& value) noexcept;
    /** @brief EngineのQuaternionをPhysXのクォータニオンに変換する。 */
    physx::PxQuat toPx(const Quaternion& value) noexcept;
    /** @brief PhysXのクォータニオンをEngineのQuaternionに変換する。 */
    Quaternion fromPx(const physx::PxQuat& value) noexcept;
    /** @brief EngineのTransformをPhysXの姿勢に変換する。スケールは形状ジオメトリに適用する。 */
    physx::PxTransform toPxPose(const Transform& value) noexcept;
} // namespace Engine::PhysicsConversions
