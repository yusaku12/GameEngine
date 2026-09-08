#include "Pch.h"
#include "Core\Prefab\PrefabInstance.h"

namespace Engine
{
    bool PrefabInstance::revert(Scene& scene)
    {
        if (!isValid())
            return false;
        m_root->destroy();
        scene.processDestroyQueue();
        m_root = m_prefab->instantiate(scene);
        return m_root != nullptr;
    }
} // namespace Engine