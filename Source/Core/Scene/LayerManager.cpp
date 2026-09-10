#include "Pch.h"
#include "Core\Scene\LayerManager.h"

namespace Engine
{
    LayerManager& LayerManager::instance() noexcept
    {
        static LayerManager manager;
        return manager;
    }

    LayerManager::LayerManager()
    {
        m_names[0] = "Default";
        m_registeredMask = 1u;
    }

    LayerID LayerManager::registerLayer(std::string name)
    {
        if (name.empty())
            return 0;
        for (LayerID layer = 0; layer < m_names.size(); ++layer)
        {
            if ((m_registeredMask & (1u << layer)) != 0 && m_names[layer] == name)
                return layer;
        }
        for (LayerID layer = 0; layer < m_names.size(); ++layer)
        {
            if ((m_registeredMask & (1u << layer)) == 0)
            {
                m_names[layer] = std::move(name);
                m_registeredMask |= 1u << layer;
                return layer;
            }
        }
        return 0;
    }

    LayerID LayerManager::find(const std::string_view name) const noexcept
    {
        for (LayerID layer = 0; layer < m_names.size(); ++layer)
        {
            if ((m_registeredMask & (1u << layer)) != 0 && m_names[layer] == name)
                return layer;
        }
        return 0;
    }

    std::string_view LayerManager::getName(const LayerID layer) const noexcept
    {
        return contains(layer) ? m_names[layer] : std::string_view{};
    }

    bool LayerManager::contains(const LayerID layer) const noexcept
    {
        return layer < m_names.size() && (m_registeredMask & (1u << layer)) != 0;
    }
} // namespace Engine