#include "Pch.h"
#include "Assets\PostProcess\PostProcessProfileManager.h"
#include "Assets\PostProcess\PostProcessEffectAssetManager.h"
#include "Assets\PostProcess\Serialization\PostProcessProfileSerializer.h"
#include "Core\Containers\String.h"
#include "Core\Logging\Logger.h"
#include "Core\Math\MathFunction.h"

namespace Engine
{
    namespace
    {
        bool samePath(const std::filesystem::path& left, const std::filesystem::path& right) noexcept
        {
            return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
        }

        bool validateProfile(const PostProcessProfile& profile)
        {
            if (profile.name.empty() || profile.name.find('\0') != std::string::npos)
            {
                LOG_ERROR("[PostProcess] Profile name must be nonempty and contain no null characters.");
                return false;
            }
            if (!validatePostProcessSettings(profile.settings))
                return false;
            auto& effects = PostProcessEffectAssetManager::instance();
            for (const auto& instance : profile.customEffects)
            {
                const auto asset = effects.get(instance.effect);
                if (!asset || asset->parameters.size() > instance.parameters.size())
                {
                    LOG_ERROR("[PostProcess] Profile contains a stale or invalid custom Effect.");
                    return false;
                }
                for (std::size_t index = 0; index < asset->parameters.size(); ++index)
                {
                    const auto& definition = asset->parameters[index];
                    const float value = instance.parameters[index];
                    if (!std::isfinite(value) || value < definition.minimum || value > definition.maximum)
                    {
                        LOG_ERROR("[PostProcess] Effect '{}' parameter '{}' is outside [{}, {}].",
                            asset->name, definition.name, definition.minimum, definition.maximum);
                        return false;
                    }
                }
            }
            return true;
        }
    }

    PostProcessProfileManager& PostProcessProfileManager::instance() noexcept
    {
        static PostProcessProfileManager manager;
        return manager;
    }

    std::filesystem::path PostProcessProfileManager::normalizePath(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto result = path.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(path, error);
        if (result.empty() || error)
        {
            LOG_ERROR("[PostProcess] Invalid Profile path '{}': {}.", String::fromWide(path.c_str()).view(), error.message());
            return {};
        }
        return result;
    }

    PostProcessProfileManager::Entry* PostProcessProfileManager::findEntry(const PostProcessProfileHandle handle) noexcept
    {
        if (!handle.isValid() || handle.index >= m_entries.size())
            return nullptr;
        auto& entry = m_entries[handle.index];
        return entry.profile && entry.generation == handle.generation ? &entry : nullptr;
    }

    const PostProcessProfileManager::Entry* PostProcessProfileManager::findEntry(const PostProcessProfileHandle handle) const noexcept
    {
        if (!handle.isValid() || handle.index >= m_entries.size())
            return nullptr;
        const auto& entry = m_entries[handle.index];
        return entry.profile && entry.generation == handle.generation ? &entry : nullptr;
    }

    PostProcessProfileHandle PostProcessProfileManager::add(PostProcessProfile profile, std::filesystem::path path)
    {
        if (m_nextGeneration == 0 || m_entries.size() >= UINT32_MAX)
        {
            LOG_ERROR("[PostProcess] Profile handle capacity exhausted.");
            return {};
        }
        const auto snapshot = std::make_shared<const PostProcessProfile>(std::move(profile));
        for (std::size_t index = 0; index < m_entries.size(); ++index)
        {
            if (!m_entries[index].profile)
            {
                const bool dirty = path.empty();
                m_entries[index] = { snapshot, std::move(path), m_nextGeneration++, dirty };
                return { static_cast<std::uint32_t>(index), m_entries[index].generation };
            }
        }
        const auto index = static_cast<std::uint32_t>(m_entries.size());
        const bool dirty = path.empty();
        m_entries.push_back({ snapshot, std::move(path), m_nextGeneration++, dirty });
        return { index, m_entries.back().generation };
    }

    PostProcessProfileHandle PostProcessProfileManager::load(const std::filesystem::path& path, const ObjectGUID expectedGuid)
    {
        const auto normalized = normalizePath(path);
        if (normalized.empty())
            return {};
        const std::scoped_lock lock(m_mutex);
        for (std::size_t index = 0; index < m_entries.size(); ++index)
        {
            const auto& entry = m_entries[index];
            if (entry.profile && !entry.path.empty() && samePath(entry.path, normalized))
            {
                if (expectedGuid.isValid() && entry.profile->guid != expectedGuid)
                {
                    LOG_ERROR("[PostProcess] Profile GUID does not match asset reference: {}.", String::fromWide(normalized.c_str()).view());
                    return {};
                }
                return { static_cast<std::uint32_t>(index), entry.generation };
            }
        }
        PostProcessProfile profile;
        if (!Serialization::PostProcessProfileSerializer{}.load(normalized, profile))
            return {};
        if (expectedGuid.isValid() && profile.guid != expectedGuid)
        {
            LOG_ERROR("[PostProcess] Profile GUID does not match asset reference: {}.", String::fromWide(normalized.c_str()).view());
            return {};
        }
        for (const auto& entry : m_entries)
            if (entry.profile && entry.profile->guid == profile.guid)
            {
                LOG_ERROR("[PostProcess] Profile GUID is already registered from another path: {}.", String::fromWide(normalized.c_str()).view());
                return {};
            }
        return add(std::move(profile), normalized);
    }

