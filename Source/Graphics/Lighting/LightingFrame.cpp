#include "Pch.h"
#include "Graphics\Lighting\LightingFrame.h"
#include "Core\Scene\Scene.h"

namespace Engine
{
    LightingFrame& LightingFrame::instance() noexcept
    {
        static LightingFrame frame;
        return frame;
    }

    void LightingFrame::capture(const Scene* active, const Scene& persistent) noexcept
    {
        m_constants = {};
        m_shadowIndex = -1;
        std::size_t count = 0;
        bool overflow = false;
        const auto collect = [&](const Scene& scene)
        {
            for (const auto& object : scene.getGameObjects())
            {
                const auto* light = object->getComponent<LightComponent>();
                if (!light || !light->isEnabled() || !object->isActiveInHierarchy())
                    continue;
                if (count == m_constants.lights.size())
                {
                    overflow = true;
                    continue;
                }
                const auto& settings = light->settings();
                Vector3 direction = Vector3::Transform(Vector3::UnitZ, object->getWorldRotation());
                direction.Normalize();
                m_constants.lights[count] = {
                    Vector4(object->getWorldPosition().x, object->getWorldPosition().y,
                        object->getWorldPosition().z, static_cast<float>(settings.kind)),
                    Vector4(settings.color.x * settings.intensity, settings.color.y * settings.intensity,
                        settings.color.z * settings.intensity, settings.range),
                    Vector4(direction.x, direction.y, direction.z,
                        std::cos(DirectX::XMConvertToRadians(settings.innerAngle * 0.5f))),
                    Vector4(std::cos(DirectX::XMConvertToRadians(settings.outerAngle * 0.5f)), 0, 0, 0) };
                if (settings.kind == LightKind::Directional && settings.castsShadow && settings.intensity > 0
                    && m_shadowIndex < 0)
                {
                    m_shadowIndex = static_cast<int>(count);
                    m_shadow = settings;
                    m_constants.lights[count].parameters.y = 1;
                }
                ++count;
            }
        };
        if (active)
            collect(*active);
        collect(persistent);
        m_constants.environment.w = static_cast<float>(count);
        if (overflow)
            LOG_ERROR("[Light] More than 32 active lights; excess lights are not rendered.");
    }

    LightingConstants LightingFrame::forCamera(const Vector3& position, const Vector3& forward) const noexcept
    {
        LightingConstants result = m_constants;
        result.cameraPosition = Vector4(position.x, position.y, position.z, 1);
        if (m_shadowIndex < 0)
            return result;
        const auto& light = result.lights[static_cast<std::size_t>(m_shadowIndex)];
        const Vector3 direction(light.directionInner.x, light.directionInner.y, light.directionInner.z);
        const Vector3 up = std::abs(direction.Dot(Vector3::UnitY)) > 0.95f ? Vector3::UnitX : Vector3::UnitY;
        const float distance = m_shadow.shadowDistance;
        const Vector3 center = position + forward * (distance * 0.5f);
        Matrix view(DirectX::XMMatrixLookAtLH(Vector3::Zero, direction, up));
        const float texel = distance / 2048.0f;
        const Vector3 lightCenter = Vector3::Transform(center, view);
        view._41 = -std::round(lightCenter.x / texel) * texel;
        view._42 = -std::round(lightCenter.y / texel) * texel;
        view._43 = distance - lightCenter.z;
        result.shadowViewProjection = view * Matrix(DirectX::XMMatrixOrthographicLH(
            distance, distance, 0.1f, distance * 2.0f));
        result.shadowOptions = Vector4(1, m_shadow.shadowBias, m_shadow.normalBias, 1.0f / 2048.0f);
        return result;
    }
}
