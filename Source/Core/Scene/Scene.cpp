#include "Pch.h"
#include "Core\Scene\Scene.h"

namespace Engine
{
    Scene::Scene(const ObjectGUID guid, std::string name)
        : m_guid(guid), m_name(std::move(name))
    {
    }
} // namespace Engine