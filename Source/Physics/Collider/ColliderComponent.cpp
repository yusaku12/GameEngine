#include "Pch.h"
#include "Physics\Collider\ColliderComponent.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Physics\Collider\BoxColliderComponent.h"
#include "Physics\Collider\CapsuleColliderComponent.h"
#include "Physics\Collider\ConvexColliderComponent.h"
#include "Physics\Collider\MeshColliderComponent.h"
#include "Physics\Collider\SphereColliderComponent.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Generated\FlatBuffers\PhysicsComponent_generated.h"
#include "Physics\Internal\PhysicsPayload.h"
#include "Physics\PhysicsMeshAsset.h"
#include <imgui.h>
#include <system_error>

namespace Engine
{
    void ColliderComponent::onImGui()
    {
        Vector3 center = getCenter();
        if (ImGui::DragFloat3("Center", &center.x, 0.05f))
            setCenter(center);

        bool trigger = isTrigger();
        if (ImGui::Checkbox("Is Trigger", &trigger))
            setIsTrigger(trigger);

        constexpr const char* layerNames[] = {
            "Default", "Player", "Enemy", "Environment", "Projectile", "Trigger"
        };
        int layer = static_cast<int>(getLayer());
        if (ImGui::Combo("Layer", &layer, layerNames, static_cast<int>(std::size(layerNames))))
            setLayer(static_cast<PhysicsLayer>(layer));

        PhysicsLayerMask collisionMask = getCollisionMask();
        if (ImGui::InputScalar("Collision Mask", ImGuiDataType_U32, &collisionMask, nullptr, nullptr,
            "%08X", ImGuiInputTextFlags_CharsHexadecimal))
            setCollisionMask(collisionMask);

        PhysicsMaterial material = getMaterial();
        float staticFriction = material.getStaticFriction();
        if (ImGui::DragFloat("Static Friction", &staticFriction, 0.01f, 0.0f, 100.0f)
            && material.setStaticFriction(staticFriction))
            setMaterial(material);

        float dynamicFriction = material.getDynamicFriction();
        if (ImGui::DragFloat("Dynamic Friction", &dynamicFriction, 0.01f, 0.0f, 100.0f)
            && material.setDynamicFriction(dynamicFriction))
            setMaterial(material);

        float restitution = material.getRestitution();
        if (ImGui::DragFloat("Restitution", &restitution, 0.01f, 0.0f, 1.0f)
            && material.setRestitution(restitution))
            setMaterial(material);

        if (auto* const box = dynamic_cast<BoxColliderComponent*>(this))
        {
            Vector3 size = box->getSize();
            if (ImGui::DragFloat3("Size", &size.x, 0.05f, 0.001f))
                box->setSize(size);
        }
        else if (auto* const sphere = dynamic_cast<SphereColliderComponent*>(this))
        {
            float radius = sphere->getRadius();
            if (ImGui::DragFloat("Radius", &radius, 0.05f, 0.001f))
                sphere->setRadius(radius);
        }
        else if (auto* const capsule = dynamic_cast<CapsuleColliderComponent*>(this))
        {
            float radius = capsule->getRadius();
            if (ImGui::DragFloat("Radius", &radius, 0.05f, 0.001f))
                capsule->setRadius(radius);
            float height = capsule->getHeight();
            if (ImGui::DragFloat("Height", &height, 0.05f, 0.002f))
                capsule->setHeight(height);
        }

        auto drawMeshAsset = [this](const std::shared_ptr<const PhysicsMeshAsset>& currentAsset,
            const auto& setAsset)
        {
            if (m_physicsAssetPath.front() == '\0' && currentAsset != nullptr)
            {
                const std::string currentPath = currentAsset->getPath().generic_string();
                const std::size_t length = std::min(currentPath.size(), m_physicsAssetPath.size() - 1);
                std::copy_n(currentPath.data(), length, m_physicsAssetPath.data());
            }
            ImGui::InputText("Physics Asset", m_physicsAssetPath.data(), m_physicsAssetPath.size());
            ImGui::SameLine();
            if (ImGui::Button("Load##PhysicsAsset"))
            {
                std::shared_ptr<const PhysicsMeshAsset> asset = PhysicsMeshAsset::load(m_physicsAssetPath.data());
                if (asset != nullptr)
                    setAsset(std::move(asset));
            }
            if (currentAsset == nullptr)
                ImGui::TextDisabled("Load a cooked .physics asset to enable this collider.");
        };
        if (auto* const mesh = dynamic_cast<MeshColliderComponent*>(this))
            drawMeshAsset(mesh->getPhysicsAsset(),
                [mesh](std::shared_ptr<const PhysicsMeshAsset> asset) { mesh->setPhysicsAsset(std::move(asset)); });
        else if (auto* const convex = dynamic_cast<ConvexColliderComponent*>(this))
            drawMeshAsset(convex->getPhysicsAsset(),
                [convex](std::shared_ptr<const PhysicsMeshAsset> asset) { convex->setPhysicsAsset(std::move(asset)); });
    }

