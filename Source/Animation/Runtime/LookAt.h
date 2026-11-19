#pragma once

#include <span>
#include "Core\Math\MathTypes.h"

namespace ozz::animation { class Skeleton; }
namespace ozz::math { struct SoaTransform; struct Float4x4; }

namespace Engine
{
	/** @brief Model-space aim target and joint-local axes for a child-to-parent joint chain. */
	struct LookAtTarget
	{
		Vector3 position = Vector3::Zero;
		Vector3 forward = Vector3::UnitZ;
		Vector3 up = Vector3::UnitY;
		Vector3 offset = Vector3::Zero;
		Vector3 pole = Vector3::UnitY;
		float weight = 1.0f;
		float jointWeight = 0.5f;
	};

	/**
	 * @brief Apply an ozz aim chain to scratch pose buffers.
	 * @param chain Joint indices ordered from the aimed joint to its ancestor.
	 * @return False with a diagnostic if the chain or pose cannot be solved.
	 * @thread_safety Exclusive access to the supplied buffers is required.
	 */
	bool solveLookAt(const ozz::animation::Skeleton& skeleton, std::span<const int> chain,
		const LookAtTarget& target, std::span<ozz::math::SoaTransform> locals,
		std::span<ozz::math::Float4x4> models);
}
