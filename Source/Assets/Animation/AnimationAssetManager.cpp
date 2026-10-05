#include "Pch.h"
#include "Assets\Animation\AnimationAssetBuilder.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Assets\Animation\Serialization\AnimationClipSerializer.h"
#include "Assets\Animation\Serialization\AnimatorControllerSerializer.h"
#include "Assets\Animation\Serialization\SkeletonSerializer.h"
#include "Assets\Model\Import\FbxModelImporter.h"

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
        const AssetGUID guid = asset.guid;
        try
        {
            const std::shared_ptr<const SkeletonAsset> resource = std::make_shared<const SkeletonAsset>(std::move(asset));
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_skeletonGuids.find(guid); found != m_skeletonGuids.end()) return found->second;
            if (!normalized.empty())
                if (const auto found = m_skeletonPaths.find(normalized); found != m_skeletonPaths.end()) return found->second;
            if (m_nextSkeletonGeneration == 0 || m_skeletons.size() >= SkeletonHandle::INVALID_INDEX
                || m_skeletons.size() == m_skeletons.max_size()
                || m_skeletonGuids.size() == m_skeletonGuids.max_size()
                || (!normalized.empty() && m_skeletonPaths.size() == m_skeletonPaths.max_size()))
            {
                LOG_ERROR("[AnimationAssetManager] Skeleton cache capacity exhausted");
                return SkeletonHandle::Invalid();
            }

            m_skeletons.reserve(m_skeletons.size() + 1);
            m_skeletonGuids.reserve(m_skeletonGuids.size() + 1);
            if (!normalized.empty())
                m_skeletonPaths.reserve(m_skeletonPaths.size() + 1);

            const SkeletonHandle handle{ static_cast<std::uint32_t>(m_skeletons.size()), m_nextSkeletonGeneration };
            bool pathInserted = false;
            bool guidInserted = false;
            try
            {
                if (!normalized.empty())
                    pathInserted = m_skeletonPaths.emplace(normalized, handle).second;
                if (!normalized.empty() && !pathInserted)
                    return m_skeletonPaths.at(normalized);

                guidInserted = m_skeletonGuids.emplace(guid, handle).second;
                if (!guidInserted)
                {
                    if (pathInserted)
                        m_skeletonPaths.erase(normalized);
                    return m_skeletonGuids.at(guid);
                }

                m_skeletons.push_back({ resource, normalized, handle.generation });
            }
            catch (...)
            {
                if (guidInserted)
                    m_skeletonGuids.erase(guid);
                if (pathInserted)
                    m_skeletonPaths.erase(normalized);
                throw;
            }
            ++m_nextSkeletonGeneration;
            return handle;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[AnimationAssetManager] Failed to allocate memory while registering Skeleton.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[AnimationAssetManager] Skeleton cache capacity exceeded.");
        }
        return SkeletonHandle::Invalid();
    }

    bool AnimationAssetManager::saveSkeleton(const SkeletonHandle handle, const std::filesystem::path& path)
    {
        const std::filesystem::path normalized = normalizePath(path);
        if (normalized.empty())
            return false;
        try
        {
            std::filesystem::path stagedPath = normalized;
            const std::scoped_lock lock(m_mutex);
            if (!handle.isValid() || handle.index >= m_skeletons.size())
                return false;
            Entry<SkeletonAsset>& entry = m_skeletons[handle.index];
            if (entry.generation != handle.generation || entry.resource == nullptr)
                return false;
            if (const auto conflict = m_skeletonPaths.find(normalized);
                conflict != m_skeletonPaths.end() && conflict->second != handle)
                return false;

            const bool needsPathEntry = entry.path != normalized;
            if (needsPathEntry)
            {
                if (m_skeletonPaths.size() == m_skeletonPaths.max_size())
                    return false;
                m_skeletonPaths.reserve(m_skeletonPaths.size() + 1);
                if (!m_skeletonPaths.emplace(stagedPath, handle).second)
                    return false;
            }
            bool saved = false;
            try
            {
                saved = Serialization::SkeletonSerializer{}.save(normalized, *entry.resource);
            }
            catch (...)
            {
                if (needsPathEntry)
                    m_skeletonPaths.erase(stagedPath);
                throw;
            }
            if (!saved)
            {
                if (needsPathEntry)
                    m_skeletonPaths.erase(stagedPath);
                return false;
            }
            if (needsPathEntry)
            {
                entry.path.swap(stagedPath);
                if (!stagedPath.empty())
                    m_skeletonPaths.erase(stagedPath);
            }
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[AnimationAssetManager] Failed to allocate memory while saving Skeleton.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[AnimationAssetManager] Skeleton path cache capacity exceeded while saving.");
        }
        return false;
    }

    std::shared_ptr<const SkeletonAsset> AnimationAssetManager::getSkeleton(const SkeletonHandle handle) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_skeletons.size()) return nullptr;
        const auto& entry = m_skeletons[handle.index];
        return entry.generation == handle.generation ? entry.resource : nullptr;
    }

    std::filesystem::path AnimationAssetManager::getSkeletonPath(const SkeletonHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_skeletons.size())
            return {};
        const Entry<SkeletonAsset>& entry = m_skeletons[handle.index];
        return entry.generation == handle.generation && entry.resource != nullptr
            ? entry.path : std::filesystem::path{};
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

    std::vector<AnimationClipHandle> AnimationAssetManager::importClips(const std::filesystem::path& path, const SkeletonHandle skeleton)
    {
        const std::shared_ptr<const SkeletonAsset> skeletonAsset = getSkeleton(skeleton);
        if (skeletonAsset == nullptr)
        {
            LOG_ERROR("[Animation] 外部アニメーションの対象Skeletonが見つかりません: {}", path.string());
            return {};
        }

        const std::vector<AnimationResource> animations = FbxModelImporter{}.importAnimations(path);
        if (animations.empty())
        {
            LOG_ERROR("[Animation] 外部ファイルにアニメーションがありません: {}", path.string());
            return {};
        }
        AnimationAssetBuilder builder;
        std::vector<AnimationClipHandle> handles;
        handles.reserve(animations.size());
        for (const AnimationResource& animation : animations)
        {
            AnimationClipAsset clip;
            if (!builder.buildClip(animation, *skeletonAsset, path, clip))
            {
                LOG_ERROR("[Animation] Clip変換に失敗しました: {} (duration={}, channels={})",
                    animation.name, animation.duration, animation.channels.size());
                continue;
            }
            const AnimationClipHandle handle = createClip(std::move(clip));
            if (handle.isValid())
                handles.push_back(handle);
        }
        return handles;
    }

    AnimationClipHandle AnimationAssetManager::createClip(AnimationClipAsset asset, const std::filesystem::path& cacheKey)
    {
        const auto normalized = cacheKey.empty() ? std::filesystem::path{} : normalizePath(cacheKey);
        if (!cacheKey.empty() && normalized.empty()) return AnimationClipHandle::Invalid();
        if (!asset.guid.isValid()) asset.guid = AssetGUID::generate();
        const AssetGUID guid = asset.guid;
        try
        {
            const std::shared_ptr<const AnimationClipAsset> resource = std::make_shared<const AnimationClipAsset>(std::move(asset));
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_clipGuids.find(guid); found != m_clipGuids.end()) return found->second;
            if (!normalized.empty())
                if (const auto found = m_clipPaths.find(normalized); found != m_clipPaths.end()) return found->second;
            if (m_nextClipGeneration == 0 || m_clips.size() >= AnimationClipHandle::INVALID_INDEX
                || m_clips.size() == m_clips.max_size()
                || m_clipGuids.size() == m_clipGuids.max_size()
                || (!normalized.empty() && m_clipPaths.size() == m_clipPaths.max_size()))
            {
                LOG_ERROR("[AnimationAssetManager] Animation clip cache capacity exhausted");
                return AnimationClipHandle::Invalid();
            }

            m_clips.reserve(m_clips.size() + 1);
            m_clipGuids.reserve(m_clipGuids.size() + 1);
            if (!normalized.empty())
                m_clipPaths.reserve(m_clipPaths.size() + 1);

            const AnimationClipHandle handle{ static_cast<std::uint32_t>(m_clips.size()), m_nextClipGeneration };
            bool pathInserted = false;
            bool guidInserted = false;
            try
            {
                if (!normalized.empty())
                    pathInserted = m_clipPaths.emplace(normalized, handle).second;
                if (!normalized.empty() && !pathInserted)
                    return m_clipPaths.at(normalized);

                guidInserted = m_clipGuids.emplace(guid, handle).second;
                if (!guidInserted)
                {
                    if (pathInserted)
                        m_clipPaths.erase(normalized);
                    return m_clipGuids.at(guid);
                }

                m_clips.push_back({ resource, normalized, handle.generation });
            }
            catch (...)
            {
                if (guidInserted)
                    m_clipGuids.erase(guid);
                if (pathInserted)
                    m_clipPaths.erase(normalized);
                throw;
            }
            ++m_nextClipGeneration;
            return handle;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[AnimationAssetManager] Failed to allocate memory while registering animation clip.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[AnimationAssetManager] Animation clip cache capacity exceeded.");
        }
        return AnimationClipHandle::Invalid();
    }

    bool AnimationAssetManager::saveClip(const AnimationClipHandle handle, const std::filesystem::path& path)
    {
        const std::filesystem::path normalized = normalizePath(path);
        if (normalized.empty())
            return false;

        try
        {
            std::filesystem::path stagedPath = normalized;
            const std::scoped_lock lock(m_mutex);
            if (!handle.isValid() || handle.index >= m_clips.size())
                return false;
            Entry<AnimationClipAsset>& entry = m_clips[handle.index];
            if (entry.generation != handle.generation || entry.resource == nullptr)
                return false;
            if (const auto conflict = m_clipPaths.find(normalized);
                conflict != m_clipPaths.end() && conflict->second != handle)
                return false;

            const bool needsPathEntry = entry.path != normalized;
            if (needsPathEntry)
            {
                if (m_clipPaths.size() == m_clipPaths.max_size())
                    return false;
                m_clipPaths.reserve(m_clipPaths.size() + 1);
                if (!m_clipPaths.emplace(stagedPath, handle).second)
                    return false;
            }
            bool saved = false;
            try
            {
                saved = Serialization::AnimationClipSerializer{}.save(normalized, *entry.resource);
            }
            catch (...)
            {
                if (needsPathEntry)
                    m_clipPaths.erase(stagedPath);
                throw;
            }
            if (!saved)
            {
                if (needsPathEntry)
                    m_clipPaths.erase(stagedPath);
                return false;
            }
            if (needsPathEntry)
            {
                entry.path.swap(stagedPath);
                if (!stagedPath.empty())
                    m_clipPaths.erase(stagedPath);
            }
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[AnimationAssetManager] Failed to allocate memory while saving animation clip.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[AnimationAssetManager] Animation clip path cache capacity exceeded while saving.");
        }
        return false;
    }

    std::shared_ptr<const AnimationClipAsset> AnimationAssetManager::getClip(const AnimationClipHandle handle) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_clips.size()) return nullptr;
        const auto& entry = m_clips[handle.index];
        return entry.generation == handle.generation ? entry.resource : nullptr;
    }

    std::filesystem::path AnimationAssetManager::getClipPath(const AnimationClipHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_clips.size())
            return {};
        const Entry<AnimationClipAsset>& entry = m_clips[handle.index];
        return entry.generation == handle.generation && entry.resource != nullptr
            ? entry.path : std::filesystem::path{};
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
        const AssetGUID guid = asset.guid;
        try
        {
            const std::shared_ptr<const AnimatorControllerAsset> resource =
                std::make_shared<const AnimatorControllerAsset>(std::move(asset));
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_controllerGuids.find(guid); found != m_controllerGuids.end()) return found->second;
            if (!normalized.empty())
                if (const auto found = m_controllerPaths.find(normalized); found != m_controllerPaths.end()) return found->second;
            if (m_nextControllerGeneration == 0 || m_controllers.size() >= AnimatorControllerHandle::INVALID_INDEX
                || m_controllers.size() == m_controllers.max_size()
                || m_controllerGuids.size() == m_controllerGuids.max_size()
                || (!normalized.empty() && m_controllerPaths.size() == m_controllerPaths.max_size()))
            {
                LOG_ERROR("[AnimationAssetManager] Animator controller cache capacity exhausted");
                return AnimatorControllerHandle::Invalid();
            }

            m_controllers.reserve(m_controllers.size() + 1);
            m_controllerGuids.reserve(m_controllerGuids.size() + 1);
            if (!normalized.empty())
                m_controllerPaths.reserve(m_controllerPaths.size() + 1);

            const AnimatorControllerHandle handle{ static_cast<std::uint32_t>(m_controllers.size()), m_nextControllerGeneration };
            bool pathInserted = false;
            bool guidInserted = false;
            try
            {
                if (!normalized.empty())
                    pathInserted = m_controllerPaths.emplace(normalized, handle).second;
                if (!normalized.empty() && !pathInserted)
                    return m_controllerPaths.at(normalized);

                guidInserted = m_controllerGuids.emplace(guid, handle).second;
                if (!guidInserted)
                {
                    if (pathInserted)
                        m_controllerPaths.erase(normalized);
                    return m_controllerGuids.at(guid);
                }

                m_controllers.push_back({ resource, normalized, handle.generation });
            }
            catch (...)
            {
                if (guidInserted)
                    m_controllerGuids.erase(guid);
                if (pathInserted)
                    m_controllerPaths.erase(normalized);
                throw;
            }
            ++m_nextControllerGeneration;
            return handle;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[AnimationAssetManager] Failed to allocate memory while registering Animator Controller.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[AnimationAssetManager] Animator Controller cache capacity exceeded.");
        }
        return AnimatorControllerHandle::Invalid();
    }

    bool AnimationAssetManager::saveController(const AnimatorControllerHandle handle,
        const std::filesystem::path& path)
    {
        const std::filesystem::path normalized = normalizePath(path);
        if (normalized.empty())
            return false;
        try
        {
            std::filesystem::path stagedPath = normalized;
            const std::scoped_lock lock(m_mutex);
            if (!handle.isValid() || handle.index >= m_controllers.size())
                return false;
            Entry<AnimatorControllerAsset>& entry = m_controllers[handle.index];
            if (entry.generation != handle.generation || entry.resource == nullptr)
                return false;
            if (const auto conflict = m_controllerPaths.find(normalized);
                conflict != m_controllerPaths.end() && conflict->second != handle)
                return false;

            const bool needsPathEntry = entry.path != normalized;
            if (needsPathEntry)
            {
                if (m_controllerPaths.size() == m_controllerPaths.max_size())
                    return false;
                m_controllerPaths.reserve(m_controllerPaths.size() + 1);
                if (!m_controllerPaths.emplace(stagedPath, handle).second)
                    return false;
            }
            bool saved = false;
            try
            {
                saved = Serialization::AnimatorControllerSerializer{}.save(normalized, *entry.resource);
            }
            catch (...)
            {
                if (needsPathEntry)
                    m_controllerPaths.erase(stagedPath);
                throw;
            }
            if (!saved)
            {
                if (needsPathEntry)
                    m_controllerPaths.erase(stagedPath);
                return false;
            }
            if (needsPathEntry)
            {
                entry.path.swap(stagedPath);
                if (!stagedPath.empty())
                    m_controllerPaths.erase(stagedPath);
            }
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[AnimationAssetManager] Failed to allocate memory while saving Animator Controller.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[AnimationAssetManager] Animator Controller path cache capacity exceeded while saving.");
        }
        return false;
    }

    std::shared_ptr<const AnimatorControllerAsset> AnimationAssetManager::getController(
        const AnimatorControllerHandle handle) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_controllers.size()) return nullptr;
        const auto& entry = m_controllers[handle.index];
        return entry.generation == handle.generation ? entry.resource : nullptr;
    }

    std::filesystem::path AnimationAssetManager::getControllerPath(const AnimatorControllerHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_controllers.size())
            return {};
        const Entry<AnimatorControllerAsset>& entry = m_controllers[handle.index];
        return entry.generation == handle.generation && entry.resource != nullptr
            ? entry.path : std::filesystem::path{};
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