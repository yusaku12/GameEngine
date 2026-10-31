#pragma once

#include <cstdint>

namespace Engine
{
    /**
     * @brief 物理演算で使用する衝突レイヤーのインデックス。最大32レイヤーまで使用できる。
     */
    enum class PhysicsLayer : std::uint32_t
    {
        Default = 0,
        Player = 1,
        Enemy = 2,
        Environment = 3,
        Projectile = 4,
        Trigger = 5
    };

    using PhysicsLayerMask = std::uint32_t;

    constexpr PhysicsLayerMask ALL_PHYSICS_LAYERS = 0xFFFFFFFFu;

    /**
     * @brief 衝突レイヤーのビットマスクを生成する。
     */
    constexpr PhysicsLayerMask physicsLayerBit(const PhysicsLayer layer) noexcept
    {
        const std::uint32_t index = static_cast<std::uint32_t>(layer);
        return index < 32 ? (PhysicsLayerMask{ 1 } << index) : 0;
    }
} // namespace Engine