    PostProcessProfileHandle PostProcessProfileManager::create(PostProcessProfile profile)
    {
        if (!validateProfile(profile))
            return {};
        if (!profile.guid.isValid())
            profile.guid = ObjectGUID::generate();
        const std::scoped_lock lock(m_mutex);
        for (const auto& entry : m_entries)
            if (entry.profile && entry.profile->guid == profile.guid)
            {
                LOG_ERROR("[PostProcess] Cannot create a duplicate Profile GUID.");
                return {};
            }
        return add(std::move(profile), {});
    }

    bool PostProcessProfileManager::update(const PostProcessProfileHandle handle, PostProcessProfile profile)
    {
        if (!validateProfile(profile))
            return false;
        const std::scoped_lock lock(m_mutex);
        auto* const entry = findEntry(handle);
        if (entry == nullptr)
        {
            LOG_ERROR("[PostProcess] Cannot update an invalid Profile handle.");
            return false;
        }
        profile.guid = entry->profile->guid;
        entry->profile = std::make_shared<const PostProcessProfile>(std::move(profile));
        entry->dirty = true;
        return true;
    }

    bool PostProcessProfileManager::save(const PostProcessProfileHandle handle, const std::filesystem::path& path)
    {
        auto target = path.empty() ? std::filesystem::path{} : normalizePath(path);
        if (!path.empty() && target.empty())
            return false;
        const std::scoped_lock lock(m_mutex);
        auto* const entry = findEntry(handle);
        if (entry == nullptr)
        {
            LOG_ERROR("[PostProcess] Cannot save an invalid Profile handle.");
            return false;
        }
        if (path.empty())
            target = entry->path;
        if (target.empty())
        {
            LOG_ERROR("[PostProcess] Unsaved Profile requires Save As.");
            return false;
        }
        for (const auto& other : m_entries)
            if (&other != entry && other.profile && !other.path.empty() && samePath(other.path, target))
            {
                LOG_ERROR("[PostProcess] Save path belongs to another Profile: {}.", String::fromWide(target.c_str()).view());
                return false;
            }
        if (!Serialization::PostProcessProfileSerializer{}.save(target, *entry->profile))
            return false;
        entry->path = std::move(target);
        entry->dirty = false;
        return true;
    }

    bool PostProcessProfileManager::reload(const PostProcessProfileHandle handle)
    {
        const std::scoped_lock lock(m_mutex);
        auto* const entry = findEntry(handle);
        if (entry == nullptr || entry->path.empty())
        {
            LOG_ERROR("[PostProcess] Cannot reload an invalid or unsaved Profile.");
            return false;
        }
        PostProcessProfile loaded;
        if (!Serialization::PostProcessProfileSerializer{}.load(entry->path, loaded))
            return false;
        if (loaded.guid != entry->profile->guid)
        {
            LOG_ERROR("[PostProcess] Reload would replace the Profile identity.");
            return false;
        }
        entry->profile = std::make_shared<const PostProcessProfile>(std::move(loaded));
        entry->dirty = false;
        return true;
    }

    std::shared_ptr<const PostProcessProfile> PostProcessProfileManager::get(const PostProcessProfileHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        const auto* const entry = findEntry(handle);
        return entry ? entry->profile : nullptr;
    }

    std::filesystem::path PostProcessProfileManager::getPath(const PostProcessProfileHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        const auto* const entry = findEntry(handle);
        return entry ? entry->path : std::filesystem::path{};
    }

    PostProcessProfileHandle PostProcessProfileManager::findByGuid(const ObjectGUID guid) const
    {
        const std::scoped_lock lock(m_mutex);
        for (std::size_t index = 0; index < m_entries.size(); ++index)
            if (m_entries[index].profile && m_entries[index].profile->guid == guid)
                return { static_cast<std::uint32_t>(index), m_entries[index].generation };
        return {};
    }

    bool PostProcessProfileManager::isDirty(const PostProcessProfileHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        const auto* const entry = findEntry(handle);
        return entry && entry->dirty;
    }

    std::vector<PostProcessProfileHandle> PostProcessProfileManager::getAllHandles() const
    {
        const std::scoped_lock lock(m_mutex);
        std::vector<PostProcessProfileHandle> handles;
        for (std::size_t index = 0; index < m_entries.size(); ++index)
            if (m_entries[index].profile)
                handles.push_back({ static_cast<std::uint32_t>(index), m_entries[index].generation });
        return handles;
    }

    bool PostProcessProfileManager::unload(const PostProcessProfileHandle handle)
    {
        const std::scoped_lock lock(m_mutex);
        auto* const entry = findEntry(handle);
        if (entry == nullptr)
        {
            LOG_ERROR("[PostProcess] Cannot unload an invalid Profile handle.");
            return false;
        }
        entry->profile.reset();
        entry->path.clear();
        return true;
    }

    void PostProcessProfileManager::clear()
    {
        const std::scoped_lock lock(m_mutex);
        m_entries.clear();
    }
}
