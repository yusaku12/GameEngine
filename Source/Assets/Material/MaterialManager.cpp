#include "Pch.h"
#include "Assets\Material\MaterialManager.h"
#include "Assets\Material\Serialization\MaterialSerializer.h"
#include "Graphics\Texture\TextureManager.h"

namespace Engine
{
    namespace
    {
        struct TextureReferencePair
        {
            AssetGUID MaterialTextureReferences::* guid;
            std::filesystem::path MaterialTextureReferences::* path;
        };

        constexpr std::array TEXTURE_REFERENCES = {
            TextureReferencePair{ &MaterialTextureReferences::baseColor, &MaterialTextureReferences::baseColorPath },
            TextureReferencePair{ &MaterialTextureReferences::normal, &MaterialTextureReferences::normalPath },
            TextureReferencePair{ &MaterialTextureReferences::metallicRoughness, &MaterialTextureReferences::metallicRoughnessPath },
            TextureReferencePair{ &MaterialTextureReferences::ambientOcclusion, &MaterialTextureReferences::ambientOcclusionPath },
            TextureReferencePair{ &MaterialTextureReferences::emissive, &MaterialTextureReferences::emissivePath },
        };

        void registerTexturePaths(const MaterialTextureReferences& references)
        {
            TextureManager& textures = TextureManager::instance();
            for (const TextureReferencePair& reference : TEXTURE_REFERENCES)
            {
                const AssetGUID& guid = references.*reference.guid;
                const std::filesystem::path& path = references.*reference.path;
                if (guid.isValid() && !path.empty() && !textures.registerAssetPath(guid, path))
                {
                    LOG_WARNING("[MaterialManager] Texture GUID path registration failed: {}", path.string());
                }
            }
        }

        void fillTexturePaths(MaterialTextureReferences& references)
        {
            TextureManager& textures = TextureManager::instance();
            for (const TextureReferencePair& reference : TEXTURE_REFERENCES)
            {
                std::filesystem::path& path = references.*reference.path;
                if (path.empty())
                    path = textures.getAssetPath(references.*reference.guid);
            }
        }
    }

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
        registerTexturePaths(material.textures);
        const AssetGUID guid = material.guid;
        const std::scoped_lock lock(m_mutex);
        if (const auto found = m_pathCache.find(normalizedPath); found != m_pathCache.end())
            return found->second;
        if (const auto found = m_guidCache.find(guid); found != m_guidCache.end())
        {
            const MaterialHandle handle = found->second;
            if (!handle.isValid() || handle.index >= m_entries.size())
                return MaterialHandle::Invalid();
            Entry& entry = m_entries[handle.index];
            if (entry.generation != handle.generation || entry.resource == nullptr)
                return MaterialHandle::Invalid();
            if (const auto conflict = m_pathCache.find(normalizedPath);
                conflict != m_pathCache.end() && conflict->second != handle)
                return MaterialHandle::Invalid();
            if (!entry.path.empty())
                m_pathCache.erase(entry.path);
            entry.resource = std::make_shared<const MaterialAsset>(std::move(material));
            entry.path = normalizedPath;
            m_pathCache[normalizedPath] = handle;
            return handle;
        }
        return addEntry(std::move(material), normalizedPath, false);
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

    bool MaterialManager::update(const MaterialHandle handle, MaterialAsset material)
    {
        AssetGUID guid;
        {
            const std::scoped_lock lock(m_mutex);
            if (!handle.isValid() || handle.index >= m_entries.size())
                return false;
            const Entry& entry = m_entries[handle.index];
            if (entry.generation != handle.generation || entry.resource == nullptr)
                return false;
            guid = entry.resource->guid;
        }

        material.guid = guid;
        const std::scoped_lock lock(m_mutex);
        Entry& entry = m_entries[handle.index];
        if (entry.generation != handle.generation || entry.resource == nullptr
            || entry.resource->guid != guid)
        {
            return false;
        }
        entry.resource = std::make_shared<const MaterialAsset>(std::move(material));
        return true;
    }

