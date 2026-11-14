#include "Pch.h"
#include "Graphics\Lighting\CascadedShadow.h"

#include <cstdio>
#include <limits>

namespace
{
    using namespace Engine;

    bool testCoverage(const Matrix& projection, const float nearClip, const float farClip)
    {
        const Matrix inverse = projection.Invert();
        CascadedShadowData cascades;
        if (!buildCascadedShadow(inverse, projection, nearClip, farClip, 50.0f, 0.02f, Vector3::UnitZ, cascades))
            return false;
        if (!(cascades.splits[0] > nearClip && cascades.splits[1] > cascades.splits[0]
            && cascades.splits[2] > cascades.splits[1]
            && std::abs(cascades.splits[2] - std::min(50.0f, farClip)) < 0.001f))
            return false;
        for (std::size_t cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade)
        {
            const float previous = cascade == 0 ? nearClip : cascades.splits[cascade - 1];
            const float previousStart = cascade < 2 ? nearClip : cascades.splits[cascade - 2];
            const float start = cascade == 0 ? nearClip : previous - (previous - previousStart) * SHADOW_CASCADE_BLEND;
            if (!(cascades.texelWorld[cascade] > 0.0f))
                return false;
            for (int corner = 0; corner < 4; ++corner)
            {
                const float x = (corner & 1) != 0 ? 1.0f : -1.0f;
                const float y = (corner & 2) != 0 ? 1.0f : -1.0f;
                const Vector3 nearPoint = Vector3::Transform(Vector3(x, y, 0), inverse);
                const Vector3 farPoint = Vector3::Transform(Vector3(x, y, 1), inverse);
                for (const float distance : { start, cascades.splits[cascade] })
                {
                    const Vector3 point = nearPoint + (farPoint - nearPoint) * ((distance - nearClip) / (farClip - nearClip));
                    const Vector3 clip = Vector3::Transform(point, cascades.viewProjections[cascade]);
                    if (std::abs(clip.x) > 1.0001f || std::abs(clip.y) > 1.0001f
                        || clip.z < -0.0001f || clip.z > 1.0001f)
                        return false;
                }
            }
        }
        return true;
    }

    bool testSnapping()
    {
        const Matrix projection(DirectX::XMMatrixPerspectiveFovLH(1.0f, 1.5f, 0.1f, 100.0f));
        CascadedShadowData first;
        CascadedShadowData moved;
        if (!buildCascadedShadow(projection.Invert(), projection, 0.1f, 100.0f, 50.0f, 0.02f, Vector3::UnitZ, first))
            return false;
        const Matrix view(DirectX::XMMatrixTranslation(-first.texelWorld[0] * 0.3f, 0, 0));
        if (!buildCascadedShadow((view * projection).Invert(), projection, 0.1f, 100.0f, 50.0f, 0.02f, Vector3::UnitZ, moved))
            return false;
        const float texelShiftX = (moved.viewProjections[0]._41 - first.viewProjections[0]._41)
            * static_cast<float>(SHADOW_CASCADE_SIZE) * 0.5f;
        const float texelShiftY = (moved.viewProjections[0]._42 - first.viewProjections[0]._42)
            * static_cast<float>(SHADOW_CASCADE_SIZE) * 0.5f;
        return std::abs(texelShiftX - std::round(texelShiftX)) < 0.001f
            && std::abs(texelShiftY - std::round(texelShiftY)) < 0.001f;
    }

    bool testLargeFarPlane()
    {
        constexpr float nearClip = 0.1f;
        constexpr float farClip = 1000000.0f;
        constexpr float fov = 0.017453293f;
        constexpr float aspect = 1.5f;
        const Matrix projection(DirectX::XMMatrixPerspectiveFovLH(fov, aspect, nearClip, farClip));
        CascadedShadowData cascades;
        if (!buildCascadedShadow(projection.Invert(), projection, nearClip, farClip, 50.0f, 0.02f, Vector3::UnitY, cascades))
            return false;
        const float overlapStart = cascades.splits[1]
            - (cascades.splits[1] - cascades.splits[0]) * SHADOW_CASCADE_BLEND;
        for (const float distance : { overlapStart, cascades.splits[2] })
        {
            const float y = distance * std::tan(fov * 0.5f);
            for (const float xSign : { -1.0f, 1.0f })
                for (const float ySign : { -1.0f, 1.0f })
                {
                    const Vector3 edge = Vector3::Transform(Vector3(xSign * y * aspect, ySign * y, distance),
                        cascades.viewProjections[2]);
                    if (std::abs(edge.x) > 1.0001f || std::abs(edge.y) > 1.0001f
                        || edge.z < -0.0001f || edge.z > 1.0001f)
                        return false;
                }
        }
        return true;
    }

