#include "Pch.h"
#include "Core\GameObject\Component\TransformComponent.h"

#include <imgui.h>

namespace Engine
{
    void TransformComponent::onImGui()
    {
        Vector3 position = m_localTransform.getPosition();
        Vector3 rotation = m_localTransform.getEulerAngles();
        Vector3 scale = m_localTransform.getScale();
        float positionData[3] = { position.x, position.y, position.z };
        float rotationData[3] = { rotation.x, rotation.y, rotation.z };
        float scaleData[3] = { scale.x, scale.y, scale.z };

        if (ImGui::DragFloat3("Position", positionData, 0.1f))
            m_localTransform.setPosition(Vector3(positionData[0], positionData[1], positionData[2]));
        if (ImGui::DragFloat3("Rotation", rotationData, 0.01f))
            m_localTransform.setEulerAngles(rotationData[0], rotationData[1], rotationData[2]);
        if (ImGui::DragFloat3("Scale", scaleData, 0.01f))
            m_localTransform.setScale(Vector3(scaleData[0], scaleData[1], scaleData[2]));
    }
} // namespace Engine