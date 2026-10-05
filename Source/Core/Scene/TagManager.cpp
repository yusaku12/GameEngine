#include "Pch.h"
#include "Core\Logging\Logging.h"
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
            return INVALID_TAG_ID;
        if (const auto found = m_ids.find(name); found != m_ids.end())
            return found->second;
        if (m_names.size() > (std::numeric_limits<TagID>::max)()
            || m_names.size() == m_names.max_size())
        {
            LOG_ERROR("Cannot register Tag because the Tag ID capacity has been exhausted.");
            return INVALID_TAG_ID;
        }

        const TagID id = static_cast<TagID>(m_names.size());
        const auto [entry, inserted] = m_ids.emplace(name, id);
        if (!inserted)
            return entry->second;

        try
        {
            m_names.push_back(std::move(name));
        }
        catch (...)
        {
            m_ids.erase(entry);
            throw;
        }
        return id;
    }

    TagID TagManager::find(const std::string_view name) const noexcept
    {
        for (std::size_t index = 0; index < m_names.size(); ++index)
        {
            if (m_names[index] == name)
                return static_cast<TagID>(index);
        }
        return INVALID_TAG_ID;
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