    bool testTransformedCamera()
    {
        constexpr float nearClip = 0.2f;
        constexpr float farClip = 500.0f;
        constexpr float fov = 1.1f;
        constexpr float aspect = 1.7f;
        const Matrix projection(DirectX::XMMatrixPerspectiveFovLH(fov, aspect, nearClip, farClip));
        const Vector3 position(50.0f, 20.0f, -30.0f);
        const Matrix view(DirectX::XMMatrixLookAtLH(position, Vector3(30.0f, 10.0f, 20.0f), Vector3::UnitY));
        Vector3 light(0.3f, -1.0f, 0.2f);
        light.Normalize();
        CascadedShadowData cascades;
        if (!buildCascadedShadow((view * projection).Invert(), projection, nearClip, farClip, 50.0f, 0.02f, light, cascades))
            return false;
        const Matrix inverseView = view.Invert();
        for (std::size_t cascade = 0; cascade < SHADOW_CASCADE_COUNT; ++cascade)
        {
            const float previous = cascade == 0 ? nearClip : cascades.splits[cascade - 1];
            const float previousStart = cascade < 2 ? nearClip : cascades.splits[cascade - 2];
            const float start = cascade == 0 ? nearClip : previous - (previous - previousStart) * SHADOW_CASCADE_BLEND;
            for (const float distance : { start, cascades.splits[cascade] })
            {
                const float y = distance * std::tan(fov * 0.5f);
                for (const float xSign : { -1.0f, 1.0f })
                    for (const float ySign : { -1.0f, 1.0f })
                    {
                        const Vector3 world = Vector3::Transform(Vector3(xSign * y * aspect, ySign * y, distance), inverseView);
                        for (const Vector3& offset : { Vector3::Zero, Vector3(0.16f, 0, 0), Vector3(-0.16f, 0, 0),
                            Vector3(0, 0.16f, 0), Vector3(0, -0.16f, 0), Vector3(0, 0, 0.16f), Vector3(0, 0, -0.16f) })
                        {
                            const Vector3 clip = Vector3::Transform(world + offset, cascades.viewProjections[cascade]);
                            if (std::abs(clip.x) > 1.0001f || std::abs(clip.y) > 1.0001f
                                || clip.z < -0.0001f || clip.z > 1.0001f)
                                return false;
                        }
                    }
            }
        }
        return true;
    }

    bool testInvalid()
    {
        CascadedShadowData result;
        result.splits[0] = 123.0f;
        const Matrix projection(DirectX::XMMatrixPerspectiveFovLH(1.0f, 1.0f, 0.1f, 100.0f));
        const Matrix inverse = projection.Invert();
        Matrix invalidInverse = inverse;
        invalidInverse._11 = std::numeric_limits<float>::quiet_NaN();
        return !buildCascadedShadow(inverse, projection, 0.1f, 100.0f, 0.05f, 0.02f, Vector3::UnitZ, result)
            && !buildCascadedShadow(inverse, projection, 1.0f, 1.0f, 50.0f, 0.02f, Vector3::UnitZ, result)
            && !buildCascadedShadow(inverse, projection, 0.1f, 100.0f, 50.0f, 0.02f, Vector3::Zero, result)
            && !buildCascadedShadow(invalidInverse, projection, 0.1f, 100.0f, 50.0f, 0.02f, Vector3::UnitZ, result)
            && result.splits[0] == 123.0f;
    }

    bool testCasterDepth()
    {
        ShadowDepthRange range;
        const Matrix original = Matrix::Identity;
        const AABB nearCaster(Vector3(0, 0, -100), Vector3(1, 1, 5));
        const AABB farCaster(Vector3(0, 0, 300), Vector3(1, 1, 10));
        if (!range.include(original, nearCaster) || !range.include(original, farCaster))
            return false;
        Matrix adjusted = original;
        float biasScale = 0.0f;
        if (!range.apply(adjusted, biasScale) || !(biasScale > 0.0f && biasScale < 1.0f))
            return false;
        for (const float z : { -105.0f, -95.0f, 0.0f, 1.0f, 290.0f, 310.0f })
        {
            const Vector3 point = Vector3::Transform(Vector3(0.25f, -0.5f, z), adjusted);
            if (point.x != 0.25f || point.y != -0.5f || point.z <= 0.0f || point.z >= 1.0f)
                return false;
        }
        AABB invalid = nearCaster;
        invalid.Extents.z = -1.0f;
        const double before = range.minimum;
        return !range.include(original, invalid) && range.minimum == before;
    }
}

int main()
{
    const Matrix perspective(DirectX::XMMatrixPerspectiveFovLH(1.0f, 1.5f, 0.1f, 100.0f));
    const Matrix orthographic(DirectX::XMMatrixOrthographicLH(30.0f, 20.0f, 0.1f, 100.0f));
    const Matrix shortFar(DirectX::XMMatrixPerspectiveFovLH(1.0f, 1.5f, 0.1f, 20.0f));
    if (!testCoverage(perspective, 0.1f, 100.0f)
        || !testCoverage(orthographic, 0.1f, 100.0f)
        || !testCoverage(shortFar, 0.1f, 20.0f)
        || !testSnapping() || !testLargeFarPlane() || !testTransformedCamera()
        || !testInvalid() || !testCasterDepth())
    {
        std::fprintf(stderr, "Cascaded shadow tests failed.\n");
        return 1;
    }
    std::printf("Cascaded shadow tests passed.\n");
    return 0;
}
