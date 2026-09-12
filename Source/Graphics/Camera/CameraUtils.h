#pragma once

#include "Graphics\Camera\CameraTypes.h"
#include "Core\Math\Geometry.h"

namespace Engine::CameraUtils
{
    /**
     * @brief 左手座標系のPerspective Projection行列を生成する。
     * @param fieldOfViewDegrees 垂直FOV（度）。
     * @param aspectRatio 横幅を高さで割ったAspect Ratio。
     * @param nearClip Near Clip距離。
     * @param farClip Far Clip距離。
     * @return DirectXのDepth Range 0～1に対応するProjection行列。
     */
    Matrix buildPerspective(float fieldOfViewDegrees, float aspectRatio, float nearClip, float farClip) noexcept;

    /**
     * @brief 左手座標系のOrthographic Projection行列を生成する。
     * @param orthographicSize Viewport縦方向の半分のWorld Size。
     * @param aspectRatio 横幅を高さで割ったAspect Ratio。
     * @param nearClip Near Clip距離。
     * @param farClip Far Clip距離。
     * @return DirectXのDepth Range 0～1に対応するProjection行列。
     */
    Matrix buildOrthographic(float orthographicSize, float aspectRatio, float nearClip, float farClip) noexcept;

    /**
     * @brief Projection行列からWorld Space Frustumを生成する。
     * @param projection Projection行列。
     * @param inverseView CameraのWorld行列。
     * @return World Space Frustum。
     */
    Frustum buildWorldFrustum(const Matrix& projection, const Matrix& inverseView) noexcept;

    /**
     * @brief 正規化Camera ViewportをPixel Viewportへ変換する。
     * @param viewport 正規化Viewport。
     * @param renderTargetSize Render TargetのPixel Size。
     * @return Pixel単位のViewport。
     */
    Viewport toPixelViewport(const CameraViewport& viewport, const Vector2& renderTargetSize) noexcept;
} // namespace Engine::CameraUtils
