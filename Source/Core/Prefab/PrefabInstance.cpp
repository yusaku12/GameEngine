#include "Pch.h"
#include "Core\Prefab\PrefabInstance.h"

namespace Engine
{
    bool PrefabInstance::revert(Scene& scene)
    {
        if (!isValid())
            return false;

        GameObject* replacement = m_prefab->instantiate(scene);
        if (replacement == nullptr)
            return false;

        m_root->destroy();
        scene.processDestroyQueue();
        m_root = replacement;
        return true;
    }
} // namespace Engine