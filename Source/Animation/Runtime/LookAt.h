#pragma once

#include <span>
#include "Core\Math\MathTypes.h"

namespace ozz::animation { class Skeleton; }
namespace ozz::math { struct SoaTransform; struct Float4x4; }

namespace Engine
{
	/**
	 * @brief LookAtのターゲット情報。SolverはChainの先端JointをForward方向に向けるようにJointを回転させる。
	 */
	struct LookAtTarget
	{
		Vector3 position = Vector3::Zero; //!< LookAtのターゲット位置
		Vector3 forward = Vector3::UnitZ; //!< LookAtのターゲット方向。Chainの先端Jointがこの方向を向くように回転する。
		Vector3 up = Vector3::UnitY;      //!< LookAtのターゲット上方向。Chainの先端Jointがこの方向を上にするように回転する。
		Vector3 offset = Vector3::Zero;   //!< LookAtのターゲット位置に対するオフセット。Chainの先端Jointがこの位置を向くように回転する。
		Vector3 pole = Vector3::UnitY; 	  //!< LookAtのターゲット上方向に対するポールベクトル。Chainの先端Jointがこの方向を上にするように回転する。
		float weight = 1.0f;              //!< LookAtの影響度。0.0fで影響なし、1.0fで完全にLookAtする。
		float jointWeight = 0.5f;         //!< LookAtの影響度をChainの各Jointに分配する際の比率。0.0fで先端Jointのみ、1.0fで全てのJointが均等に影響を受ける。
	};

	/*
	* @brief LookAtのSolver。Chainの先端JointをForward方向に向けるようにJointを回転させる。
	* @param skeleton Skeleton。ChainのJointの親子関係を取得するために使用する。
	* @param chain LookAtの影響を受けるJointのインデックスの配列。Chainの先端JointがForward方向に向くように回転する。
	* @param target LookAtのターゲット情報。Chainの先端Jointがこの方向を向くように回転する。
	* @param locals ChainのJointのローカル変換行列の配列。Solverはこの配列を更新する。
	* @param models ChainのJointのモデル変換行列の配列。Solverはこの配列を更新する。
	*/
	bool solveLookAt(const ozz::animation::Skeleton& skeleton, std::span<const int> chain,
		const LookAtTarget& target, std::span<ozz::math::SoaTransform> locals,
		std::span<ozz::math::Float4x4> models);
}