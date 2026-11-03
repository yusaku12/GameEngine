#pragma once

#include "Core\GameObject\Behaviour.h"

namespace Engine
{
    /**
     * @brief WASD移動、キャラクター旋回、TPSカメラを管理する。
     * @details W/Sはワールド+Z/-Z、A/Dはワールド-X/+Xへ移動する。右マウスボタンでカメラを回転する。
     * @thread_safety Main thread only.
     */
    class CharacterControllerMovementComponent final : public Behaviour
    {
    protected:

        void onEnable() override;
        void onDisable() override;
        void onUpdate(float deltaTime) override;
        void onFixedUpdate(float fixedDeltaTime) override;
        void onLateUpdate(float deltaTime) override;

    private:

        float m_verticalVelocity = 0.0f;
        float m_cameraYawOffset = 0.0f;
        float m_cameraPitch = -0.2f;
        bool m_reportedInvalidSetup = false;
        bool m_reportedInvalidCamera = false;
    };
} // namespace Engine
