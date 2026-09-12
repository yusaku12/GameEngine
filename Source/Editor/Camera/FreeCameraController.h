#pragma once

#include "Core\GameObject\Component.h"
#include "Core\Math\Geometry.h"

namespace Engine
{
    class CameraComponent;

    /**
     * @brief Editor Scene View向けのFree Camera操作を提供する。
     * @details Scene Viewが入力を占有している間だけupdateを呼び出す。
     * @thread_safety Main thread only.
     */
    class FreeCameraController final : public Component
    {
    public:

        /**
         * @brief Free Camera Controllerを生成する。
         */
        FreeCameraController() noexcept;

        /**
         * @brief 通常移動速度を設定する。
         * @param speed World Unit毎秒。
         */
        void setMoveSpeed(float speed) noexcept;

        /**
         * @brief Mouse回転感度を設定する。
         * @param sensitivity PixelあたりのRadian係数。
         */
        void setLookSensitivity(float sensitivity) noexcept;

        /**
         * @brief 指定Boundsが見える位置へCameraを移動する。
         * @param bounds 注視対象のWorld Space Bounds。
         */
        void focus(const AABB& bounds) noexcept;

        /**
         * @brief Orbit中心を設定する。
         * @param point World Space中心点。
         */
        void setOrbitPoint(const Vector3& point) noexcept;

    protected:

        void onUpdate(float deltaTime) override;
        void onImGui() override;

    private:

        CameraComponent* getCamera() noexcept;

        /**
         * @brief Cameraの回転を更新する。
         * @param yawDelta Yawの変化量（Radian）。
         * @param pitchDelta Pitchの変化量（Radian）。
         */
        void updateRotation(float yawDelta, float pitchDelta) noexcept;

        /**
         * @brief CameraのOrbitを更新する。
         * @param yawDelta Yawの変化量（Radian）。
         * @param pitchDelta Pitchの変化量（Radian）。
         */
        void updateOrbit(float yawDelta, float pitchDelta) noexcept;

        Vector3 m_orbitPoint = Vector3::Zero; //!< Orbit中心点
        float m_orbitDistance = 5.0f;         //!< Orbit中心からの距離
        float m_moveSpeed = 5.0f;             //!< 通常移動速度（World Unit毎秒）
        float m_lookSensitivity = 0.003f;     //!< Mouse回転感度（PixelあたりのRadian係数）
        float m_panSensitivity = 0.005f;      //!< Pan操作感度（PixelあたりのWorld Unit係数）
        float m_zoomSensitivity = 0.01f;      //!< Zoom操作感度（PixelあたりのWorld Unit係数）
        float m_pitch = 0.0f;                 //!< Pitch角度（Radian）
        float m_yaw = 0.0f;                   //!< Yaw角度（Radian）
        bool m_rotationInitialized = false;   //!< 初回回転更新時にYaw/Pitchを初期化するフラグ
    };
} // namespace Engine
