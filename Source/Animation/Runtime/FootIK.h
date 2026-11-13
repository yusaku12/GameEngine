#pragma once

#include <span>
#include <optional>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/soa_transform.h>
#include "Core\Math\MathTypes.h"

namespace Engine
{
    /** @brief Resolved rig axes are in joint-local space; targets are in skeleton-model space. */
    struct FootIKTarget
    {
        int hip = -1;
        int knee = -1;
        int ankle = -1;
        Vector3 kneeAxis = -Vector3::UnitX;
        Vector3 poleAxis = Vector3::UnitZ;
        Vector3 footUp = Vector3::UnitY;
        Vector3 footForward = Vector3::UnitZ;
        Vector3 position = Vector3::Zero;
        Vector3 normal = Vector3::UnitY;
        float weight = 1.0f;
        float soften = 1.0f;
        bool alignFoot = true;
        /** @brief Pole anchor; unset uses the knee, kNoParent uses skeleton-model space. */
        std::optional<int> poleJoint;
    };

    /** @brief Validate finite targets, orthogonal foot axes and non-overlapping ancestor chains. */
    bool validateFootIKTargets(const ozz::animation::Skeleton& skeleton, std::span<const FootIKTarget> targets);

    /**
     * @brief Derive joint-local axes from a Y-up, +Z-forward skeleton rest pose.
     * @details Preserve the rest knee bend; straight legs use +Z as the preferred bend direction.
     * @return False with a diagnostic, leaving all targets unchanged. Only rig axes are replaced on success.
     * @thread_safety Independent target buffers can be processed concurrently.
     */
    bool configureFootIKAxesFromRestPose(const ozz::animation::Skeleton& skeleton,
        std::span<FootIKTarget> targets);

    /**
     * @brief Preserve rest-pose pole directions in hip-parent space, independent of animated knee flexion.
     * @return False with a diagnostic, leaving all targets unchanged.
     * @thread_safety Independent target buffers can be processed concurrently.
     */
    bool anchorFootIKPolesToHipParents(const ozz::animation::Skeleton& skeleton,
        std::span<FootIKTarget> targets);

    /**
     * @brief Apply ozz two-bone/aim corrections and a model-space visual root offset.
     * @details Inputs must be a fresh, uncorrected pose. All descendants are recomputed with LocalToModelJob.
     * @param modelUp Unit world-up direction expressed in skeleton-model space.
     * @return False with a diagnostic on invalid input or failed jobs. Caller must not publish failed output.
     * @thread_safety Independent buffers can be processed concurrently.
     */
    bool solveFootIK(const ozz::animation::Skeleton& skeleton, std::span<const FootIKTarget> targets,
        const Vector3& pelvisOffset, std::span<ozz::math::SoaTransform> locals,
        std::span<ozz::math::Float4x4> models, const Vector3& modelUp = Vector3::UnitY);

    /**
     * @brief Find the ankle on its vertical ray at the requested perpendicular sole clearance.
     * @return False for invalid input or a surface beyond the maximum slope.
     */
    bool calculateFootIKGroundTarget(const Vector3& hitPoint, const Vector3& normal,
        float footHeight, float maxSlopeDegrees, Vector3& target) noexcept;
}
