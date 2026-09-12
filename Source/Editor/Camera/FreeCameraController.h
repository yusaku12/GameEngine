#pragma once

#include "Core\Math\Geometry.h"

namespace Engine
{
    class CameraComponent;

    /**
     * @brief Editor Scene View向けのFree Camera操作を提供する。
     * @details Scene Viewが入力を占有している間だけupdateを呼び出す。
     * @thread_safety Main thread only.
     */
    class FreeCameraController
    {
    public:

        /**
         * @brief 操作対象Cameraを指定して生成する。
         * @param camera 操作対象。Controllerより長く生存する必要がある。
         */
        explicit FreeCameraController(CameraComponent& camera) noexcept;

        /**
         * @brief 現在の入力でCamera Transformを更新する。
         * @param deltaTime 前フレームからの経過時間（秒）。
         */
        void update(float deltaTime) noexcept;

        /**
         * @brief 操作の有効状態を設定する。
         * @param enabled 入力を反映する場合はtrue。
         */
        void setEnabled(bool enabled) noexcept { m_enabled = enabled; }

        /**
         * @brief 操作が有効か取得する。
         * @return 有効な場合はtrue。
         */
        bool isEnabled() const noexcept { return m_enabled; }

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

    private:

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

        CameraComponent* m_camera = nullptr;  //!< 操作対象Camera
        Vector3 m_orbitPoint = Vector3::Zero; //!< Orbit中心点
        float m_orbitDistance = 5.0f;         //!< Orbit中心からの距離
        float m_moveSpeed = 5.0f;             //!< 通常移動速度（World Unit毎秒）
        float m_lookSensitivity = 0.003f;     //!< Mouse回転感度（PixelあたりのRadian係数）
        float m_panSensitivity = 0.005f;      //!< Pan操作感度（PixelあたりのWorld Unit係数）
        float m_zoomSensitivity = 0.01f;      //!< Zoom操作感度（PixelあたりのWorld Unit係数）
        float m_pitch = 0.0f;                 //!< Pitch角度（Radian）
        float m_yaw = 0.0f;                   //!< Yaw角度（Radian）
        bool m_rotationInitialized = false;   //!< 初回回転更新時にYaw/Pitchを初期化するフラグ
        bool m_enabled = true;                //!< 操作有効フラグ
    };
} // namespace Engine
