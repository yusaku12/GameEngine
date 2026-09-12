#include "Pch.h"
#include "Graphics\Camera\CameraUtils.h"

namespace Engine::CameraUtils
{
    Matrix buildPerspective(const float fieldOfViewDegrees, const float aspectRatio,
        const float nearClip, const float farClip) noexcept
    {
        return Matrix(DirectX::XMMatrixPerspectiveFovLH(
            DirectX::XMConvertToRadians(fieldOfViewDegrees), aspectRatio, nearClip, farClip));
    }

    Matrix buildOrthographic(const float orthographicSize, const float aspectRatio,
        const float nearClip, const float farClip) noexcept
    {
        const float height = orthographicSize * 2.0f;
        return Matrix(DirectX::XMMatrixOrthographicLH(height * aspectRatio, height, nearClip, farClip));
    }

    Frustum buildWorldFrustum(const Matrix& projection, const Matrix& inverseView) noexcept
    {
        Frustum viewFrustum;
        Frustum::CreateFromMatrix(viewFrustum, projection, false);
        Frustum worldFrustum;
        viewFrustum.Transform(worldFrustum, inverseView);
        return worldFrustum;
    }

    Viewport toPixelViewport(const CameraViewport& viewport, const Vector2& renderTargetSize) noexcept
    {
        return Viewport(
            viewport.x * renderTargetSize.x,
            viewport.y * renderTargetSize.y,
            viewport.width * renderTargetSize.x,
            viewport.height * renderTargetSize.y,
            0.0f,
            1.0f);
    }
} // namespace Engine::CameraUtils