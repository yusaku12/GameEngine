#pragma once

#include "Assets\Effects\ParticleEffectAsset.h"
#include "Core\CoreDefines.h"
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace Engine
{
    /**
     * @brief Registry of immutable particle effect configurations.
     * @thread_safety Thread-safe. Published snapshots remain valid after updates.
     */
    class ParticleEffectAssetManager
    {
    public:
        /** @brief Returns the shared registry. */
        static ParticleEffectAssetManager& instance() noexcept;
        GE_DISABLE_COPY_AND_MOVE(ParticleEffectAssetManager);

        /** @brief Creates an unsaved asset from validated settings. */
        ParticleEffectAssetHandle create(const ParticleEffectSettings& settings);
        /** @brief Loads an asset and optionally verifies its persistent identity. */
        ParticleEffectAssetHandle load(const std::filesystem::path& path, ObjectGUID expectedGuid = {});
        /** @brief Replaces a published configuration. Existing snapshots remain unchanged. */
        bool update(ParticleEffectAssetHandle handle, const ParticleEffectSettings& settings);
        /** @brief Saves atomically to the registered path, or assigns a path to an unsaved asset. */
        bool save(ParticleEffectAssetHandle handle, const std::filesystem::path& path = {});
        /** @brief Reloads the registered file without changing identity. */
        bool reload(ParticleEffectAssetHandle handle);
        /** @brief Returns an immutable asset snapshot or null for a stale handle. */
        std::shared_ptr<const ParticleEffectAsset> get(ParticleEffectAssetHandle handle) const;
        /** @brief Returns the registered path, or an empty path for unsaved assets. */
        std::filesystem::path getPath(ParticleEffectAssetHandle handle) const;
        /** @brief Returns whether the asset has unsaved changes. */
        bool isDirty(ParticleEffectAssetHandle handle) const;
        /** @brief Lists the registered handles. */
        std::vector<ParticleEffectAssetHandle> getAllHandles() const;
        /** @brief Clears registrations after all scene references are destroyed. */
        void clear();

    private:
        struct Entry
        {
            std::shared_ptr<const ParticleEffectAsset> asset;
            std::filesystem::path path;
            std::uint32_t generation = 0;
            bool dirty = false;
        };
        ParticleEffectAssetManager() = default;
        Entry* findEntry(ParticleEffectAssetHandle handle) noexcept;
        const Entry* findEntry(ParticleEffectAssetHandle handle) const noexcept;
        ParticleEffectAssetHandle add(ParticleEffectAsset asset, std::filesystem::path path);
        mutable std::mutex m_mutex;
        std::vector<Entry> m_entries;
        std::uint32_t m_nextGeneration = 1;
    };
}
