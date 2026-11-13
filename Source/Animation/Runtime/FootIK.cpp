#include "Pch.h"
#include "Animation\Runtime\FootIK.h"
#include <cmath>
#include <limits>
#include <vector>
#include <ozz/animation/runtime/ik_aim_job.h>
#include <ozz/animation/runtime/ik_two_bone_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/base/maths/simd_quaternion.h>
#include <ozz/base/span.h>
#include "Core\Logging\Logger.h"

namespace Engine
{
    namespace
    {
        bool finite(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool unit(const Vector3& value) noexcept
        {
            return finite(value) && std::abs(value.LengthSquared() - 1.0f) < 1.0e-3f;
        }

        ozz::math::SimdFloat4 simd(const Vector3& value) noexcept
        {
            return ozz::math::simd_float4::Load(value.x, value.y, value.z, 0.0f);
        }

        Vector3 vector(const ozz::math::SimdFloat4 value) noexcept
        {
            return { ozz::math::GetX(value), ozz::math::GetY(value), ozz::math::GetZ(value) };
        }

        bool normalize(Vector3& value) noexcept
        {
            const float lengthSquared = value.LengthSquared();
            if (!finite(value) || !std::isfinite(lengthSquared) || lengthSquared <= 0.0f)
                return false;
            value /= std::sqrt(lengthSquared);
            return true;
        }

        void multiplyRotation(const int joint, const ozz::math::SimdQuaternion correction,
            const std::span<ozz::math::SoaTransform> locals)
        {
            auto& rotation = locals[static_cast<std::size_t>(joint / 4)].rotation;
            ozz::math::SimdQuaternion rotations[4];
            ozz::math::Transpose4x4(&rotation.x, &rotations[0].xyzw);
            rotations[joint % 4] = rotations[joint % 4] * correction;
            ozz::math::Transpose4x4(&rotations[0].xyzw, &rotation.x);
        }

        bool ancestor(const ozz::animation::Skeleton& skeleton, const int parent, int child)
        {
            const auto parents = skeleton.joint_parents();
            while (child >= 0)
            {
                child = parents[static_cast<std::size_t>(child)];
                if (child == parent)
                    return true;
            }
            return false;
        }
    }

    bool validateFootIKTargets(const ozz::animation::Skeleton& skeleton,
        const std::span<const FootIKTarget> targets)
    {
        for (std::size_t index = 0; index < targets.size(); ++index)
        {
            const auto& target = targets[index];
            if (target.hip < 0 || target.knee < 0 || target.ankle < 0
                || target.hip >= skeleton.num_joints() || target.knee >= skeleton.num_joints()
                || target.ankle >= skeleton.num_joints()
                || !ancestor(skeleton, target.hip, target.knee)
                || !ancestor(skeleton, target.knee, target.ankle)
                || !unit(target.kneeAxis) || !unit(target.poleAxis)
                || !unit(target.footUp) || !unit(target.footForward)
                || std::abs(target.footUp.Dot(target.footForward)) > 1.0e-3f
                || !finite(target.position) || !unit(target.normal)
                || !std::isfinite(target.weight) || target.weight < 0.0f || target.weight > 1.0f
                || !std::isfinite(target.soften) || target.soften < 0.0f || target.soften > 1.0f)
            {
                LOG_ERROR("[FootIK] Invalid leg chain, axes, target, weight or soften at index {}.", index);
                return false;
            }
            for (std::size_t previous = 0; previous < index; ++previous)
            {
                const int other = targets[previous].hip;
                if (other == target.hip || ancestor(skeleton, other, target.hip)
                    || ancestor(skeleton, target.hip, other))
                {
                    LOG_ERROR("[FootIK] Leg chains must have independent hip subtrees.");
                    return false;
                }
            }
        }
        return true;
    }

