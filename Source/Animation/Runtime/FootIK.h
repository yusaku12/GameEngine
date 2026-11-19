#pragma once

#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/soa_transform.h>
#include "Core\Math\MathTypes.h"

namespace Engine
{
	/*
	* @brief FootIKTarget構造体は、足の逆運動学（Inverse Kinematics）を解決するためのターゲット情報を保持します。
	* この構造体には、ヒップ、膝、足首のジョイントインデックス、膝の軸、ポール軸、足の上方向、前方向、位置、法線ベクトル、重み、ソフト化パラメータ、足の整列フラグ、およびオプションでポールジョイントや最小位置が含まれています。
	* これにより、キャラクターの足の動きを自然に制御することができます。
	*/
	struct FootIKTarget
	{
		int hip = -1;                           //!< ヒップジョイントのインデックス。-1の場合、ヒップジョイントは無効です。
		int knee = -1;                          //!< 膝ジョイントのインデックス。-1の場合、膝ジョイントは無効です。
		int ankle = -1;                         //!< 足首ジョイントのインデックス。-1の場合、足首ジョイントは無効です。
		Vector3 kneeAxis = -Vector3::UnitX;     //!< 膝の軸ベクトル。膝の回転方向を定義します。
		Vector3 poleAxis = Vector3::UnitZ;      //!< ポール軸ベクトル。膝の方向を制御するために使用されます。
		Vector3 footUp = Vector3::UnitY; 	    //!< 足の上方向ベクトル。足の向きを制御するために使用されます。
		Vector3 footForward = Vector3::UnitZ;   //!< 足の前方向ベクトル。足の向きを制御するために使用されます。
		Vector3 position = Vector3::Zero; 	    //!< 足のターゲット位置。足が到達すべき目標位置を指定します。
		Vector3 normal = Vector3::UnitY;        //!< 足のターゲット法線ベクトル。足が接地する面の法線を指定します。
		float weight = 1.0f;                    //!< 足のIKの影響度を示す重み。0.0fの場合、IKは無効で、1.0fの場合、完全に適用されます。
		float soften = 1.0f;                    //!< 足のIKのソフト化パラメータ。値が大きいほど、足の動きが滑らかになります。
		bool alignFoot = true;                  //!< 足の整列フラグ。trueの場合、足はターゲット法線に整列します。
		std::optional<int> poleJoint;           //!< ポールジョイントのインデックス。オプションで、膝の方向を制御するために使用されます。存在しない場合は、膝の方向は自動的に計算されます。
		std::optional<Vector3> minimumPosition; //!< 足の最小位置。オプションで、足が到達できる最小位置を指定します。存在しない場合は、足の位置は制限されません。
		float minimumWeight = 1.0f;             //!< 足の最小位置の影響度を示す重み。0.0fの場合、最小位置は無効で、1.0fの場合、完全に適用されます。
	};

	/*
	* @brief validateFootIKTargets関数は、指定されたスケルトンと足のIKターゲットの配列を検証します。
	* @param skeleton 検証対象のスケルトン。
	* @param targets 検証対象の足のIKターゲットの配列。
	* @return trueの場合、すべてのターゲットが有効であり、falseの場合、少なくとも1つのターゲットが無効です。
	*/
	bool validateFootIKTargets(const ozz::animation::Skeleton& skeleton, std::span<const FootIKTarget> targets);

	/*
	* @brief configureFootIKAxesFromRestPose関数は、指定されたスケルトンと足のIKターゲットの配列を使用して、足のIK軸を休止姿勢から構成します。
	* @param skeleton 構成対象のスケルトン。
	* @param targets 構成対象の足のIKターゲットの配列。
	* @return trueの場合、足のIK軸は休止姿勢から構成され、falseの場合、構成に失敗します。
	*/
	bool configureFootIKAxesFromRestPose(const ozz::animation::Skeleton& skeleton, std::span<FootIKTarget> targets);

	/*
	* @brief anchorFootIKPolesToHipParents関数は、指定されたスケルトンと足のIKターゲットの配列を使用して、ポールジョイントをヒップの親に固定します。
	* @param skeleton 構成対象のスケルトン。
	* @param targets 構成対象の足のIKターゲットの配列。
	* @return trueの場合、ポールジョイントはヒップの親に固定され、falseの場合、固定に失敗します。
	*/
	bool anchorFootIKPolesToHipParents(const ozz::animation::Skeleton& skeleton, std::span<FootIKTarget> targets);

	/*
	* @brief solveFootIK関数は、指定されたスケルトンと足のIKターゲットの配列を使用して、足のIKを解決します。
	* @param skeleton 解決対象のスケルトン。
	* @param targets 解決対象の足のIKターゲットの配列。
	* @param pelvisOffset 骨盤のオフセット。
	* @param locals ローカル変換の配列。
	* @param models モデル変換の配列。
	* @param modelUp モデルの上方向ベクトル。
	* @return trueの場合、足のIKは正常に解決され、falseの場合、解決に失敗します。
	*/
	bool solveFootIK(const ozz::animation::Skeleton& skeleton, std::span<const FootIKTarget> targets,
		const Vector3& pelvisOffset, std::span<ozz::math::SoaTransform> locals,
		std::span<ozz::math::Float4x4> models, const Vector3& modelUp = Vector3::UnitY);

	/*
	* @brief calculateFootIKGroundTarget関数は、指定されたヒットポイントと法線を使用して、足のIKの地面ターゲットを計算します。
	* @param hitPoint ヒットポイント。
	* @param normal 法線ベクトル。
	* @param footHeight 足の高さ。
	* @param maxSlopeDegrees 最大傾斜角度（度）。
	* @param target 計算結果のターゲット位置。
	* @return trueの場合、地面ターゲットは正常に計算され、falseの場合、計算に失敗します。
	*/
	bool calculateFootIKGroundTarget(const Vector3& hitPoint, const Vector3& normal,
		float footHeight, float maxSlopeDegrees, Vector3& target) noexcept;

	/*
	* @brief calculateFootIKContactWeight関数は、指定された足首位置と地面ターゲットを使用して、足のIKの接触重みを計算します。
	* @param ankle 足首位置。
	* @param groundTarget 地面ターゲット位置。
	* @param animatedUp アニメーションの上方向ベクトル。
	* @param groundNormal 地面の法線ベクトル。
	* @param fadeDistance フェード距離。
	* @param weight 計算結果の重み。
	* @return trueの場合、接触重みは正常に計算され、falseの場合、計算に失敗します。
	*/
	bool calculateFootIKContactWeight(const Vector3& ankle, const Vector3& groundTarget,
		const Vector3& animatedUp, const Vector3& groundNormal, float fadeDistance, float& weight) noexcept;
}
