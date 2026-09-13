#include "Pch.h"
#include "Assets\Material\MaterialManager.h"
#include "Assets\Material\Serialization\MaterialSerializer.h"

namespace Engine
{
    MaterialManager& MaterialManager::instance() noexcept
    {
        static MaterialManager manager;
        return manager;
    }

    MaterialManager::MaterialManager()
    {
        initializeBuiltIns();
    }

    MaterialHandle MaterialManager::load(const std::filesystem::path& path)
    {
        const std::filesystem::path normalizedPath = normalizePath(path);
        if (normalizedPath.empty()) {
            LOG_ERROR("[MaterialManager] Material Pathが無効です");
            return MaterialHandle::Invalid();
        }
        {
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_pathCache.find(normalizedPath); found != m_pathCache.end())
                return found->second;
        }

        MaterialAsset material;
        if (!Serialization::MaterialSerializer{}.load(normalizedPath, material)) {
            LOG_ERROR("[MaterialManager] Materialのロードに失敗しました: {}", normalizedPath.string());
            return MaterialHandle::Invalid();
        }
        return create(std::move(material), normalizedPath);
    }

    MaterialHandle MaterialManager::create(MaterialAsset material, const std::filesystem::path& cacheKey)
    {
        const std::filesystem::path normalizedKey = cacheKey.empty() ? std::filesystem::path{} : normalizePath(cacheKey);
        if (!cacheKey.empty() && normalizedKey.empty())
            return MaterialHandle::Invalid();
        if (!material.guid.isValid())
            material.guid = AssetGUID::generate();

        const std::scoped_lock lock(m_mutex);
        if (!normalizedKey.empty()) {
            if (const auto found = m_pathCache.find(normalizedKey); found != m_pathCache.end())
                return found->second;
        }
        if (const auto found = m_guidCache.find(material.guid); found != m_guidCache.end())
            return found->second;
        return addEntry(std::move(material), normalizedKey, false);
    }

    std::shared_ptr<const MaterialAsset> MaterialManager::get(const MaterialHandle handle) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_entries.size())
            return nullptr;
        const Entry& entry = m_entries[handle.index];
        return entry.generation == handle.generation ? entry.resource : nullptr;
    }

    MaterialHandle MaterialManager::findByGuid(const AssetGUID& guid) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_guidCache.find(guid);
        return found == m_guidCache.end() ? MaterialHandle::Invalid() : found->second;
    }

    std::vector<MaterialHandle> MaterialManager::getAllHandles() const
    {
        std::scoped_lock lock(m_mutex);
        std::vector<MaterialHandle> handles;
        handles.reserve(m_entries.size());
        for (std::uint32_t index = 0; index < m_entries.size(); ++index)
        {
            const Entry& entry = m_entries[index];
            if (entry.resource != nullptr)
                handles.push_back({ index, entry.generation });
        }
        return handles;
    }

    MaterialHandle MaterialManager::getDefaultMaterial() const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        return m_defaultMaterial;
    }

    MaterialHandle MaterialManager::getErrorMaterial() const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        return m_errorMaterial;
    }

    void MaterialManager::unload(const MaterialHandle handle) noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_entries.size())
            return;
        Entry& entry = m_entries[handle.index];
        if (entry.generation != handle.generation || entry.resource == nullptr || entry.builtIn)
            return;
        if (!entry.path.empty())
            m_pathCache.erase(entry.path);
        m_guidCache.erase(entry.resource->guid);
        entry.resource.reset();
        entry.path.clear();
        entry.generation = 0;
    }

    void MaterialManager::clear() noexcept
    {
        const std::scoped_lock lock(m_mutex);
        m_entries.clear();
        m_pathCache.clear();
        m_guidCache.clear();
        m_defaultMaterial = MaterialHandle::Invalid();
        m_errorMaterial = MaterialHandle::Invalid();
        initializeBuiltIns();
    }

    std::filesystem::path MaterialManager::normalizePath(const std::filesystem::path& path)
    {
        if (path.empty())
            return {};
        std::error_code error;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
        if (error) {
            error.clear();
            normalized = std::filesystem::absolute(path, error);
        }
        return error ? std::filesystem::path{} : normalized.lexically_normal();
    }

    MaterialHandle MaterialManager::addEntry(MaterialAsset material, const std::filesystem::path& path, const bool builtIn)
    {
        if (m_nextGeneration == 0)
            ++m_nextGeneration;
        const MaterialHandle handle{
            .index = static_cast<std::uint32_t>(m_entries.size()),
            .generation = m_nextGeneration++,
        };
        const AssetGUID guid = material.guid;
        m_entries.push_back(Entry{
            .resource = std::make_shared<const MaterialAsset>(std::move(material)),
            .path = path,
            .generation = handle.generation,
            .builtIn = builtIn,
            });
        if (!path.empty())
            m_pathCache.emplace(path, handle);
        m_guidCache.emplace(guid, handle);
        return handle;
    }

    void MaterialManager::initializeBuiltIns()
    {
        MaterialAsset defaultMaterial;
        defaultMaterial.guid = { 0, 1 };
        defaultMaterial.name = "Default Material";
        m_defaultMaterial = addEntry(std::move(defaultMaterial), {}, true);

        MaterialAsset errorMaterial;
        errorMaterial.guid = { 0, 2 };
        errorMaterial.name = "Error Material";
        errorMaterial.baseColor = Vector4(1.0f, 0.0f, 1.0f, 1.0f);
        errorMaterial.emissiveColor = Vector3(1.0f, 0.0f, 1.0f);
        errorMaterial.emissiveIntensity = 1.0f;
        m_errorMaterial = addEntry(std::move(errorMaterial), {}, true);
    }
} // namespace Engine