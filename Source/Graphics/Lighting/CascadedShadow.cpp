#include "Pch.h"
#include "Graphics\Lighting\CascadedShadow.h"

namespace Engine
{
    bool ShadowDepthRange::include(const Matrix& viewProjection, const AABB& bounds) noexcept
    {
        const auto& center = bounds.Center;
        const auto& extents = bounds.Extents;
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)
            || !std::isfinite(extents.x) || !std::isfinite(extents.y) || !std::isfinite(extents.z)
            || extents.x < 0.0f || extents.y < 0.0f || extents.z < 0.0f)
            return false;
        const double depth = static_cast<double>(center.x) * viewProjection._13
            + static_cast<double>(center.y) * viewProjection._23
            + static_cast<double>(center.z) * viewProjection._33 + viewProjection._43;
        const double radius = std::abs(static_cast<double>(viewProjection._13)) * extents.x
            + std::abs(static_cast<double>(viewProjection._23)) * extents.y
            + std::abs(static_cast<double>(viewProjection._33)) * extents.z;
        if (!std::isfinite(depth) || !std::isfinite(radius)
            || !std::isfinite(depth - radius) || !std::isfinite(depth + radius))
            return false;
        minimum = std::min(minimum, depth - radius);
        maximum = std::max(maximum, depth + radius);
        return true;
    }

    bool ShadowDepthRange::apply(Matrix& viewProjection, float& biasScale) const noexcept
    {
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum < minimum)
            return false;
        const double padding = std::max(0.001, (maximum - minimum) * 0.00001);
        const double low = minimum - padding;
        const double range = maximum + padding - low;
        const float scale = static_cast<float>(1.0 / range);
        if (!std::isfinite(range) || !std::isfinite(scale) || scale <= 0.0f)
            return false;
        Matrix adjusted = viewProjection;
        for (std::size_t row = 0; row < 4; ++row)
        {
            const double value = (static_cast<double>(viewProjection.m[row][2])
                - low * viewProjection.m[row][3]) / range;
            if (!std::isfinite(value) || std::abs(value) > FLT_MAX)
                return false;
            adjusted.m[row][2] = static_cast<float>(value);
        }
        viewProjection = adjusted;
        biasScale = scale;
        return true;
    }

    bool buildCascadedShadow(const Matrix& inverseViewProjection, const Matrix& projection,
        const float nearClip, const float farClip, const float shadowDistance, const float normalBias,
        const Vector3& lightDirection, CascadedShadowData& result) noexcept
    {
        const float distance = std::min(shadowDistance, farClip);
        if (!std::isfinite(nearClip) || !std::isfinite(farClip) || !std::isfinite(shadowDistance)
            || !std::isfinite(normalBias) || normalBias < 0.0f
            || nearClip <= 0.0f || farClip <= nearClip || distance <= nearClip
            || !std::isfinite(lightDirection.x) || !std::isfinite(lightDirection.y)
            || !std::isfinite(lightDirection.z) || std::abs(lightDirection.LengthSquared() - 1.0f) > 0.001f)
            return false;
        for (const auto& row : inverseViewProjection.m)
            for (const float value : row)
                if (!std::isfinite(value))
                    return false;
        const float denominator = projection._33 - 0.5f * projection._34;
        if (!std::isfinite(denominator) || denominator == 0.0f)
            return false;
        // Mid-depth unprojection avoids the loss of precision at a distant far plane.
        const float middleDistance = (0.5f * projection._44 - projection._43) / denominator;
        if (!std::isfinite(middleDistance) || middleDistance <= nearClip)
            return false;

        CascadedShadowData data;
        std::array<Vector3, 4> nearCorners;
        std::array<Vector3, 4> middleCorners;
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
            const float x = (corner & 1) != 0 ? 1.0f : -1.0f;
            const float y = (corner & 2) != 0 ? 1.0f : -1.0f;
            nearCorners[corner] = Vector3::Transform(Vector3(x, y, 0.0f), inverseViewProjection);
            middleCorners[corner] = Vector3::Transform(Vector3(x, y, 0.5f), inverseViewProjection);
        }
        for (std::size_t cascade = 0; cascade < data.splits.size(); ++cascade)
        {
            const float fraction = static_cast<float>(cascade + 1) / static_cast<float>(data.splits.size());
            const float logarithmic = nearClip * std::pow(distance / nearClip, fraction);
            const float uniform = nearClip + (distance - nearClip) * fraction;
            data.splits[cascade] = 0.5f * (logarithmic + uniform);
            if (!std::isfinite(data.splits[cascade])
                || data.splits[cascade] <= (cascade == 0 ? nearClip : data.splits[cascade - 1]))
                return false;
        }
        const Vector3 up = std::abs(lightDirection.Dot(Vector3::UnitX)) > 0.9999f ? Vector3::UnitY : Vector3::UnitX;
        const Matrix lightRotation(DirectX::XMMatrixLookAtLH(Vector3::Zero, lightDirection, up));
        for (std::size_t cascade = 0; cascade < data.splits.size(); ++cascade)
        {
            const float previous = cascade == 0 ? nearClip : data.splits[cascade - 1];
            const float previousStart = cascade < 2 ? nearClip : data.splits[cascade - 2];
            const float start = cascade == 0 ? nearClip : previous - (previous - previousStart) * SHADOW_CASCADE_BLEND;
            const float startFraction = (start - nearClip) / (middleDistance - nearClip);
            const float endFraction = (data.splits[cascade] - nearClip) / (middleDistance - nearClip);
            std::array<Vector3, 8> corners;
            Vector3 center = Vector3::Zero;
            for (std::size_t corner = 0; corner < 4; ++corner)
            {
                corners[corner] = nearCorners[corner] + (middleCorners[corner] - nearCorners[corner]) * startFraction;
                corners[corner + 4] = nearCorners[corner] + (middleCorners[corner] - nearCorners[corner]) * endFraction;
                center += corners[corner] + corners[corner + 4];
            }
            center /= 8.0f;
            float radius = 0.0f;
            for (const Vector3& corner : corners)
                radius = std::max(radius, (corner - center).Length());
            if (!std::isfinite(radius) || radius <= 0.0f
                || !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z))
                return false;
            // Shader normal bias is clamped to at most eight times the first cascade's bias.
            radius = std::ceil((radius + normalBias * 8.0f) * 16.0f) / 16.0f;
            // Reserve two texels for snapping and PCF at the edge.
            radius *= static_cast<float>(SHADOW_CASCADE_SIZE) / (SHADOW_CASCADE_SIZE - 4.0f);
            if (!std::isfinite(radius))
                return false;
            const float texel = radius * 2.0f / SHADOW_CASCADE_SIZE;
            data.texelWorld[cascade] = texel;
            const Vector3 lightCenter = Vector3::Transform(center, lightRotation);
            Matrix view = lightRotation;
            view._41 = -std::round(lightCenter.x / texel) * texel;
            view._42 = -std::round(lightCenter.y / texel) * texel;
            view._43 = radius - lightCenter.z;
            data.viewProjections[cascade] = view * Matrix(DirectX::XMMatrixOrthographicLH(
                radius * 2.0f, radius * 2.0f, 0.0f, radius * 2.0f));
            for (const auto& row : data.viewProjections[cascade].m)
                for (const float value : row)
                    if (!std::isfinite(value))
                        return false;
        }
        result = data;
        return true;
    }
}
