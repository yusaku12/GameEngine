#include "Pch.h"
#include "Physics\Collider\TerrainColliderComponent.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Core\GameObject\Component\TerrainComponent.h"
#include "Core\GameObject\GameObject.h"
#include "Assets\Model\Resource\ModelResource.h"
#include "Assets\Model\ModelManager.h"
#include "Terrain\TerrainMeshBuilder.h"
#include "Terrain\TerrainChunks.h"
#include <imgui.h>

namespace Engine
{
    bool TerrainColliderComponent::cookChunks(const ModelResource& model,
        std::vector<std::shared_ptr<const PhysicsMeshAsset>>& assets)
    {
        if (model.meshes.empty())
        {
            LOG_ERROR("[TerrainCollider] Terrain must contain at least one chunk.");
            return false;
        }
        std::vector<std::shared_ptr<const PhysicsMeshAsset>> replacement;
        replacement.reserve(model.meshes.size());
        for (const auto& mesh : model.meshes)
        {
            auto asset = PhysicsMeshAsset::cookTriangle(mesh);
            if (!asset)
                return false;
            replacement.push_back(std::move(asset));
        }
        assets = std::move(replacement);
        return true;
    }

    bool TerrainColliderComponent::replaceAssets(std::vector<std::shared_ptr<const PhysicsMeshAsset>> assets,
        const ModelHandle source) noexcept
    {
        if (assets.empty() || !std::ranges::all_of(assets, [](const auto& asset)
            { return asset && asset->getType() == PhysicsMeshType::Triangle; }))
        {
            LOG_ERROR("[TerrainCollider] Expected cooked terrain chunk triangles.");
            return false;
        }
        auto previous = std::exchange(m_assets, std::move(assets));
        if (m_active)
        {
            auto* world = PhysicsSystem::instance().getWorld();
            if (!world || !world->updateCollider(*this) || !world->createCollider(*this))
            {
                m_assets = std::move(previous);
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
        std::vector<std::shared_ptr<const PhysicsMeshAsset>> assets;
        if (const auto model = ModelManager::instance().get(terrain->getModel()))
            return cookChunks(*model, assets) && replaceAssets(std::move(assets), terrain->getModel());
        ModelResource model;
        std::vector<TerrainChunk> chunks;
        if (!buildTerrainModel(terrain->getHeightmap(), terrain->getSettings(), model)
            || !partitionTerrainModel(model, terrain->getHeightmap().width(), terrain->getHeightmap().height(),
                terrain->getChunkSettings(), chunks))
            return false;
        return cookChunks(model, assets) && replaceAssets(std::move(assets), terrain->getModel());
    }

    PhysicsShapeGeometry TerrainColliderComponent::getGeometry(const Vector3& scale) const noexcept
    {
        const auto* object = getGameObject();
        Transform represented;
        if (!object || !Transform::tryFromMatrix(object->getWorldMatrix(), represented)
            || scale.x <= 0 || scale.y <= 0 || scale.z <= 0)
        {
            LOG_ERROR("[TerrainCollider] PhysX terrain requires a non-sheared transform with positive scale.");
            return { PhysicsShapeType::TriangleMesh, Vector3::Zero, getPhysicsAsset() };
        }
        return { PhysicsShapeType::TriangleMesh, scale, getPhysicsAsset() };
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
        if ((m_assets.empty() || !m_sourceModel.isValid() || m_sourceModel != terrain->getModel()) && !generateAsset())
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
        m_assets.clear();
        m_sourceModel = {};
    }

    void TerrainColliderComponent::onImGui()
    {
        ImGui::TextWrapped("Static triangles from the Terrain on this GameObject. "
            "Rebuilt automatically; no external .physics asset. Positive scale only; no Rigidbody or trigger.");
        ImGui::Text("Cooked Chunk Shapes: %zu", m_assets.size());
        if (!getGameObject() || !getGameObject()->hasComponent<TerrainComponent>())
            ImGui::TextDisabled("Add a Terrain component to supply geometry.");
        ColliderComponent::onImGui();
    }
}
