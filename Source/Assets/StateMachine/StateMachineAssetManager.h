#pragma once

#include "Assets\StateMachine\StateMachineAsset.h"
#include "Core\CoreDefines.h"
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace Engine
{
    /**
     * @brief Loads and publishes immutable State Machine asset snapshots.
     * @thread_safety Thread-safe. Returned snapshots remain valid after updates or unloads.
     */
    class StateMachineAssetManager
    {
    public:
        static StateMachineAssetManager& instance() noexcept;
        GE_DISABLE_COPY_AND_MOVE(StateMachineAssetManager);

        /** @brief Loads an asset and optionally verifies its persistent identity. */
        StateMachineAssetHandle load(const std::filesystem::path& path, ObjectGUID expectedGuid = {});
        /** @brief Registers an in-memory asset, generating an identity when needed. */
        StateMachineAssetHandle create(StateMachineAsset asset);
        /** @brief Publishes a validated immutable snapshot while retaining its GUID. */
        bool update(StateMachineAssetHandle handle, StateMachineAsset asset);
        /** @brief Atomically saves to the registered path; an explicit path is only for an unsaved asset. */
        bool save(StateMachineAssetHandle handle, const std::filesystem::path& path = {});
        /** @brief Returns a lifetime-independent immutable snapshot. */
        std::shared_ptr<const StateMachineAsset> get(StateMachineAssetHandle handle) const;
        /** @brief Returns the normalized on-disk path, or empty for an unsaved asset. */
        std::filesystem::path getPath(StateMachineAssetHandle handle) const;
        /** @brief Finds a currently registered asset by persistent identity. */
        StateMachineAssetHandle findByGuid(ObjectGUID guid) const;
        /** @brief Whether an asset contains edits not yet saved to disk. */
        bool isDirty(StateMachineAssetHandle handle) const;

    private:
        struct Entry
        {
            std::shared_ptr<const StateMachineAsset> asset;
            std::filesystem::path path;
            std::uint32_t generation = 0;
            bool dirty = false;
        };

        StateMachineAssetManager() = default;
        static std::filesystem::path normalizePath(const std::filesystem::path& path);
        Entry* findEntry(StateMachineAssetHandle handle) noexcept;
        const Entry* findEntry(StateMachineAssetHandle handle) const noexcept;
        StateMachineAssetHandle add(StateMachineAsset asset, std::filesystem::path path);

        mutable std::mutex m_mutex;
        std::vector<Entry> m_entries;
        std::uint32_t m_nextGeneration = 1;
    };
}