    namespace
    {
        bool isFiniteVector(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool isPositiveVector(const Vector3& value) noexcept
        {
            return isFiniteVector(value) && value.x > 0.0f && value.y > 0.0f && value.z > 0.0f;
        }

        bool makeProjectRelativePath(
            const std::filesystem::path& assetPath,
            std::filesystem::path& relativePath)
        {
            std::error_code error;
            const std::filesystem::path projectRoot = std::filesystem::current_path(error);
            if (error)
            {
                LOG_ERROR("[ColliderComponent] Cannot determine the project root for a Physics Asset path.");
                return false;
            }

            const std::filesystem::path absolutePath =
                std::filesystem::absolute(assetPath, error).lexically_normal();
            if (error)
            {
                LOG_ERROR("[ColliderComponent] Cannot normalize the Physics Asset path: {}", assetPath.string());
                return false;
            }

            relativePath = absolutePath.lexically_relative(projectRoot.lexically_normal());
            if (relativePath.empty() || relativePath.is_absolute()
                || *relativePath.begin() == "..")
            {
                LOG_ERROR("[ColliderComponent] Physics Assets must be inside the project directory: {}",
                    assetPath.string());
                return false;
            }
            return true;
        }

        bool isSafeProjectRelativePath(const std::filesystem::path& path)
        {
            if (path.empty() || path.is_absolute() || path.has_root_name())
                return false;
            const std::filesystem::path normalized = path.lexically_normal();
            return !normalized.empty() && *normalized.begin() != "..";
        }
    }

    std::uint32_t ColliderComponent::getPayloadVersion() const noexcept
    {
        return Serialization::CURRENT_PHYSICS_COMPONENT_VERSION;
    }

    bool ColliderComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder(192);
        const Vector3& center = getCenter();
        const Serialization::Vec3 serializedCenter(center.x, center.y, center.z);
        Serialization::Vec3 size(0.0f, 0.0f, 0.0f);
        float radius = 0.0f;
        float height = 0.0f;
        std::filesystem::path physicsAssetPath;
        if (const auto* const box = dynamic_cast<const BoxColliderComponent*>(this))
        {
            const Vector3& boxSize = box->getSize();
            size = Serialization::Vec3(boxSize.x, boxSize.y, boxSize.z);
        }
        else if (const auto* const sphere = dynamic_cast<const SphereColliderComponent*>(this))
            radius = sphere->getRadius();
        else if (const auto* const capsule = dynamic_cast<const CapsuleColliderComponent*>(this))
        {
            radius = capsule->getRadius();
            height = capsule->getHeight();
        }
        else if (const auto* const mesh = dynamic_cast<const MeshColliderComponent*>(this))
        {
            if (mesh->getPhysicsAsset() == nullptr || mesh->getPhysicsAsset()->getPath().empty())
            {
                LOG_ERROR("[ColliderComponent] Save the cooked Mesh Collider Physics Asset before saving the Scene or Prefab.");
                return false;
            }
            physicsAssetPath = mesh->getPhysicsAsset()->getPath();
        }
        else if (const auto* const convex = dynamic_cast<const ConvexColliderComponent*>(this))
        {
            if (convex->getPhysicsAsset() == nullptr || convex->getPhysicsAsset()->getPath().empty())
            {
                LOG_ERROR("[ColliderComponent] Save the cooked Convex Collider Physics Asset before saving the Scene or Prefab.");
                return false;
            }
            physicsAssetPath = convex->getPhysicsAsset()->getPath();
        }
        else
        {
            LOG_ERROR("[ColliderComponent] Cannot serialize an unsupported Collider type.");
            return false;
        }
        if (!physicsAssetPath.empty())
        {
            std::filesystem::path relativePath;
            if (!makeProjectRelativePath(physicsAssetPath, relativePath))
                return false;
            physicsAssetPath = std::move(relativePath);
        }

