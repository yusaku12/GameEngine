#include "Pch.h"
#include "Assets\StateMachine\StateMachineAssetManager.h"
#include "Assets\StateMachine\Serialization\StateMachineAssetSerializer.h"
#include "Core\Containers\String.h"
#include "Core\Logging\Logger.h"

namespace Engine
{
    namespace
    {
        bool samePath(const std::filesystem::path& left, const std::filesystem::path& right) noexcept
        {
            return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
        }
    }

    StateMachineAssetManager& StateMachineAssetManager::instance() noexcept
    {
        static StateMachineAssetManager manager;
        return manager;
    }

    std::filesystem::path StateMachineAssetManager::normalizePath(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto normalized = path.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(path, error);
        if (normalized.empty() || error)
        {
            LOG_ERROR("[StateMachine] Invalid asset path '{}': {}.", String::fromWide(path.c_str()).view(),
                error.message());
            return {};
        }
        return normalized;
    }

    StateMachineAssetManager::Entry* StateMachineAssetManager::findEntry(const StateMachineAssetHandle handle) noexcept
    {
        if (!handle.isValid() || handle.index >= m_entries.size())
            return nullptr;
        Entry& entry = m_entries[handle.index];
        return entry.asset != nullptr && entry.generation == handle.generation ? &entry : nullptr;
    }

    const StateMachineAssetManager::Entry* StateMachineAssetManager::findEntry(
        const StateMachineAssetHandle handle) const noexcept
    {
        if (!handle.isValid() || handle.index >= m_entries.size())
            return nullptr;
        const Entry& entry = m_entries[handle.index];
        return entry.asset != nullptr && entry.generation == handle.generation ? &entry : nullptr;
    }

    StateMachineAssetHandle StateMachineAssetManager::add(StateMachineAsset asset, std::filesystem::path path)
    {
        if (m_nextGeneration == 0 || m_entries.size() >= StateMachineAssetHandle::INVALID_INDEX)
        {
            LOG_ERROR("[StateMachine] Asset handle capacity exhausted.");
            return {};
        }
        try
        {
            auto snapshot = std::make_shared<const StateMachineAsset>(std::move(asset));
            for (std::size_t index = 0; index < m_entries.size(); ++index)
            {
                if (!m_entries[index].asset)
                {
                    const std::uint32_t generation = m_nextGeneration++;
                    const bool dirty = path.empty();
                    m_entries[index] = { std::move(snapshot), std::move(path), generation, dirty };
                    return { static_cast<std::uint32_t>(index), generation };
                }
            }
            const auto index = static_cast<std::uint32_t>(m_entries.size());
            const std::uint32_t generation = m_nextGeneration++;
            const bool dirty = path.empty();
            m_entries.push_back({ std::move(snapshot), std::move(path), generation, dirty });
            return { index, generation };
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[StateMachine] Failed to allocate an asset registry entry.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[StateMachine] Asset registry capacity exceeded.");
        }
        return {};
    }

    StateMachineAssetHandle StateMachineAssetManager::load(const std::filesystem::path& path,
        const ObjectGUID expectedGuid)
    {
        const std::filesystem::path normalized = normalizePath(path);
        if (normalized.empty())
            return {};
        const std::scoped_lock lock(m_mutex);
        for (std::size_t index = 0; index < m_entries.size(); ++index)
        {
            const Entry& entry = m_entries[index];
            if (entry.asset && !entry.path.empty() && samePath(entry.path, normalized))
            {
                if (expectedGuid.isValid() && entry.asset->guid != expectedGuid)
                {
                    LOG_ERROR("[StateMachine] Asset GUID does not match the saved reference.");
                    return {};
                }
                return { static_cast<std::uint32_t>(index), entry.generation };
            }
        }

        StateMachineAsset asset;
        if (!Serialization::StateMachineAssetSerializer{}.load(normalized, asset))
            return {};
        if ((expectedGuid.isValid() && asset.guid != expectedGuid)
            || std::ranges::any_of(m_entries, [&asset](const Entry& entry)
                {
                    return entry.asset && entry.asset->guid == asset.guid;
                }))
        {
            LOG_ERROR("[StateMachine] Asset identity is mismatched or already registered from another path.");
            return {};
        }
        return add(std::move(asset), normalized);
    }

