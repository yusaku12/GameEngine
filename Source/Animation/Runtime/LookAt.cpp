#include "Pch.h"
#include "Animation\Runtime\LookAt.h"
#include <ozz/animation/runtime/ik_aim_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_quaternion.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/span.h>
#include "Core\Logging\Logger.h"

namespace Engine
{
	namespace
	{
		bool finite(const Vector3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		ozz::math::SimdFloat4 simd(const Vector3& v)
		{
			return ozz::math::simd_float4::Load(v.x, v.y, v.z, 0.0f);
		}

		bool finiteMatrix(const ozz::math::Float4x4& matrix)
		{
			for (const auto column : matrix.cols)
				if (!ozz::math::AreAllTrue(ozz::math::CmpLe(ozz::math::Abs(column),
					ozz::math::simd_float4::Load1((std::numeric_limits<float>::max)()))))
					return false;
			return true;
		}
	}

	bool solveLookAt(const ozz::animation::Skeleton& skeleton, const std::span<const int> chain,
		const LookAtTarget& target, const std::span<ozz::math::SoaTransform> locals,
		const std::span<ozz::math::Float4x4> models)
	{
		const auto unit = [](const Vector3& v)
			{ return finite(v) && std::abs(v.LengthSquared() - 1.0f) < 1.0e-3f; };
		if (chain.empty() || locals.size() < static_cast<std::size_t>(skeleton.num_soa_joints())
			|| models.size() < static_cast<std::size_t>(skeleton.num_joints())
			|| !finite(target.position) || !finite(target.offset) || !unit(target.forward)
			|| !unit(target.up) || !unit(target.pole)
			|| std::abs(target.forward.Dot(target.up)) > 1.0e-3f
			|| !std::isfinite(target.weight) || target.weight < 0.0f || target.weight > 1.0f
			|| !std::isfinite(target.jointWeight) || target.jointWeight < 0.0f || target.jointWeight > 1.0f)
		{
			LOG_ERROR("[LookAt] Invalid chain, axes, target, weight or pose buffers.");
			return false;
		}
		const auto parents = skeleton.joint_parents();
		for (std::size_t i = 0; i < chain.size(); ++i)
		{
			if (chain[i] < 0 || chain[i] >= skeleton.num_joints()
				|| (i > 0 && parents[static_cast<std::size_t>(chain[i - 1])] != chain[i])
				|| !finiteMatrix(models[static_cast<std::size_t>(chain[i])]))
			{
				LOG_ERROR("[LookAt] Joint chain must consist of finite, consecutive child-to-parent transforms.");
				return false;
			}
		}
		if (target.weight == 0.0f)
			return true;

		ozz::animation::IKAimJob job;
		job.target = simd(target.position);
		job.pole_vector = simd(target.pole);
		job.up = simd(target.up);
		job.forward = simd(target.forward);
		job.offset = simd(target.offset);
		ozz::math::SimdQuaternion correction;
		job.joint_correction = &correction;
		for (std::size_t i = 0; i < chain.size(); ++i)
		{
			const int joint = chain[i];
			job.joint = &models[static_cast<std::size_t>(joint)];
			ozz::math::SimdInt4 invertible;
			const auto inverse = ozz::math::Invert(*job.joint, &invertible);
			if (!ozz::math::AreAllTrue1(invertible) || !finiteMatrix(inverse))
			{
				LOG_ERROR("[LookAt] Joint transform is singular or nonfinite.");
				return false;
			}
			const auto localTarget = ozz::math::TransformPoint(inverse, job.target);
			const float distanceSquared = ozz::math::GetX(ozz::math::Length3Sqr(localTarget));
			if (!std::isfinite(distanceSquared) || distanceSquared <= 1.0e-12f)
			{
				LOG_WARNING("[LookAt] Target is too close to the joint.");
				return false;
			}
			if (i != 0)
			{
				const auto& previous = models[static_cast<std::size_t>(chain[i - 1])];
				job.forward = ozz::math::TransformVector(inverse, ozz::math::TransformVector(previous,
					ozz::math::TransformVector(correction, job.forward)));
				job.offset = ozz::math::TransformPoint(inverse, ozz::math::TransformPoint(previous,
					ozz::math::TransformVector(correction, job.offset)));
				const float lengthSquared = ozz::math::GetX(ozz::math::Length3Sqr(job.forward));
				if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f)
				{
					LOG_ERROR("[LookAt] Forward direction collapsed.");
					return false;
				}
				job.forward = job.forward * ozz::math::simd_float4::Load1(1.0f / std::sqrt(lengthSquared));
			}
			job.weight = target.weight * (i + 1 == chain.size() ? 1.0f : target.jointWeight);
			bool reached = false;
			job.reached = &reached;
			if (!job.Run() || !reached)
			{
				LOG_WARNING("[LookAt] Target is too close to the aim offset or the aim job failed.");
				return false;
			}
			if (!ozz::math::AreAllTrue(ozz::math::CmpLe(ozz::math::Abs(correction.xyzw),
				ozz::math::simd_float4::Load1((std::numeric_limits<float>::max)()))))
			{
				LOG_ERROR("[LookAt] Aim produced a nonfinite correction.");
				return false;
			}
			auto& rotation = locals[static_cast<std::size_t>(joint / 4)].rotation;
			ozz::math::SimdQuaternion rotations[4];
			ozz::math::Transpose4x4(&rotation.x, &rotations[0].xyzw);
			rotations[joint % 4] = rotations[joint % 4] * correction;
			ozz::math::Transpose4x4(&rotations[0].xyzw, &rotation.x);
		}
		ozz::animation::LocalToModelJob rebuild;
		rebuild.skeleton = &skeleton;
		rebuild.input = ozz::span<const ozz::math::SoaTransform>(locals.data(), locals.size());
		rebuild.output = ozz::span<ozz::math::Float4x4>(models.data(), models.size());
		if (!rebuild.Run())
		{
			LOG_ERROR("[LookAt] LocalToModelJob failed.");
			return false;
		}
		for (int joint = 0; joint < skeleton.num_joints(); ++joint)
			if (!finiteMatrix(models[static_cast<std::size_t>(joint)]))
			{
				LOG_ERROR("[LookAt] Aim produced a nonfinite pose.");
				return false;
			}
		return true;
	}
}