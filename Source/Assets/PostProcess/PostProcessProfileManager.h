#pragma once

#include "Assets\PostProcess\PostProcessProfile.h"
#include "Core\CoreDefines.h"
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace Engine
{
    /**
     * @brief Generation-checked Profile registry publishing immutable snapshots.
     * @thread_safety Thread-safe. Saves/reloads serialize with updates; callers retain snapshots freely.
     */
    class PostProcessProfileManager
    {
    public:
        /** @brief Returns the engine Profile registry. */
        static PostProcessProfileManager& instance() noexcept;
        GE_DISABLE_COPY_AND_MOVE(PostProcessProfileManager);

        /** @brief Loads/caches a Profile. Optional expected GUID is checked before registry mutation. */
        PostProcessProfileHandle load(const std::filesystem::path& path, ObjectGUID expectedGuid = {});
        /** @brief Creates an in-memory Profile; generates a GUID if empty. Failure logs and returns invalid. */
        PostProcessProfileHandle create(PostProcessProfile profile);
        /** @brief Replaces an immutable snapshot after validation; keeps the registered GUID. */
        bool update(PostProcessProfileHandle handle, PostProcessProfile profile);
        /** @brief Atomic save to existing path or Save As. Path conflicts are rejected before writing. */
        bool save(PostProcessProfileHandle handle, const std::filesystem::path& path = {});
        /** @brief Explicitly reloads the registered file; failure retains all current values. */
        bool reload(PostProcessProfileHandle handle);
        /** @brief Gets a lifetime-independent immutable snapshot, or null for a stale/invalid handle. */
        std::shared_ptr<const PostProcessProfile> get(PostProcessProfileHandle handle) const;
        /** @brief Gets the normalized path; unsaved or invalid handles have no path. */
        std::filesystem::path getPath(PostProcessProfileHandle handle) const;
        /** @brief Whether the current snapshot has unsaved edits (including an in-memory asset). */
        bool isDirty(PostProcessProfileHandle handle) const;
        /** @brief Gets a loaded handle by asset identity; not found returns invalid. */
        PostProcessProfileHandle findByGuid(ObjectGUID guid) const;
        /** @brief Gets all currently registered handles for Inspector selection. */
        std::vector<PostProcessProfileHandle> getAllHandles() const;
        /** @brief Removes a registered handle; retained immutable snapshots remain alive. */
        bool unload(PostProcessProfileHandle handle);
        /** @brief Clears registrations without resetting generations. Destroy camera references first. */
        void clear();

    private:
        struct Entry
        {
            std::shared_ptr<const PostProcessProfile> profile;
            std::filesystem::path path;
            std::uint32_t generation = 0;
            bool dirty = false;
        };
        PostProcessProfileManager() = default;
        static std::filesystem::path normalizePath(const std::filesystem::path& path);
        Entry* findEntry(PostProcessProfileHandle handle) noexcept;
        const Entry* findEntry(PostProcessProfileHandle handle) const noexcept;
        PostProcessProfileHandle add(PostProcessProfile profile, std::filesystem::path path);
        mutable std::mutex m_mutex;
        std::vector<Entry> m_entries;
        std::uint32_t m_nextGeneration = 1;
    };
}