        const PhysicsMaterial& material = getMaterial();
        const auto root = Serialization::CreateColliderComponentPayload(builder,
            &serializedCenter,
            material.getStaticFriction(),
            material.getDynamicFriction(),
            material.getRestitution(),
            static_cast<std::uint32_t>(getLayer()),
            getCollisionMask(),
            isTrigger(),
            &size,
            radius,
            height,
            physicsAssetPath.empty() ? 0 : builder.CreateString(physicsAssetPath.generic_string()));
        PhysicsSerialization::writePayload(builder, root, "CLCP", payload);
        return true;
    }

    bool ColliderComponent::deserializePayload(
        const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        if (version == 0 && payload.empty())
        {
            if (dynamic_cast<MeshColliderComponent*>(this) != nullptr
                || dynamic_cast<ConvexColliderComponent*>(this) != nullptr)
            {
                LOG_ERROR("[ColliderComponent] Legacy serialized Mesh Collider data has no Physics Asset reference.");
                return false;
            }
            return true;
        }
        const Serialization::ColliderComponentPayload* source = nullptr;
        if (version < Serialization::MINIMUM_SUPPORTED_PHYSICS_COMPONENT_VERSION
            || version > Serialization::CURRENT_PHYSICS_COMPONENT_VERSION
            || !PhysicsSerialization::readPayload(payload, "CLCP", source)
            || source->center() == nullptr)
            return false;

        const Vector3 center(source->center()->x(), source->center()->y(), source->center()->z());
        PhysicsMaterial material;
        if (!isFiniteVector(center) || !material.setStaticFriction(source->static_friction())
            || !material.setDynamicFriction(source->dynamic_friction())
            || !material.setRestitution(source->restitution())
            || source->layer() >= 32)
            return false;

        auto* const box = dynamic_cast<BoxColliderComponent*>(this);
        auto* const sphere = dynamic_cast<SphereColliderComponent*>(this);
        auto* const capsule = dynamic_cast<CapsuleColliderComponent*>(this);
        auto* const mesh = dynamic_cast<MeshColliderComponent*>(this);
        auto* const convex = dynamic_cast<ConvexColliderComponent*>(this);
        std::shared_ptr<const PhysicsMeshAsset> meshAsset;
        if (box != nullptr)
        {
            if (source->size() == nullptr
                || !isPositiveVector(Vector3(source->size()->x(), source->size()->y(), source->size()->z())))
                return false;
        }
        else if (sphere != nullptr)
        {
            if (!std::isfinite(source->radius()) || source->radius() <= 0.0f)
                return false;
        }
        else if (capsule != nullptr)
        {
            if (!std::isfinite(source->radius()) || !std::isfinite(source->height())
                || source->radius() <= 0.0f || source->height() <= source->radius() * 2.0f)
                return false;
        }
        else if (mesh != nullptr || convex != nullptr)
        {
            if (source->physics_asset_path() == nullptr || source->physics_asset_path()->empty()
                || source->is_trigger())
                return false;
            std::filesystem::path assetPath(source->physics_asset_path()->str());
            if (assetPath.is_absolute())
            {
                std::filesystem::path relativePath;
                if (!makeProjectRelativePath(assetPath, relativePath))
                    return false;
                assetPath = std::move(relativePath);
            }
            else if (!isSafeProjectRelativePath(assetPath))
            {
                LOG_ERROR("[ColliderComponent] Serialized Physics Asset path must be project-relative.");
                return false;
            }
            meshAsset = PhysicsMeshAsset::load(assetPath);
            const PhysicsMeshType requiredType = mesh != nullptr
                ? PhysicsMeshType::Triangle : PhysicsMeshType::Convex;
            if (meshAsset == nullptr || meshAsset->getType() != requiredType)
            {
                LOG_ERROR("[ColliderComponent] Failed to load the serialized Physics Mesh Asset.");
                return false;
            }
        }
        else
        {
            return false;
        }
        if (source->is_trigger() && !supportsTrigger())
            return false;

        if (mesh != nullptr)
            mesh->setPhysicsAsset(meshAsset);
        else if (convex != nullptr)
            convex->setPhysicsAsset(meshAsset);
        if (capsule != nullptr)
        {
            capsule->setRadius(std::min(capsule->getRadius(), source->radius()));
            capsule->setHeight(source->height());
            capsule->setRadius(source->radius());
        }
        setCenter(center);
        setMaterial(material);
        setLayer(static_cast<PhysicsLayer>(source->layer()));
        setCollisionMask(source->collision_mask());
        setIsTrigger(source->is_trigger());
        if (box != nullptr)
            box->setSize(Vector3(source->size()->x(), source->size()->y(), source->size()->z()));
        else if (sphere != nullptr)
            sphere->setRadius(source->radius());
        return true;
    }

