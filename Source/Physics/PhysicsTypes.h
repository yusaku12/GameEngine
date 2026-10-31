#pragma once

#include <memory>
#include "Core\Math\MathTypes.h"
#include "Physics\PhysicsMeshAsset.h"

namespace Engine
{
    /**
     * @brief 初期物理バックエンドがサポートするプリミティブコライダー形状。
     */
    enum class PhysicsShapeType
    {
        Box,
        Sphere,
        Capsule,
        TriangleMesh,
        ConvexMesh
    };

    /**
     * @brief プリミティブコライダーの寸法をエンジン側で表す。
     * @details ボックス形状は半径、球形状はx成分を半径、カプセル形状はx成分を半径、y成分を半高として扱う。
     */
    struct PhysicsShapeGeometry
    {
        PhysicsShapeType type = PhysicsShapeType::Box;
        Vector3 dimensions = Vector3::One;
        std::shared_ptr<const PhysicsMeshAsset> meshAsset;
    };
} // namespace Engine
