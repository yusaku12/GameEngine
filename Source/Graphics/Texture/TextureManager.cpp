#include "Pch.h"
#include "Graphics\Texture\TextureManager.h"
#include "Graphics\Texture\Texture.h"
#include "Graphics\DirectX12\Device.h"
#include "Graphics\DirectX12\Queue.h"
#include "Graphics\DirectX12\Fence.h"
#include "Core\Logging\Logger.h"

namespace Engine
{
    bool TextureManager::initialize(
        DX12Device& device,
        DX12CommandQueue& directQueue,
        DX12Fence& directFence,
        uint32_t descriptorHeapCapacity)
    {
        if (!finalize())
        {
            LOG_ERROR("[TextureManager] 既存リソースを安全に解放できないため再初期化を中止します");
            return false;
        }

        if (device.get() == nullptr)
        {
            LOG_ERROR("[TextureManager] デバイスが無効です");
            return false;
        }

        m_device = &device;
        m_directQueue = &directQueue;
        m_directFence = &directFence;

        // SRV ディスクリプタヒープを作成
        DX12DescriptorHeapConfig heapConfig{};
        heapConfig.type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapConfig.capacity = descriptorHeapCapacity;
        heapConfig.shaderVisible = true;

        if (!m_descriptorHeap.initialize(*device.get(), heapConfig))
        {
            LOG_ERROR("[TextureManager] SRV ヒープ作成失敗");
            return false;
        }

        m_textures.reserve(256);

        if (!createDefaultTextures())
        {
            LOG_ERROR("[TextureManager] デフォルトテクスチャ作成失敗");
            if (!finalize())
                LOG_CRITICAL("[TextureManager] 初期化失敗後のリソース解放に失敗しました");
            return false;
        }

        LOG_INFO("[TextureManager] 初期化完了");
        return true;
    }

    bool TextureManager::finalize()
    {
        if (m_directFence != nullptr)
        {
            const std::uint64_t fenceValue = m_directFence->getLastSignaledValue();
            if (fenceValue != 0 && !m_directFence->isComplete(fenceValue)
                && !m_directFence->waitOnCpu(fenceValue))
            {
                LOG_ERROR("[TextureManager] GPU完了を確認できないためFinalizeを中止します");
                return false;
            }
        }

        for (TextureEntry& entry : m_textures)
        {
            if (entry.texture)
            {
                const TextureResourceInfo* const info = entry.texture->getResourceInfo();
                const std::uint32_t srvIndex = info != nullptr ? info->srvIndex : TextureHandle::INVALID_INDEX;
                if (!entry.texture->finalize())
                    return false;
                if (srvIndex != TextureHandle::INVALID_INDEX && !m_descriptorHeap.release(srvIndex))
                {
                    LOG_ERROR("[TextureManager] Texture SRV Descriptor を解放できませんでした");
                    return false;
                }
            }
        }
        m_textures.clear();
        m_pathToHandle.clear();
        m_descriptorHeap.finalize();
        m_device = nullptr;
        m_directQueue = nullptr;
        m_directFence = nullptr;
        m_defaultWhite = TextureHandle::Invalid();
        m_defaultBlack = TextureHandle::Invalid();
        m_defaultNormal = TextureHandle::Invalid();
        m_defaultMetallicRoughness = TextureHandle::Invalid();
        m_defaultError = TextureHandle::Invalid();
        return true;
    }

    TextureHandle TextureManager::load(const std::filesystem::path& path, const TextureLoadDesc& desc)
    {
        if (path.empty())
        {
            LOG_ERROR("[TextureManager] パスが空です");
            return TextureHandle::Invalid();
        }

        // 正規化
        std::error_code error;
        const auto normPath = std::filesystem::weakly_canonical(path, error);
        if (error)
        {
            LOG_ERROR("[TextureManager] パスの正規化に失敗しました: {} ({})", path.string(), error.message());
            return TextureHandle::Invalid();
        }
        const auto normPathStr = normPath.string();

        // キャッシュ確認
        const auto [begin, end] = m_pathToHandle.equal_range(normPathStr);
        for (auto it = begin; it != end;)
        {
            if (it->second.desc != desc)
            {
                ++it;
                continue;
            }
            if (get(it->second.handle) != nullptr)
            {
                LOG_DEBUG("[TextureManager] キャッシュ: {}", normPathStr);
                return it->second.handle;
            }
            it = m_pathToHandle.erase(it);
        }

        // 新規ロード
        return loadInternal(normPath, desc);
    }

