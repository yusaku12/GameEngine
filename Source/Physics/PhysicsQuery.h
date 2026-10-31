#pragma once

#include <cstdint>
#include <vector>

#include "Core\Math\MathTypes.h"
#include "Core\Object\ObjectGUID.h"
#include "Physics\PhysicsLayer.h"

namespace Engine
{
    class ColliderComponent;
    class GameObject;

    using GameObjectHandle = ObjectGUID;

    /**
     * @brief PhysicsWorldのシーン照会に適用するフィルター。
     */
    struct PhysicsQueryFilter
    {
        bool includeStatic = true;
        bool includeDynamic = true;
        bool includeTriggers = false;
        PhysicsLayerMask layerMask = ALL_PHYSICS_LAYERS;
        const GameObject* ignoredGameObject = nullptr; //!< 非所有参照。照会の実行中のみ使用する。
    };

    /**
     * @brief Colliderに対するレイキャストまたは形状スイープの結果。
     */
    struct RaycastHit
    {
        GameObjectHandle gameObject;
        Vector3 point = Vector3::Zero;
        Vector3 normal = Vector3::Zero;
        float distance = 0.0f;
        ColliderComponent* collider = nullptr; //!< 非所有参照。Colliderコンポーネントが存在する間のみ有効。
    };

    using SweepHit = RaycastHit;

    /**
     * @brief シーン照会で重なりが検出されたオブジェクト。
     * @details 照会メソッドは各GameObjectを最大1回だけ返す。
     */
    using PhysicsOverlapResults = std::vector<GameObjectHandle>;
} // namespace Engine