    void ColliderComponent::onPayloadRestored()
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
        {
            if (!world->createCollider(*this))
                LOG_ERROR("[ColliderComponent] Failed to create the Collider after payload restoration.");
        }
    }

    void ColliderComponent::setIsTrigger(const bool enabled) noexcept
    {
        if (enabled && !supportsTrigger())
        {
            LOG_ERROR("[ColliderComponent] This Collider type cannot be used as a trigger.");
            return;
        }
        if (m_isTrigger == enabled)
            return;
        m_isTrigger = enabled;
        refreshGeometry();
    }

    void ColliderComponent::setCenter(const Vector3& center) noexcept
    {
        if (!isFiniteVector(center))
        {
            LOG_ERROR("[ColliderComponent] Center must be finite.");
            return;
        }
        m_center = center;
        refreshGeometry();
    }

    void ColliderComponent::setMaterial(const PhysicsMaterial& material) noexcept
    {
        m_material = material;
        refreshGeometry();
    }

    void ColliderComponent::setLayer(const PhysicsLayer layer) noexcept
    {
        if (physicsLayerBit(layer) == 0)
        {
            LOG_ERROR("[ColliderComponent] Physics layer index must be in [0, 31].");
            return;
        }
        if (m_layer == layer)
            return;
        m_layer = layer;
        refreshGeometry();
    }

    void ColliderComponent::setCollisionMask(const PhysicsLayerMask mask) noexcept
    {
        if (m_collisionMask == mask)
            return;
        m_collisionMask = mask;
        refreshGeometry();
    }

    void ColliderComponent::onEnable()
    {
        PhysicsWorld* const world = PhysicsSystem::instance().getWorld();
        if (world == nullptr || !world->createCollider(*this))
            LOG_ERROR("[ColliderComponent] Failed to create the PhysX Collider.");
    }

    void ColliderComponent::onDisable()
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->destroyCollider(*this);
    }

    void ColliderComponent::onDestroy()
    {
        onDisable();
    }

    void ColliderComponent::refreshGeometry() noexcept
    {
        if (PhysicsWorld* const world = PhysicsSystem::instance().getWorld())
            world->updateCollider(*this);
    }
} // namespace Engine