    bool MaterialManager::save(const MaterialHandle handle, const std::filesystem::path& path)
    {
        MaterialAsset material;
        std::shared_ptr<const MaterialAsset> snapshot;
        std::filesystem::path currentPath;
        {
            const std::scoped_lock lock(m_mutex);
            if (!handle.isValid() || handle.index >= m_entries.size())
                return false;
            const Entry& entry = m_entries[handle.index];
            if (entry.generation != handle.generation || entry.resource == nullptr)
                return false;
            snapshot = entry.resource;
            material = *snapshot;
            currentPath = entry.path;
        }

        const std::filesystem::path targetPath = path.empty() ? currentPath : normalizePath(path);
        fillTexturePaths(material.textures);
        if (targetPath.empty() || !Serialization::MaterialSerializer{}.save(targetPath, material))
            return false;
        if (path.empty())
            return true;

        const std::scoped_lock lock(m_mutex);
        Entry& entry = m_entries[handle.index];
        if (entry.generation != handle.generation || entry.resource != snapshot)
            return false;
        if (const auto conflict = m_pathCache.find(targetPath);
            conflict != m_pathCache.end() && conflict->second != handle)
        {
            return false;
        }
        if (!entry.path.empty())
            m_pathCache.erase(entry.path);
        entry.path = targetPath;
        entry.resource = std::make_shared<const MaterialAsset>(std::move(material));
        m_pathCache[targetPath] = handle;
        return true;
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

    std::filesystem::path MaterialManager::getPath(const MaterialHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_entries.size())
            return {};
        const Entry& entry = m_entries[handle.index];
        return entry.generation == handle.generation && entry.resource != nullptr ? entry.path : std::filesystem::path{};
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
        if (m_nextGeneration == 0 || m_entries.size() >= MaterialHandle::INVALID_INDEX)
        {
            LOG_ERROR("[MaterialManager] Material handle space exhausted");
            return MaterialHandle::Invalid();
        }

        const std::uint32_t generation = m_nextGeneration;
        const MaterialHandle handle{
            .index = static_cast<std::uint32_t>(m_entries.size()),
            .generation = generation,
        };
        const AssetGUID guid = material.guid;
        bool entryInserted = false;
        bool pathInserted = false;
        bool guidInserted = false;
        try
        {
            std::shared_ptr<const MaterialAsset> resource =
                std::make_shared<const MaterialAsset>(std::move(material));
            m_entries.reserve(m_entries.size() + 1);
            if (!path.empty())
                m_pathCache.reserve(m_pathCache.size() + 1);
            m_guidCache.reserve(m_guidCache.size() + 1);

            m_entries.push_back(Entry{
                .resource = std::move(resource),
                .path = path,
                .generation = generation,
                .builtIn = builtIn,
                });
            entryInserted = true;

            if (!path.empty())
            {
                const auto [pathEntry, inserted] = m_pathCache.emplace(path, handle);
                GE_UNUSED(pathEntry);
                if (!inserted)
                {
                    m_entries.pop_back();
                    LOG_ERROR("[MaterialManager] Material path is already registered: {}", path.string());
                    return MaterialHandle::Invalid();
                }
                pathInserted = true;
            }

            const auto [guidEntry, inserted] = m_guidCache.emplace(guid, handle);
            GE_UNUSED(guidEntry);
            if (!inserted)
            {
                if (pathInserted)
                    m_pathCache.erase(path);
                m_entries.pop_back();
                LOG_ERROR("[MaterialManager] Material GUID is already registered.");
                return MaterialHandle::Invalid();
            }
            guidInserted = true;
        }
        catch (const std::bad_alloc&)
        {
            if (guidInserted)
                m_guidCache.erase(guid);
            if (pathInserted)
                m_pathCache.erase(path);
            if (entryInserted)
                m_entries.pop_back();
            LOG_ERROR("[MaterialManager] Memory allocation failed while registering a Material.");
            return MaterialHandle::Invalid();
        }
        catch (const std::length_error&)
        {
            if (guidInserted)
                m_guidCache.erase(guid);
            if (pathInserted)
                m_pathCache.erase(path);
            if (entryInserted)
                m_entries.pop_back();
            LOG_ERROR("[MaterialManager] Material registry reached its maximum capacity.");
            return MaterialHandle::Invalid();
        }

        ++m_nextGeneration;
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