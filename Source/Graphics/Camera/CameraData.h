#pragma once

#include <cstdint>
#include <limits>
#include "Core\Math\Geometry.h"
#include "Graphics\Camera\CameraTypes.h"

namespace Engine
{
    /**
     * @brief Rendererへ渡すCamera行列とフレーム情報。
     */
    struct CameraRenderData
    {
        Matrix view = Matrix::Identity;                   //!< View行列
        Matrix projection = Matrix::Identity;             //!< Projection行列
        Matrix viewProjection = Matrix::Identity;         //!< ViewProjection行列
        Matrix inverseView = Matrix::Identity;            //!< 逆View行列
        Matrix inverseProjection = Matrix::Identity;      //!< 逆Projection行列
        Matrix inverseViewProjection = Matrix::Identity;  //!< 逆ViewProjection行列
        Matrix previousViewProjection = Matrix::Identity; //!< 前フレームのViewProjection行列
        Vector3 position = Vector3::Zero;                 //!< Cameraのワールド空間での位置
        float nearClip = 0.1f;                            //!< Nearクリップ距離
        Vector3 forward = Vector3::UnitZ;                 //!< Cameraのワールド空間での前方方向
        float farClip = 1000.0f;                          //!< Farクリップ距離
        Vector2 viewportSize = Vector2::One;              //!< CameraのViewportサイズ
        Vector2 inverseViewportSize = Vector2::One;       //!< CameraのViewportサイズの逆数
        Vector2 jitter = Vector2::Zero;                   //!< CameraのJitter値
    };

    /**
     * @brief Render threadへ渡すCameraのimmutable snapshot。
     */
    struct RenderView
    {
        CameraRenderData camera;                                               //!< Cameraのレンダリングデータ
        Frustum frustum{};                                                     //!< CameraのFrustum
        CameraViewport viewport{};                                             //!< CameraのViewport
        CameraClearMode clearMode = CameraClearMode::SolidColor;               //!< Cameraのクリアモード
        Color backgroundColor = Color(0.08f, 0.16f, 0.24f, 1.0f);              //!< Cameraの背景色
        std::uint32_t cullingMask = std::numeric_limits<std::uint32_t>::max(); //!< CameraのCulling Mask
        int priority = 0;                                                      //!< Cameraの描画優先度
    };
} // namespace Engine
