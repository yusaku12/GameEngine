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
        finalize();

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
        m_nextHandle = 0;

        if (!createDefaultTextures())
        {
            LOG_ERROR("[TextureManager] デフォルトテクスチャ作成失敗");
            finalize();
            return false;
        }

        LOG_INFO("[TextureManager] 初期化完了");
        return true;
    }

    bool TextureManager::finalize()
    {
        for (auto& texture : m_textures)
        {
            if (texture)
                texture->finalize();
        }
        m_textures.clear();
        m_pathToHandle.clear();
        m_descriptorHeap.finalize();
        m_device = nullptr;
        m_nextHandle = 0;
        m_defaultWhite = TextureHandle::Invalid();
        m_defaultBlack = TextureHandle::Invalid();
        m_defaultNormal = TextureHandle::Invalid();
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
        const auto normPath = std::filesystem::weakly_canonical(path);
        const auto normPathStr = normPath.string();

        // キャッシュ確認
        auto it = m_pathToHandle.find(normPathStr);
        if (it != m_pathToHandle.end())
        {
            LOG_DEBUG("[TextureManager] キャッシュ: {}", normPathStr);
            return TextureHandle(it->second);
        }

        // 新規ロード
        return loadInternal(normPath, desc);
    }

    Texture* TextureManager::get(TextureHandle handle) noexcept
    {
        if (!handle.isValid() || handle.index >= static_cast<uint32_t>(m_textures.size()))
            return nullptr;
        return m_textures[handle.index].get();
    }

    const Texture* TextureManager::get(TextureHandle handle) const noexcept
    {
        if (!handle.isValid() || handle.index >= static_cast<uint32_t>(m_textures.size()))
            return nullptr;
        return m_textures[handle.index].get();
    }

    void TextureManager::unload(TextureHandle handle) noexcept
    {
        if (!handle.isValid() || handle.index >= static_cast<uint32_t>(m_textures.size()))
            return;

        auto& texture = m_textures[handle.index];
        if (texture)
        {
            // キャッシュから削除
            for (auto it = m_pathToHandle.begin(); it != m_pathToHandle.end(); ++it)
            {
                if (it->second == handle.index)
                {
                    m_pathToHandle.erase(it);
                    break;
                }
            }

            texture->finalize();
            texture.reset();
        }
    }

    void TextureManager::clear() noexcept
    {
        for (auto& texture : m_textures)
        {
            if (texture)
                texture->finalize();
        }
        m_textures.clear();
        m_pathToHandle.clear();
        m_nextHandle = 0;
        m_defaultWhite = TextureHandle::Invalid();
        m_defaultBlack = TextureHandle::Invalid();
        m_defaultNormal = TextureHandle::Invalid();
        m_defaultError = TextureHandle::Invalid();
    }

    bool TextureManager::exists(const std::filesystem::path& path) const noexcept
    {
        const auto normPath = std::filesystem::weakly_canonical(path).string();
        return m_pathToHandle.find(normPath) != m_pathToHandle.end();
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
                auto texture = std::make_unique<Texture>();
                if (!texture->initializeSolidColor(*m_device->get(), m_descriptorHeap, *m_directQueue, *m_directFence, color))
                    return false;
                destination = TextureHandle{ m_nextHandle++ };
                m_textures.push_back(std::move(texture));
                return true;
            };

        return create({ 255, 255, 255, 255 }, m_defaultWhite)
            && create({ 0, 0, 0, 255 }, m_defaultBlack)
            && create({ 128, 128, 255, 255 }, m_defaultNormal)
            && create({ 255, 0, 255, 255 }, m_defaultError);
    }

    TextureHandle TextureManager::loadInternal(
        const std::filesystem::path& path,
        const TextureLoadDesc& desc)
    {
        if (m_device == nullptr)
        {
            LOG_ERROR("[TextureManager] デバイスが無効です");
            return TextureHandle::Invalid();
        }

        // テクスチャを作成
        auto texture = std::make_unique<Texture>();
        if (!texture->initialize(*m_device->get(), m_descriptorHeap, *m_directQueue, *m_directFence, path, desc))
        {
            LOG_ERROR("[TextureManager] テクスチャ初期化失敗: {}", path.string());
            return TextureHandle::Invalid();
        }

        uint32_t handle = m_nextHandle++;
        m_textures.push_back(std::move(texture));
        m_pathToHandle[path.string()] = handle;

        LOG_DEBUG("[TextureManager] テクスチャロード: {} (Handle: {})", path.filename().string(), handle);
        return TextureHandle(handle);
    }
} // namespace Engine