    StateMachineAssetHandle StateMachineAssetManager::create(StateMachineAsset asset)
    {
        if (!asset.guid.isValid())
            asset.guid = ObjectGUID::generate();
        if (!validateStateMachineAsset(asset))
            return {};
        const std::scoped_lock lock(m_mutex);
        for (const Entry& entry : m_entries)
            if (entry.asset && entry.asset->guid == asset.guid)
            {
                LOG_ERROR("[StateMachine] Cannot register a duplicate asset GUID.");
                return {};
            }
        return add(std::move(asset), {});
    }

    bool StateMachineAssetManager::update(const StateMachineAssetHandle handle, StateMachineAsset asset)
    {
        if (!validateStateMachineAsset(asset))
            return false;
        const std::scoped_lock lock(m_mutex);
        Entry* const entry = findEntry(handle);
        if (entry == nullptr)
        {
            LOG_ERROR("[StateMachine] Cannot update an invalid asset handle.");
            return false;
        }
        asset.guid = entry->asset->guid;
        try
        {
            entry->asset = std::make_shared<const StateMachineAsset>(std::move(asset));
            entry->dirty = true;
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[StateMachine] Failed to allocate an immutable asset snapshot.");
        }
        return false;
    }

    bool StateMachineAssetManager::save(const StateMachineAssetHandle handle, const std::filesystem::path& path)
    {
        std::filesystem::path target = path.empty() ? std::filesystem::path{} : normalizePath(path);
        if (!path.empty() && target.empty())
            return false;

        const std::scoped_lock lock(m_mutex);
        Entry* const entry = findEntry(handle);
        if (entry == nullptr)
        {
            LOG_ERROR("[StateMachine] Cannot save an invalid asset handle.");
            return false;
        }
        if (target.empty())
            target = entry->path;
        if (target.empty())
        {
            LOG_ERROR("[StateMachine] Unsaved asset requires Save As.");
            return false;
        }
        if (!entry->path.empty() && !samePath(entry->path, target))
        {
            LOG_ERROR("[StateMachine] A registered asset cannot be moved by Save As; use its current path.");
            return false;
        }
        for (const Entry& other : m_entries)
            if (&other != entry && other.asset && !other.path.empty() && samePath(other.path, target))
            {
                LOG_ERROR("[StateMachine] Save path belongs to another registered asset.");
                return false;
            }
        if (!Serialization::StateMachineAssetSerializer{}.save(target, *entry->asset))
            return false;
        entry->path = std::move(target);
        entry->dirty = false;
        return true;
    }

    std::shared_ptr<const StateMachineAsset> StateMachineAssetManager::get(const StateMachineAssetHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        const Entry* const entry = findEntry(handle);
        return entry != nullptr ? entry->asset : nullptr;
    }

    std::filesystem::path StateMachineAssetManager::getPath(const StateMachineAssetHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        const Entry* const entry = findEntry(handle);
        return entry != nullptr ? entry->path : std::filesystem::path{};
    }

    StateMachineAssetHandle StateMachineAssetManager::findByGuid(const ObjectGUID guid) const
    {
        if (!guid.isValid())
            return {};
        const std::scoped_lock lock(m_mutex);
        for (std::size_t index = 0; index < m_entries.size(); ++index)
            if (m_entries[index].asset && m_entries[index].asset->guid == guid)
                return { static_cast<std::uint32_t>(index), m_entries[index].generation };
        return {};
    }

    bool StateMachineAssetManager::isDirty(const StateMachineAssetHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        const Entry* const entry = findEntry(handle);
        return entry != nullptr && entry->dirty;
    }
}
