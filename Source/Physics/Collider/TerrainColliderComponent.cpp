#include "Pch.h"
#include "Physics\Collider\TerrainColliderComponent.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Core\GameObject\Component\TerrainComponent.h"
#include "Core\GameObject\GameObject.h"
#include "Assets\Model\Resource\ModelResource.h"
#include "Terrain\TerrainMeshBuilder.h"
#include <imgui.h>

namespace Engine
{
    bool TerrainColliderComponent::replaceAsset(std::shared_ptr<const PhysicsMeshAsset> asset, const ModelHandle source) noexcept
    {
        if (!asset || asset->getType() != PhysicsMeshType::Triangle)
        {
            LOG_ERROR("[TerrainCollider] Expected cooked terrain triangles.");
            return false;
        }
        auto previous = std::exchange(m_asset, std::move(asset));
        if (m_active)
        {
            auto* world = PhysicsSystem::instance().getWorld();
            if (!world || !world->updateCollider(*this) || !world->createCollider(*this))
            {
                m_asset = std::move(previous);
                LOG_ERROR("[TerrainCollider] Physics replacement failed; previous terrain is preserved.");
                return false;
            }
        }
        m_sourceModel = source;
        return true;
    }

    bool TerrainColliderComponent::generateAsset()
    {
        auto* object = getGameObject();
        const auto* terrain = object ? object->getComponent<TerrainComponent>() : nullptr;
        if (!terrain)
        {
            LOG_ERROR("[TerrainCollider] A Terrain component is required on the same GameObject.");
            return false;
        }
        ModelResource model;
        if (!buildTerrainModel(terrain->getHeightmap(), terrain->getSettings(), model))
            return false;
        auto asset = PhysicsMeshAsset::cookTriangle(model.meshes[0]);
        return asset && replaceAsset(std::move(asset), terrain->getModel());
    }

    PhysicsShapeGeometry TerrainColliderComponent::getGeometry(const Vector3& scale) const noexcept
    {
        const auto* object = getGameObject();
        Transform represented;
        if (!object || !Transform::tryFromMatrix(object->getWorldMatrix(), represented)
            || scale.x <= 0 || scale.y <= 0 || scale.z <= 0)
        {
            LOG_ERROR("[TerrainCollider] PhysX terrain requires a non-sheared transform with positive scale.");
            return { PhysicsShapeType::TriangleMesh, Vector3::Zero, m_asset };
        }
        return { PhysicsShapeType::TriangleMesh, scale, m_asset };
    }

    void TerrainColliderComponent::onEnable()
    {
        if (!getGameObject() || !getGameObject()->hasComponent<TerrainComponent>())
        {
            detachTerrain();
            LOG_ERROR("[TerrainCollider] A Terrain component is required on the same GameObject.");
            return;
        }
        // Deserialize/clone may add the collider before restoring the Terrain; defer cooking until activation.
        const auto* terrain = getGameObject()->getComponent<TerrainComponent>();
        if ((!m_asset || !m_sourceModel.isValid() || m_sourceModel != terrain->getModel()) && !generateAsset())
            return;
        auto* world = PhysicsSystem::instance().getWorld();
        m_active = world && world->createCollider(*this);
        if (!m_active)
            LOG_ERROR("[TerrainCollider] Cannot activate PhysX terrain collision.");
    }

    void TerrainColliderComponent::onDisable()
    {
        if (auto* world = PhysicsSystem::instance().getWorld())
            world->destroyCollider(*this);
        m_active = false;
    }

    void TerrainColliderComponent::onDestroy()
    {
        onDisable();
    }

    void TerrainColliderComponent::onPayloadRestored()
    {
        // Registration belongs to onEnable, not asset restoration (inactive prefab/clone safety).
    }

    void TerrainColliderComponent::onFixedUpdate(float)
    {
        if (!m_active && getGameObject()->hasComponent<TerrainComponent>())
            onEnable();
    }

    void TerrainColliderComponent::detachTerrain() noexcept
    {
        onDisable();
        m_asset.reset();
        m_sourceModel = {};
    }

    void TerrainColliderComponent::onImGui()
    {
        ImGui::TextWrapped("Static triangles from the Terrain on this GameObject. "
            "Rebuilt automatically; no external .physics asset. Positive scale only; no Rigidbody or trigger.");
        if (!getGameObject() || !getGameObject()->hasComponent<TerrainComponent>())
            ImGui::TextDisabled("Add a Terrain component to supply geometry.");
        ColliderComponent::onImGui();
    }
}