    bool configureFootIKAxesFromRestPose(const ozz::animation::Skeleton& skeleton,
        const std::span<FootIKTarget> targets)
    {
        if (targets.empty() || !validateFootIKTargets(skeleton, targets))
        {
            LOG_ERROR("[FootIK] Select valid leg chains before configuring rest-pose axes.");
            return false;
        }
        std::vector<ozz::math::Float4x4> models(static_cast<std::size_t>(skeleton.num_joints()));
        ozz::animation::LocalToModelJob job;
        job.skeleton = &skeleton;
        job.input = skeleton.joint_rest_poses();
        job.output = ozz::span<ozz::math::Float4x4>(models.data(), models.size());
        if (!job.Run())
        {
            LOG_ERROR("[FootIK] Cannot build the skeleton rest pose.");
            return false;
        }
        std::vector<FootIKTarget> configured(targets.begin(), targets.end());
        for (auto& target : configured)
        {
            ozz::math::SimdInt4 kneeInvertible;
            ozz::math::SimdInt4 ankleInvertible;
            const auto kneeInverse = ozz::math::Invert(models[target.knee], &kneeInvertible);
            const auto ankleInverse = ozz::math::Invert(models[target.ankle], &ankleInvertible);
            if (!ozz::math::AreAllTrue1(kneeInvertible) || !ozz::math::AreAllTrue1(ankleInvertible))
            {
                LOG_ERROR("[FootIK] Rest-pose axis setup requires nonsingular leg transforms.");
                return false;
            }
            auto start = vector(ozz::math::TransformPoint(kneeInverse, models[target.hip].cols[3]));
            auto end = vector(ozz::math::TransformPoint(kneeInverse, models[target.ankle].cols[3]));
            auto direction = vector(models[target.ankle].cols[3] - models[target.hip].cols[3]);
            if (!normalize(start) || !normalize(end) || !normalize(direction))
            {
                LOG_ERROR("[FootIK] Cannot infer axes from a zero-length or folded rest-pose leg.");
                return false;
            }
            const auto mid = vector(models[target.knee].cols[3] - models[target.hip].cols[3]);
            auto pole = mid - direction * mid.Dot(direction);
            if (pole.LengthSquared() <= 1.0e-6f * mid.LengthSquared())
                pole = Vector3::UnitZ - direction * direction.z;
            target.poleAxis = vector(ozz::math::TransformVector(kneeInverse, simd(pole)));
            if (!normalize(target.poleAxis))
            {
                LOG_ERROR("[FootIK] Rest-pose knee bend is ambiguous; configure axes manually.");
                return false;
            }
            // This sign makes ozz's initial knee angle positive (positive rotation opens the knee).
            target.kneeAxis = start.Cross(end);
            if (target.kneeAxis.LengthSquared() <= 1.0e-6f)
                target.kneeAxis = target.poleAxis.Cross(start);
            target.footUp = vector(ozz::math::TransformVector(ankleInverse, simd(Vector3::UnitY)));
            target.footForward = vector(ozz::math::TransformVector(ankleInverse, simd(Vector3::UnitZ)));
            if (!normalize(target.kneeAxis) || !normalize(target.footUp))
            {
                LOG_ERROR("[FootIK] Rest-pose hinge or foot-up axis is invalid.");
                return false;
            }
            target.footForward -= target.footUp * target.footUp.Dot(target.footForward);
            if (!normalize(target.footForward))
            {
                LOG_ERROR("[FootIK] Rest-pose foot-forward axis is invalid.");
                return false;
            }
        }
        if (!validateFootIKTargets(skeleton, configured))
            return false;
        std::copy(configured.begin(), configured.end(), targets.begin());
        return true;
    }

    bool calculateFootIKGroundTarget(const Vector3& hitPoint, const Vector3& normal,
        const float footHeight, const float maxSlopeDegrees, Vector3& target) noexcept
    {
        if (!finite(hitPoint) || !unit(normal) || !std::isfinite(footHeight) || footHeight < 0.0f
            || !std::isfinite(maxSlopeDegrees) || maxSlopeDegrees < 0.0f || maxSlopeDegrees >= 90.0f)
        {
            LOG_ERROR("[FootIK] Ground target requires finite inputs, a unit normal and a slope below 90 degrees.");
            return false;
        }
        const float minimumUp = std::cos(maxSlopeDegrees * DirectX::XM_PI / 180.0f);
        if (normal.y <= 1.0e-4f || normal.y + 1.0e-6f < minimumUp)
            return false;
        // Keep the ankle on the ray; clearance is measured perpendicular to the ground plane.
        target = hitPoint + Vector3::UnitY * (footHeight / normal.y);
        if (!finite(target))
        {
            LOG_ERROR("[FootIK] Ground target overflowed finite coordinates.");
            return false;
        }
        return true;
    }

