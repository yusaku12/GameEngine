#include "Pch.h"
#include "Graphics\Camera\CameraUtils.h"
#include "Core\Logging\Logging.h"

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

    CameraFrustum buildWorldFrustum(const Matrix& projection, const Matrix& inverseView) noexcept
    {
        CameraFrustum result;
        DirectX::XMVECTOR determinant;
        const Matrix view(DirectX::XMMatrixInverse(&determinant, inverseView));
        const Matrix viewProjection = view * projection;
        if (!std::isfinite(DirectX::XMVectorGetX(determinant))
            || DirectX::XMVectorGetX(determinant) == 0.0f)
        {
            LOG_ERROR("[Camera] Cannot build clip planes from a singular or nonfinite camera transform; culling disabled.");
            return result;
        }
        for (const auto& row : viewProjection.m)
            for (const float value : row)
                if (!std::isfinite(value))
                {
                    LOG_ERROR("[Camera] Nonfinite view-projection matrix; culling disabled.");
                    return result;
                }
        for (std::size_t index = 0; index < result.planes.size(); ++index)
        {
            auto& plane = result.planes[index];
            const std::size_t column = index / 2;
            for (std::size_t row = 0; row < 4; ++row)
            {
                // Row-vector DirectX clip space: -w <= x,y <= w and 0 <= z <= w.
                plane[row] = index == 4 ? viewProjection.m[row][2]
                    : static_cast<double>(viewProjection.m[row][3])
                        + (index % 2 == 0 ? 1.0 : -1.0) * viewProjection.m[row][column];
            }
            const double length = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
            if (!std::isfinite(length) || length == 0.0)
            {
                LOG_ERROR("[Camera] Degenerate clip plane {}; culling disabled.", index);
                return result;
            }
            for (double& value : plane)
                value /= length;
        }
        result.valid = true;
        return result;
    }

    Viewport toPixelViewport(const CameraViewport& viewport, const Vector2& renderTargetSize) noexcept
    {
        const float x = viewport.x * renderTargetSize.x;
        const float y = viewport.y * renderTargetSize.y;
        const float right = static_cast<float>(std::min(1.0,
            static_cast<double>(viewport.x) + viewport.width) * renderTargetSize.x);
        const float bottom = static_cast<float>(std::min(1.0,
            static_cast<double>(viewport.y) + viewport.height) * renderTargetSize.y);
        float width = right - x;
        float height = bottom - y;
        // A rounded subtraction followed by addition can overshoot even an exact target edge.
        if (x + width > right)
            width = std::nextafter(width, 0.0f);
        if (y + height > bottom)
            height = std::nextafter(height, 0.0f);
        return Viewport(x, y, width, height, 0.0f, 1.0f);
    }
} // namespace Engine::CameraUtils