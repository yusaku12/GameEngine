#pragma once

#include "Core\Math\MathFunction.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{

//! 軸平行境界ボックス
using AABB = DirectX::BoundingBox;

//! 有向境界ボックス
using OrientedBox = DirectX::BoundingOrientedBox;

//! 境界球
using BoundingSphere = DirectX::BoundingSphere;

//! 視錐台
using Frustum = DirectX::BoundingFrustum;

//! 包含判定の結果
using ContainmentType = DirectX::ContainmentType;

/**
 * @brief 2点を含む最小の軸平行境界ボックスを作る
 * @param minimumPoint 最小側の点
 * @param maximumPoint 最大側の点
 * @return AABB 境界ボックス
 */
inline AABB makeAABB(const Vector3& minimumPoint, const Vector3& maximumPoint)
{
    AABB result;
    AABB::CreateFromPoints(result, minimumPoint, maximumPoint);
    return result;
}

/**
 * @brief 点群を含む最小の軸平行境界ボックスを作る
 * @param points 点群の先頭
 * @param count 点の数
 * @return AABB 境界ボックス
 */
inline AABB makeAABB(const Vector3* points, size_t count)
{
    AABB result;
    if (points != nullptr && count > 0)
        AABB::CreateFromPoints(result, count, points, sizeof(Vector3));

    return result;
}

/**
 * @brief 境界ボックスの最小側の点を取得する
 */
inline Vector3 minimumPointOf(const AABB& box)
{
    return Vector3(box.Center) - Vector3(box.Extents);
}

/**
 * @brief 境界ボックスの最大側の点を取得する
 */
inline Vector3 maximumPointOf(const AABB& box)
{
    return Vector3(box.Center) + Vector3(box.Extents);
}

/**
 * @brief 点と線分の最近接点を求める
 * @param point 対象の点
 * @param lineStart 線分の始点
 * @param lineEnd 線分の終点
 * @return Vector3 最近接点
 */
inline Vector3 closestPointOnSegment(const Vector3& point, const Vector3& lineStart, const Vector3& lineEnd)
{
    const Vector3 segment = lineEnd - lineStart;
    const float lengthSquared = segment.LengthSquared();

    if (lengthSquared <= EPSILON)
        return lineStart;

    const float t = saturate((point - lineStart).Dot(segment) / lengthSquared);
    return lineStart + segment * t;
}

/**
 * @brief 点と平面の符号付き距離を求める
 * @param point 対象の点
 * @param plane 対象の平面
 * @return float 符号付き距離
 */
inline float signedDistanceToPlane(const Vector3& point, const Plane& plane)
{
    return plane.DotCoordinate(point);
}

/**
 * @brief 視錐台に境界ボックスが交差するかを判定する
 * @param frustum 視錐台
 * @param box 境界ボックス
 * @return bool 交差または内包していればtrue
 */
inline bool isVisible(const Frustum& frustum, const AABB& box)
{
    return frustum.Contains(box) != DirectX::DISJOINT;
}

/**
 * @brief 視錐台に境界球が交差するかを判定する
 * @param frustum 視錐台
 * @param sphere 境界球
 * @return bool 交差または内包していればtrue
 */
inline bool isVisible(const Frustum& frustum, const BoundingSphere& sphere)
{
    return frustum.Contains(sphere) != DirectX::DISJOINT;
}

} // namespace Engine
