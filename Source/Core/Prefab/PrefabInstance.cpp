#include "Pch.h"
#include "Core\Prefab\PrefabInstance.h"

namespace Engine
{
    bool PrefabInstance::revert(Scene& scene)
    {
        if (!isValid())
            return false;

        if (scene.find(m_root->getGUID()) != m_root)
            return false;

        GameObject* const parent = m_root->getParent();
        GameObject* replacement = m_prefab->instantiate(scene);
        if (replacement == nullptr)
            return false;

        if (parent != nullptr && !replacement->setParent(parent, false))
        {
            replacement->destroy();
            scene.processDestroyQueue();
            return false;
        }

        m_root->destroy();
        scene.processDestroyQueue();
        m_root = replacement;
        return true;
    }
} // namespace Engine