    bool solveFootIK(const ozz::animation::Skeleton& skeleton,
        const std::span<const FootIKTarget> targets, const Vector3& pelvisOffset,
        const std::span<ozz::math::SoaTransform> locals, const std::span<ozz::math::Float4x4> models,
        const Vector3& modelUp)
    {
        if (!finite(pelvisOffset) || !unit(modelUp)
            || locals.size() < static_cast<std::size_t>(skeleton.num_soa_joints())
            || models.size() < static_cast<std::size_t>(skeleton.num_joints())
            || !validateFootIKTargets(skeleton, targets))
        {
            LOG_ERROR("[FootIK] Cannot solve an invalid pose.");
            return false;
        }
        ozz::animation::LocalToModelJob localToModel;
        localToModel.skeleton = &skeleton;
        localToModel.input = ozz::span<const ozz::math::SoaTransform>(locals.data(), locals.size());
        localToModel.output = ozz::span<ozz::math::Float4x4>(models.data(), models.size());
        const auto rebuild = [&]()
        {
            if (!localToModel.Run())
            {
                LOG_ERROR("[FootIK] LocalToModelJob failed.");
                return false;
            }
            for (int joint = 0; joint < skeleton.num_joints(); ++joint)
                for (const auto column : models[static_cast<std::size_t>(joint)].cols)
                    if (!ozz::math::AreAllTrue(ozz::math::CmpLe(ozz::math::Abs(column),
                        ozz::math::simd_float4::Load1((std::numeric_limits<float>::max)()))))
                    {
                        LOG_ERROR("[FootIK] Pose contains a nonfinite joint transform.");
                        return false;
                    }
            return true;
        };
        const auto parents = skeleton.joint_parents();
        for (int joint = 0; joint < skeleton.num_joints(); ++joint)
        {
            if (parents[static_cast<std::size_t>(joint)] != ozz::animation::Skeleton::kNoParent)
                continue;
            auto& translation = locals[static_cast<std::size_t>(joint / 4)].translation;
            ozz::math::SimdFloat4 values[4];
            ozz::math::Transpose3x4(&translation.x, values);
            values[joint % 4] = values[joint % 4] + simd(pelvisOffset);
            ozz::math::Transpose4x3(values, &translation.x);
        }
        if (!rebuild())
            return false;
        for (const auto& target : targets)
        {
            if (target.weight == 0.0f)
                continue;
            for (const int joint : { target.hip, target.knee, target.ankle })
            {
                ozz::math::SimdInt4 invertible;
                static_cast<void>(ozz::math::Invert(models[joint], &invertible));
                if (!ozz::math::AreAllTrue1(invertible))
                {
                    LOG_ERROR("[FootIK] IK requires nonsingular leg joint transforms.");
                    return false;
                }
            }
            if (ozz::math::GetX(ozz::math::Length3Sqr(models[target.knee].cols[3] - models[target.hip].cols[3])) == 0.0f
                || ozz::math::GetX(ozz::math::Length3Sqr(models[target.ankle].cols[3] - models[target.knee].cols[3])) == 0.0f)
            {
                LOG_ERROR("[FootIK] IK requires two nonzero-length bones.");
                return false;
            }
            const auto originalForward = ozz::math::TransformVector(models[target.ankle], simd(target.footForward));
            const auto originalUp = ozz::math::TransformVector(models[target.ankle], simd(target.footUp));
            ozz::animation::IKTwoBoneJob leg;
            leg.target = simd(target.position);
            leg.mid_axis = simd(target.kneeAxis);
            leg.pole_vector = ozz::math::TransformVector(models[target.knee], simd(target.poleAxis));
            leg.weight = target.weight;
            leg.soften = target.soften;
            leg.start_joint = &models[target.hip];
            leg.mid_joint = &models[target.knee];
            leg.end_joint = &models[target.ankle];
            ozz::math::SimdQuaternion hipCorrection;
            ozz::math::SimdQuaternion kneeCorrection;
            leg.start_joint_correction = &hipCorrection;
            leg.mid_joint_correction = &kneeCorrection;
            if (!leg.Run())
            {
                LOG_ERROR("[FootIK] IKTwoBoneJob failed.");
                return false;
            }
            multiplyRotation(target.hip, hipCorrection, locals);
            multiplyRotation(target.knee, kneeCorrection, locals);
            if (!rebuild())
                return false;
            if (!target.alignFoot)
                continue;
            ozz::animation::IKAimJob ankle;
            ankle.joint = &models[target.ankle];
            ankle.target = models[target.ankle].cols[3] + simd(target.normal);
            ankle.forward = simd(target.footUp);
            ankle.up = simd(target.footForward);
            const auto normal = simd(target.normal);
            // Recover upright heading before tilting to ground; direct slope alignment can become antiparallel.
            const auto upright = simd(modelUp);
            const auto recoverHeading = ozz::math::SimdQuaternion::FromVectors(originalUp, upright);
            const auto tiltToGround = ozz::math::SimdQuaternion::FromUnitVectors(upright, normal);
            auto heading = vector(ozz::math::TransformVector(tiltToGround,
                ozz::math::TransformVector(recoverHeading, originalForward)));
            heading -= target.normal * target.normal.Dot(heading);
            if (!normalize(heading))
            {
                LOG_ERROR("[FootIK] Cannot recover a finite, nonzero foot heading.");
                return false;
            }
            ankle.pole_vector = simd(heading);
            ankle.weight = target.weight;
            ozz::math::SimdQuaternion ankleCorrection;
            ankle.joint_correction = &ankleCorrection;
            if (!ankle.Run())
            {
                LOG_ERROR("[FootIK] IKAimJob failed.");
                return false;
            }
            multiplyRotation(target.ankle, ankleCorrection, locals);
            if (!rebuild())
                return false;
        }
        return true;
    }
}
