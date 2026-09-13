#include "Pch.h"
#include "Assets\Material\MaterialInstance.h"
#include "Assets\Material\MaterialManager.h"

namespace Engine
{
    MaterialHandle MaterialInstance::createMaterial(std::string name) const
    {
        const std::shared_ptr<const MaterialAsset> parent = MaterialManager::instance().get(m_parent);
        if (parent == nullptr)
            return MaterialHandle::Invalid();

        MaterialAsset material = *parent;
        material.guid = {};
        if (!name.empty())
            material.name = std::move(name);
        else
            material.name += " Instance";
        m_overrides.applyTo(material);
        return MaterialManager::instance().create(std::move(material));
    }
} // namespace Engine