    bool TextureManager::registerAssetPath(const AssetGUID& guid, const std::filesystem::path& path)
    {
        if (!guid.isValid() || path.empty())
            return false;
        std::error_code error;
        const std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path, error);
        if (error)
        {
            LOG_ERROR("[TextureManager] Asset path normalization failed: {} ({})", path.string(), error.message());
            return false;
        }

        const std::scoped_lock lock(m_assetPathMutex);
        if (const auto guidEntry = m_guidToPath.find(guid); guidEntry != m_guidToPath.end())
            return guidEntry->second == normalizedPath;
        if (const auto pathEntry = m_pathToGuid.find(normalizedPath); pathEntry != m_pathToGuid.end())
            return pathEntry->second == guid;

        try
        {
            const auto [guidEntry, guidInserted] = m_guidToPath.emplace(guid, normalizedPath);
            if (!guidInserted)
                return guidEntry->second == normalizedPath;

            try
            {
                const auto [pathEntry, pathInserted] = m_pathToGuid.emplace(normalizedPath, guid);
                GE_UNUSED(pathEntry);
                if (!pathInserted)
                {
                    m_guidToPath.erase(guidEntry);
                    return false;
                }
            }
            catch (const std::bad_alloc&)
            {
                m_guidToPath.erase(guidEntry);
                throw;
            }
            catch (const std::length_error&)
            {
                m_guidToPath.erase(guidEntry);
                throw;
            }
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[TextureManager] Memory allocation failed while registering texture asset path: {}",
                path.string());
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[TextureManager] Texture asset path registry reached its maximum capacity.");
            return false;
        }
    }

    AssetGUID TextureManager::registerAssetPath(const std::filesystem::path& path)
    {
        if (path.empty())
            return {};
        std::error_code error;
        const std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path, error);
        if (error)
        {
            LOG_ERROR("[TextureManager] Asset path normalization failed: {} ({})", path.string(), error.message());
            return {};
        }
        const std::scoped_lock lock(m_assetPathMutex);
        if (const auto found = m_pathToGuid.find(normalizedPath); found != m_pathToGuid.end())
            return found->second;

        try
        {
            const AssetGUID guid = AssetGUID::generate();
            const auto [guidEntry, guidInserted] = m_guidToPath.emplace(guid, normalizedPath);
            GE_UNUSED(guidEntry);
            if (!guidInserted)
            {
                LOG_ERROR("[TextureManager] Generated texture asset GUID is already registered.");
                return {};
            }

            try
            {
                const auto [pathEntry, pathInserted] = m_pathToGuid.emplace(normalizedPath, guid);
                GE_UNUSED(pathEntry);
                if (!pathInserted)
                {
                    m_guidToPath.erase(guid);
                    LOG_ERROR("[TextureManager] Texture asset path became registered during insertion.");
                    return {};
                }
            }
            catch (const std::bad_alloc&)
            {
                m_guidToPath.erase(guid);
                throw;
            }
            catch (const std::length_error&)
            {
                m_guidToPath.erase(guid);
                throw;
            }
            return guid;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[TextureManager] Memory allocation failed while registering texture asset path: {}",
                path.string());
            return {};
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[TextureManager] Texture asset path registry reached its maximum capacity.");
            return {};
        }
    }

    std::filesystem::path TextureManager::getAssetPath(const AssetGUID& guid) const
    {
        const std::scoped_lock lock(m_assetPathMutex);
        const auto found = m_guidToPath.find(guid);
        return found == m_guidToPath.end() ? std::filesystem::path{} : found->second;
    }

    TextureHandle TextureManager::load(const AssetGUID& guid, const TextureLoadDesc& desc)
    {
        std::filesystem::path path;
        {
            const std::scoped_lock lock(m_assetPathMutex);
            const auto found = m_guidToPath.find(guid);
            if (found == m_guidToPath.end())
                return TextureHandle::Invalid();
            path = found->second;
        }
        return load(path, desc);
    }

    Texture* TextureManager::get(TextureHandle handle) noexcept
    {
        if (!handle.isValid() || handle.index >= static_cast<uint32_t>(m_textures.size()))
            return nullptr;
        const TextureEntry& entry = m_textures[handle.index];
        return entry.generation == handle.generation && entry.texture != nullptr && entry.texture->isLoaded()
            ? entry.texture.get() : nullptr;
    }

    const Texture* TextureManager::get(TextureHandle handle) const noexcept
    {
        if (!handle.isValid() || handle.index >= static_cast<uint32_t>(m_textures.size()))
            return nullptr;
        const TextureEntry& entry = m_textures[handle.index];
        return entry.generation == handle.generation && entry.texture != nullptr && entry.texture->isLoaded()
            ? entry.texture.get() : nullptr;
    }

    void TextureManager::unload(TextureHandle handle) noexcept
    {
        if (!handle.isValid() || handle.index >= static_cast<uint32_t>(m_textures.size()))
            return;

        TextureEntry& entry = m_textures[handle.index];
        if (entry.generation != handle.generation || !entry.texture)
            return;
        if (handle == m_defaultWhite || handle == m_defaultBlack || handle == m_defaultNormal
            || handle == m_defaultMetallicRoughness || handle == m_defaultError)
            return;

        const TextureResourceInfo* const info = entry.texture->getResourceInfo();
        const std::uint32_t srvIndex = info != nullptr ? info->srvIndex : TextureHandle::INVALID_INDEX;
        if (!entry.texture->finalize())
        {
            LOG_ERROR("[TextureManager] GPU完了を確認できないためTextureをUnloadできません");
            return;
        }
        if (srvIndex != TextureHandle::INVALID_INDEX && !m_descriptorHeap.release(srvIndex))
            LOG_ERROR("[TextureManager] Texture SRV Descriptor を解放できませんでした");

        for (auto it = m_pathToHandle.begin(); it != m_pathToHandle.end();)
        {
            if (it->second.handle == handle)
                it = m_pathToHandle.erase(it);
            else
                ++it;
        }
        entry.texture.reset();
    }

    void TextureManager::clear() noexcept
    {
        if (m_directFence != nullptr)
        {
            const std::uint64_t fenceValue = m_directFence->getLastSignaledValue();
            if (fenceValue != 0 && !m_directFence->isComplete(fenceValue)
                && !m_directFence->waitOnCpu(fenceValue))
            {
                LOG_ERROR("[TextureManager] GPU完了を確認できないためClearを中止します");
                return;
            }
        }

        for (TextureEntry& entry : m_textures)
        {
            if (!entry.texture)
                continue;

            const TextureResourceInfo* const info = entry.texture->getResourceInfo();
            const std::uint32_t srvIndex = info != nullptr ? info->srvIndex : TextureHandle::INVALID_INDEX;
            if (!entry.texture->finalize())
            {
                LOG_ERROR("[TextureManager] GPU完了を確認できないためClearを中止します");
                return;
            }
            if (srvIndex != TextureHandle::INVALID_INDEX && !m_descriptorHeap.release(srvIndex))
            {
                LOG_ERROR("[TextureManager] Texture SRV Descriptor を解放できないためClearを中止します");
                return;
            }
        }
        m_textures.clear();
        m_pathToHandle.clear();
        m_defaultWhite = TextureHandle::Invalid();
        m_defaultBlack = TextureHandle::Invalid();
        m_defaultNormal = TextureHandle::Invalid();
        m_defaultMetallicRoughness = TextureHandle::Invalid();
        m_defaultError = TextureHandle::Invalid();
    }

    bool TextureManager::exists(const std::filesystem::path& path) const noexcept
    {
        if (path.empty())
            return false;
        std::error_code error;
        const std::string normalizedPath = std::filesystem::weakly_canonical(path, error).string();
        if (error)
            return false;
        const auto [begin, end] = m_pathToHandle.equal_range(normalizedPath);
        return std::any_of(begin, end, [this](const auto& entry)
            {
                return get(entry.second.handle) != nullptr;
            });
    }

    uint32_t TextureManager::getLoadedTextureCount() const noexcept
    {
        return static_cast<uint32_t>(std::count_if(m_textures.begin(), m_textures.end(),
            [](const TextureEntry& entry) { return entry.texture != nullptr; }));
    }

    std::uint32_t TextureManager::getSRVIndex(const TextureHandle handle) const noexcept
    {
        const Texture* texture = get(handle);
        const TextureResourceInfo* info = texture != nullptr ? texture->getResourceInfo() : nullptr;
        return info != nullptr ? info->srvIndex : TextureHandle::INVALID_INDEX;
    }

    bool TextureManager::createDefaultTextures()
    {
        const auto create = [this](const std::array<std::uint8_t, 4>& color, TextureHandle& destination)
            {
                if (m_nextGeneration == 0
                    || m_textures.size() >= TextureHandle::INVALID_INDEX)
                {
                    LOG_ERROR("[TextureManager] Texture handle space exhausted");
                    return false;
                }

                auto texture = std::make_unique<Texture>();
                if (!texture->initializeSolidColor(*m_device->get(), m_descriptorHeap, *m_directQueue, *m_directFence, color))
                    return false;
                const std::uint32_t generation = m_nextGeneration++;
                destination = TextureHandle{ static_cast<std::uint32_t>(m_textures.size()), generation };
                m_textures.push_back({ std::move(texture), generation });
                return true;
            };

        return create({ 255, 255, 255, 255 }, m_defaultWhite)
            && create({ 0, 0, 0, 255 }, m_defaultBlack)
            && create({ 128, 128, 255, 255 }, m_defaultNormal)
            && create({ 0, 255, 0, 255 }, m_defaultMetallicRoughness)
            && create({ 255, 0, 255, 255 }, m_defaultError);
    }

    TextureHandle TextureManager::loadInternal(const std::filesystem::path& path, const TextureLoadDesc& desc)
    {
        if (m_device == nullptr)
        {
            LOG_ERROR("[TextureManager] デバイスが無効です");
            return TextureHandle::Invalid();
        }

        if (m_nextGeneration == 0 || m_textures.size() >= TextureHandle::INVALID_INDEX)
        {
            LOG_ERROR("[TextureManager] Texture handle space exhausted");
            return TextureHandle::Invalid();
        }

        std::uint32_t allocatedSrvIndex = TextureHandle::INVALID_INDEX;
        try
        {
            const std::string cachePath = path.string();
            auto texture = std::make_unique<Texture>();
            if (!texture->initialize(*m_device->get(), m_descriptorHeap, *m_directQueue, *m_directFence, path, desc))
            {
                LOG_ERROR("[TextureManager] テクスチャ初期化失敗: {}", path.string());
                return TextureHandle::Invalid();
            }
            const TextureResourceInfo* const info = texture->getResourceInfo();
            if (info == nullptr)
            {
                LOG_ERROR("[TextureManager] Initialized texture has no SRV resource information: {}", path.string());
                return TextureHandle::Invalid();
            }
            allocatedSrvIndex = info->srvIndex;

            const std::uint32_t generation = m_nextGeneration;
            const TextureHandle handle{ static_cast<std::uint32_t>(m_textures.size()), generation };
            m_textures.reserve(m_textures.size() + 1);
            m_pathToHandle.reserve(m_pathToHandle.size() + 1);
            m_textures.push_back({ std::move(texture), generation });
            try
            {
                m_pathToHandle.emplace(cachePath, CachedTexture{ desc, handle });
            }
            catch (const std::bad_alloc&)
            {
                m_textures.pop_back();
                throw;
            }
            catch (const std::length_error&)
            {
                m_textures.pop_back();
                throw;
            }

            ++m_nextGeneration;
            allocatedSrvIndex = TextureHandle::INVALID_INDEX;
            LOG_DEBUG("[TextureManager] テクスチャロード: {} (Handle: {})", path.filename().string(), handle.index);
            return handle;
        }
        catch (const std::bad_alloc&)
        {
            if (allocatedSrvIndex != TextureHandle::INVALID_INDEX
                && !m_descriptorHeap.release(allocatedSrvIndex))
                LOG_ERROR("[TextureManager] Failed to release Texture SRV Descriptor after allocation failure.");
            LOG_ERROR("[TextureManager] メモリ確保に失敗したためテクスチャを登録できません: {}", path.string());
            return TextureHandle::Invalid();
        }
        catch (const std::length_error&)
        {
            if (allocatedSrvIndex != TextureHandle::INVALID_INDEX
                && !m_descriptorHeap.release(allocatedSrvIndex))
                LOG_ERROR("[TextureManager] Failed to release Texture SRV Descriptor after capacity failure.");
            LOG_ERROR("[TextureManager] テクスチャ管理容量の上限に達しました: {}", path.string());
            return TextureHandle::Invalid();
        }
    }
} // namespace Engine