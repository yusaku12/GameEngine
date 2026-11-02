#pragma once

#include "Assets\PostProcess\PostProcessEffectAsset.h"
#include "Core\CoreDefines.h"
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace Engine
{
    /**
     * @brief Loads saved effect assets and transactionally publishes compiled, immutable shader snapshots.
     * @thread_safety Thread-safe for registry access. Compilation and asset updates serialize per manager.
     */
    class PostProcessEffectAssetManager
    {
    public:
        static PostProcessEffectAssetManager& instance() noexcept;
        GE_DISABLE_COPY_AND_MOVE(PostProcessEffectAssetManager);

        /** @brief Loads an asset and its previously compiled bytecode; no HLSL compilation is performed. */
        PostProcessEffectHandle load(const std::filesystem::path& path, ObjectGUID expectedGuid = {});
        /** @brief Creates and compiles an in-memory asset. Save As is required before Profile serialization. */
        PostProcessEffectHandle create(PostProcessEffectAsset asset);
        /** @brief Compiles a candidate before replacing the registered immutable snapshot. */
        bool update(PostProcessEffectHandle handle, PostProcessEffectAsset asset);
        /** @brief Atomically writes the asset descriptor; shader bytecode remains in the generated cache. */
        bool save(PostProcessEffectHandle handle, const std::filesystem::path& path = {});
        /** @brief Reloads the descriptor and bytecode; failures preserve the previous snapshot. */
        bool reload(PostProcessEffectHandle handle);
        /** @brief Returns a lifetime-independent immutable asset snapshot or null for a stale handle. */
        std::shared_ptr<const PostProcessEffectAsset> get(PostProcessEffectHandle handle) const;
        std::filesystem::path getPath(PostProcessEffectHandle handle) const;
        PostProcessEffectHandle findByGuid(ObjectGUID guid) const;
        std::vector<PostProcessEffectHandle> getAllHandles() const;
        bool unload(PostProcessEffectHandle handle);
        void clear();
        /** @brief Logs each stale generation-checked reference once per manager lifetime. */
        void reportStaleHandleOnce(PostProcessEffectHandle handle) const;

    private:
        struct Entry
        {
            std::shared_ptr<const PostProcessEffectAsset> asset;
            std::filesystem::path path;
            std::uint32_t generation = 0;
            bool dirty = false;
        };
        PostProcessEffectAssetManager() = default;
        static std::filesystem::path normalizePath(const std::filesystem::path& path);
        Entry* findEntry(PostProcessEffectHandle handle) noexcept;
        const Entry* findEntry(PostProcessEffectHandle handle) const noexcept;
        PostProcessEffectHandle add(PostProcessEffectAsset asset, std::filesystem::path path);
        bool compile(PostProcessEffectAsset& asset) const;
        bool loadCompiled(PostProcessEffectAsset& asset) const;

        mutable std::mutex m_mutex;
        std::vector<Entry> m_entries;
        mutable std::unordered_set<std::uint64_t> m_reportedStaleHandles;
        std::uint32_t m_nextGeneration = 1;
    };

    /** @brief Appends a validated immutable shader snapshot; a stale asset is diagnosed once and skipped. */
    bool appendPostProcessEffectSnapshot(PostProcessCameraSnapshot& destination,
        PostProcessEffectHandle handle, const std::array<float, 16>& parameters, float contribution) noexcept;
}
