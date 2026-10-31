#pragma once

#include <cstdint>

namespace Engine
{
    /**
     * @brief エディター専用の物理可視化に使用する実行時オプション。
     */
    struct PhysicsDebugSettings
    {
        bool enabled = false;
        bool drawColliders = true;
        bool drawContacts = true;
        bool drawAabbs = false;
        bool drawVelocities = false;
        bool drawCenterOfMass = false;
        bool drawRaycasts = true;
    };

    /**
     * @brief PhysicsWorldが管理する有効なオブジェクト数。
     */
    struct PhysicsWorldStatistics
    {
        std::uint32_t activeActors = 0;
        std::uint32_t activeBodies = 0;
        std::uint32_t dynamicBodies = 0;
        std::uint32_t staticBodies = 0;
        std::uint32_t activeShapes = 0;
        std::uint32_t activeJoints = 0;
        std::uint32_t activeContacts = 0;
    };

    /**
     * @brief エディターでのプロファイリングに使用する物理演算時間の直近計測情報。
     */
    struct PhysicsProfilerSnapshot
    {
        float fixedDeltaTime = 0.0f;
        double lastStepMilliseconds = 0.0;
        double averageStepMilliseconds = 0.0;
        std::uint64_t totalSteps = 0;
        std::uint32_t activeBodies = 0;
        std::uint32_t activeActors = 0;
        std::uint32_t dynamicBodies = 0;
        std::uint32_t staticBodies = 0;
        std::uint32_t activeShapes = 0;
        std::uint32_t activeJoints = 0;
        std::uint32_t activeContacts = 0;
    };
} // namespace Engine
