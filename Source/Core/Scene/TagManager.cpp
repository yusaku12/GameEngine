#include "Pch.h"
#include "Core\Scene\TagManager.h"

namespace Engine
{
    TagManager& TagManager::instance() noexcept
    {
        static TagManager manager;
        return manager;
    }

    TagManager::TagManager()
    {
        registerTag("Untagged");
    }

    TagID TagManager::registerTag(std::string name)
    {
        if (name.empty())
            return 0;
        if (const auto found = m_ids.find(name); found != m_ids.end())
            return found->second;
        const TagID id = static_cast<TagID>(m_names.size());
        m_ids.emplace(name, id);
        m_names.push_back(std::move(name));
        return id;
    }

    TagID TagManager::find(const std::string_view name) const noexcept
    {
        const auto found = m_ids.find(std::string(name));
        return found == m_ids.end() ? 0 : found->second;
    }

    std::string_view TagManager::getName(const TagID tag) const noexcept
    {
        return tag < m_names.size() ? m_names[tag] : std::string_view{};
    }

    bool TagManager::contains(const TagID tag) const noexcept
    {
        return tag < m_names.size();
    }
} // namespace Engine