#include "Pch.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Assets\Animation\Serialization\AnimationClipSerializer.h"
#include "Assets\Animation\Serialization\AnimatorControllerSerializer.h"
#include "Assets\Animation\Serialization\SkeletonSerializer.h"

namespace Engine
{
    AnimationAssetManager& AnimationAssetManager::instance() noexcept
    {
        static AnimationAssetManager manager;
        return manager;
    }

    std::filesystem::path AnimationAssetManager::normalizePath(const std::filesystem::path& path)
    {
        if (path.empty()) return {};
        std::error_code error;
        std::filesystem::path result = std::filesystem::weakly_canonical(path, error);
        if (error) { error.clear(); result = std::filesystem::absolute(path, error); }
        return error ? std::filesystem::path{} : result.lexically_normal();
    }

    SkeletonHandle AnimationAssetManager::loadSkeleton(const std::filesystem::path& path)
    {
        const auto normalized = normalizePath(path);
        if (normalized.empty()) return SkeletonHandle::Invalid();
        {
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_skeletonPaths.find(normalized); found != m_skeletonPaths.end()) return found->second;
        }
        SkeletonAsset asset;
        if (!Serialization::SkeletonSerializer{}.load(normalized, asset)) return SkeletonHandle::Invalid();
        return createSkeleton(std::move(asset), normalized);
    }

    SkeletonHandle AnimationAssetManager::createSkeleton(SkeletonAsset asset, const std::filesystem::path& cacheKey)
    {
        const auto normalized = cacheKey.empty() ? std::filesystem::path{} : normalizePath(cacheKey);
        if (!cacheKey.empty() && normalized.empty()) return SkeletonHandle::Invalid();
        if (!asset.guid.isValid()) asset.guid = AssetGUID::generate();
        const std::scoped_lock lock(m_mutex);
        if (const auto found = m_skeletonGuids.find(asset.guid); found != m_skeletonGuids.end()) return found->second;
        if (!normalized.empty()) if (const auto found = m_skeletonPaths.find(normalized); found != m_skeletonPaths.end()) return found->second;
        if (m_nextSkeletonGeneration == 0) ++m_nextSkeletonGeneration;
        const SkeletonHandle handle{ static_cast<std::uint32_t>(m_skeletons.size()), m_nextSkeletonGeneration++ };
        const AssetGUID guid = asset.guid;
        m_skeletons.push_back({ std::make_shared<const SkeletonAsset>(std::move(asset)), normalized, handle.generation });
        if (!normalized.empty()) m_skeletonPaths.emplace(normalized, handle);
        m_skeletonGuids.emplace(guid, handle);
        return handle;
    }

    bool AnimationAssetManager::saveSkeleton(const SkeletonHandle handle, const std::filesystem::path& path) const
    {
        const auto asset = getSkeleton(handle);
        return asset != nullptr && Serialization::SkeletonSerializer{}.save(path, *asset);
    }

    std::shared_ptr<const SkeletonAsset> AnimationAssetManager::getSkeleton(const SkeletonHandle handle) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_skeletons.size()) return nullptr;
        const auto& entry = m_skeletons[handle.index];
        return entry.generation == handle.generation ? entry.resource : nullptr;
    }

    SkeletonHandle AnimationAssetManager::findSkeletonByGuid(const AssetGUID& guid) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_skeletonGuids.find(guid);
        return found == m_skeletonGuids.end() ? SkeletonHandle::Invalid() : found->second;
    }

    void AnimationAssetManager::unloadSkeleton(const SkeletonHandle handle) noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_skeletons.size()) return;
        auto& entry = m_skeletons[handle.index];
        if (entry.generation != handle.generation || entry.resource == nullptr) return;
        if (!entry.path.empty()) m_skeletonPaths.erase(entry.path);
        m_skeletonGuids.erase(entry.resource->guid);
        entry = {};
    }

    AnimationClipHandle AnimationAssetManager::loadClip(const std::filesystem::path& path)
    {
        const auto normalized = normalizePath(path);
        if (normalized.empty()) return AnimationClipHandle::Invalid();
        {
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_clipPaths.find(normalized); found != m_clipPaths.end()) return found->second;
        }
        AnimationClipAsset asset;
        if (!Serialization::AnimationClipSerializer{}.load(normalized, asset)) return AnimationClipHandle::Invalid();
        return createClip(std::move(asset), normalized);
    }

    AnimationClipHandle AnimationAssetManager::createClip(AnimationClipAsset asset, const std::filesystem::path& cacheKey)
    {
        const auto normalized = cacheKey.empty() ? std::filesystem::path{} : normalizePath(cacheKey);
        if (!cacheKey.empty() && normalized.empty()) return AnimationClipHandle::Invalid();
        if (!asset.guid.isValid()) asset.guid = AssetGUID::generate();
        const std::scoped_lock lock(m_mutex);
        if (const auto found = m_clipGuids.find(asset.guid); found != m_clipGuids.end()) return found->second;
        if (!normalized.empty()) if (const auto found = m_clipPaths.find(normalized); found != m_clipPaths.end()) return found->second;
        if (m_nextClipGeneration == 0) ++m_nextClipGeneration;
        const AnimationClipHandle handle{ static_cast<std::uint32_t>(m_clips.size()), m_nextClipGeneration++ };
        const AssetGUID guid = asset.guid;
        m_clips.push_back({ std::make_shared<const AnimationClipAsset>(std::move(asset)), normalized, handle.generation });
        if (!normalized.empty()) m_clipPaths.emplace(normalized, handle);
        m_clipGuids.emplace(guid, handle);
        return handle;
    }

    bool AnimationAssetManager::saveClip(const AnimationClipHandle handle, const std::filesystem::path& path) const
    {
        const auto asset = getClip(handle);
        return asset != nullptr && Serialization::AnimationClipSerializer{}.save(path, *asset);
    }

    std::shared_ptr<const AnimationClipAsset> AnimationAssetManager::getClip(const AnimationClipHandle handle) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_clips.size()) return nullptr;
        const auto& entry = m_clips[handle.index];
        return entry.generation == handle.generation ? entry.resource : nullptr;
    }

    AnimationClipHandle AnimationAssetManager::findClipByGuid(const AssetGUID& guid) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_clipGuids.find(guid);
        return found == m_clipGuids.end() ? AnimationClipHandle::Invalid() : found->second;
    }

    void AnimationAssetManager::unloadClip(const AnimationClipHandle handle) noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_clips.size()) return;
        auto& entry = m_clips[handle.index];
        if (entry.generation != handle.generation || entry.resource == nullptr) return;
        if (!entry.path.empty()) m_clipPaths.erase(entry.path);
        m_clipGuids.erase(entry.resource->guid);
        entry = {};
    }

    AnimatorControllerHandle AnimationAssetManager::loadController(const std::filesystem::path& path)
    {
        const auto normalized = normalizePath(path);
        if (normalized.empty()) return AnimatorControllerHandle::Invalid();
        {
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_controllerPaths.find(normalized); found != m_controllerPaths.end()) return found->second;
        }
        AnimatorControllerAsset asset;
        if (!Serialization::AnimatorControllerSerializer{}.load(normalized, asset)) return AnimatorControllerHandle::Invalid();
        return createController(std::move(asset), normalized);
    }

    AnimatorControllerHandle AnimationAssetManager::createController(AnimatorControllerAsset asset, const std::filesystem::path& cacheKey)
    {
        const auto normalized = cacheKey.empty() ? std::filesystem::path{} : normalizePath(cacheKey);
        if (!cacheKey.empty() && normalized.empty()) return AnimatorControllerHandle::Invalid();
        if (!asset.guid.isValid()) asset.guid = AssetGUID::generate();
        const std::scoped_lock lock(m_mutex);
        if (const auto found = m_controllerGuids.find(asset.guid); found != m_controllerGuids.end()) return found->second;
        if (!normalized.empty()) if (const auto found = m_controllerPaths.find(normalized); found != m_controllerPaths.end()) return found->second;
        if (m_nextControllerGeneration == 0) ++m_nextControllerGeneration;
        const AnimatorControllerHandle handle{ static_cast<std::uint32_t>(m_controllers.size()), m_nextControllerGeneration++ };
        const AssetGUID guid = asset.guid;
        m_controllers.push_back({ std::make_shared<const AnimatorControllerAsset>(std::move(asset)), normalized, handle.generation });
        if (!normalized.empty()) m_controllerPaths.emplace(normalized, handle);
        m_controllerGuids.emplace(guid, handle);
        return handle;
    }

    bool AnimationAssetManager::saveController(const AnimatorControllerHandle handle,
        const std::filesystem::path& path) const
    {
        const auto asset = getController(handle);
        return asset != nullptr && Serialization::AnimatorControllerSerializer{}.save(path, *asset);
    }

    std::shared_ptr<const AnimatorControllerAsset> AnimationAssetManager::getController(
        const AnimatorControllerHandle handle) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_controllers.size()) return nullptr;
        const auto& entry = m_controllers[handle.index];
        return entry.generation == handle.generation ? entry.resource : nullptr;
    }

    AnimatorControllerHandle AnimationAssetManager::findControllerByGuid(const AssetGUID& guid) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_controllerGuids.find(guid);
        return found == m_controllerGuids.end() ? AnimatorControllerHandle::Invalid() : found->second;
    }

    void AnimationAssetManager::unloadController(const AnimatorControllerHandle handle) noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_controllers.size()) return;
        auto& entry = m_controllers[handle.index];
        if (entry.generation != handle.generation || entry.resource == nullptr) return;
        if (!entry.path.empty()) m_controllerPaths.erase(entry.path);
        m_controllerGuids.erase(entry.resource->guid);
        entry = {};
    }

    void AnimationAssetManager::clear() noexcept
    {
        const std::scoped_lock lock(m_mutex);
        m_skeletons.clear(); m_clips.clear(); m_controllers.clear();
        m_skeletonPaths.clear(); m_clipPaths.clear(); m_controllerPaths.clear();
        m_skeletonGuids.clear(); m_clipGuids.clear(); m_controllerGuids.clear();
    }
}