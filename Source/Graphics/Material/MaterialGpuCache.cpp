#include "Pch.h"
#include "Graphics\Material\MaterialGpuCache.h"
#include "Graphics\DirectX12\Device\Device.h"
#include "Graphics\DirectX12\Command\Fence.h"
#include "Graphics\Texture\TextureManager.h"

namespace Engine
{
    namespace
    {
        constexpr std::uint64_t CONSTANT_BUFFER_ALIGNMENT = 256;

        TextureHandle resolveTexture(TextureManager& manager, const AssetGUID& guid,
            const TextureLoadDesc& description, const TextureHandle fallback,
            const MaterialAsset& material, const char* slotName)
        {
            if (!guid.isValid())
                return fallback;
            const TextureHandle texture = manager.load(guid, description);
            if (texture.isValid())
                return texture;
            LOG_WARNING("[MaterialGpuCache] Texture fallback: Material='{}', GUID={:016X}{:016X}, Slot={}",
                material.name, guid.high, guid.low, slotName);
            return fallback;
        }
    }

    MaterialGpuCache::~MaterialGpuCache()
    {
        finalize();
    }

    bool MaterialGpuCache::initialize(DX12Device& device, DX12Fence& fence) noexcept
    {
        if (device.get() == nullptr)
            return false;
        if (!finalize())
            return false;
        m_device = &device;
        m_fence = &fence;
        return true;
    }

    bool MaterialGpuCache::finalize()
    {
        bool succeeded = true;
        for (auto& [key, resource] : m_resources)
        {
            GE_UNUSED(key);
            succeeded = resource->constantBuffer.finalize() && succeeded;
        }
        for (std::unique_ptr<MaterialGpuResource>& resource : m_retiredResources)
            succeeded = resource->constantBuffer.finalize() && succeeded;
        if (!succeeded)
        {
            LOG_ERROR("[MaterialGpuCache] GPU使用完了を確認できないためCacheのFinalizeを中止します");
            return false;
        }

        m_resources.clear();
        m_retiredResources.clear();
        m_device = nullptr;
        m_fence = nullptr;
        return true;
    }

    MaterialGpuResource* MaterialGpuCache::getOrCreate(const MaterialHandle handle,
        std::shared_ptr<const MaterialAsset> snapshot)
    {
        if (m_device == nullptr || m_fence == nullptr)
            return nullptr;
        const std::uint64_t key = makeKey(handle);
        if (const auto found = m_resources.find(key); found != m_resources.end())
        {
            found->second->pendingSubmission = true;
            const MaterialGpuResource& cached = *found->second;
            std::shared_ptr<const MaterialAsset> current = snapshot ? snapshot : MaterialManager::instance().get(handle);
            if (current == nullptr)
                current = cached.source;
            TextureManager& textures = TextureManager::instance();
            const bool cachedTexturesAvailable =
                textures.get(cached.baseColorTexture) != nullptr
                && textures.get(cached.normalTexture) != nullptr
                && textures.get(cached.metallicRoughnessTexture) != nullptr
                && textures.get(cached.ambientOcclusionTexture) != nullptr
                && textures.get(cached.emissiveTexture) != nullptr
                && std::ranges::all_of(cached.terrainTextures, [&textures](const TextureHandle texture)
                    { return textures.get(texture) != nullptr; });
            if (cached.source == current && cachedTexturesAvailable)
                return found->second.get();

            std::unique_ptr<MaterialGpuResource> replacement = createResource(handle, current);
            if (replacement == nullptr)
                return found->second.get();
            replacement->pendingSubmission = true;
            try
            {
                m_retiredResources.reserve(m_retiredResources.size() + 1);
                m_retiredResources.push_back(std::move(found->second));
            }
            catch (const std::bad_alloc&)
            {
                LOG_ERROR("[MaterialGpuCache] Failed to retain the previous material resource.");
                return found->second.get();
            }
            catch (const std::length_error&)
            {
                LOG_ERROR("[MaterialGpuCache] Retired material resource capacity is exhausted.");
                return found->second.get();
            }
            found->second = std::move(replacement);
            return found->second.get();
        }

        std::unique_ptr<MaterialGpuResource> resource = createResource(handle, std::move(snapshot));
        if (resource == nullptr)
        {
            const MaterialHandle errorMaterial = MaterialManager::instance().getErrorMaterial();
            if (handle != errorMaterial)
                return getOrCreate(errorMaterial);
            return nullptr;
        }
        MaterialGpuResource* const result = resource.get();
        result->pendingSubmission = true;
        try
        {
            const auto [entry, inserted] = m_resources.emplace(key, std::move(resource));
            if (!inserted)
                return entry->second.get();
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[MaterialGpuCache] Failed to allocate a cache entry for Material {}:{}.",
                handle.index, handle.generation);
            return nullptr;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[MaterialGpuCache] Material cache reached its maximum capacity.");
            return nullptr;
        }
        return result;
    }

    bool MaterialGpuCache::markUsed(const MaterialHandle handle, const std::uint64_t fenceValue)
    {
        const auto found = m_resources.find(makeKey(handle));
        if (found == m_resources.end())
            return false;
        found->second->lastUsedFenceValue = fenceValue;
        found->second->pendingSubmission = false;
        bool succeeded = found->second->constantBuffer.markUsed(fenceValue);
        // Different queued snapshots of one handle may have been prepared in this submission.
        for (auto& retired : m_retiredResources)
        {
            if (retired->handle == handle && retired->pendingSubmission)
            {
                retired->lastUsedFenceValue = fenceValue;
                retired->pendingSubmission = false;
                succeeded = retired->constantBuffer.markUsed(fenceValue) && succeeded;
            }
        }
        return succeeded;
    }

    bool MaterialGpuCache::rebuildAll()
    {
        if (m_device == nullptr || m_fence == nullptr)
            return false;

        std::unordered_map<std::uint64_t, std::unique_ptr<MaterialGpuResource>> replacements;
        replacements.reserve(m_resources.size());
        for (const auto& [key, current] : m_resources)
        {
            auto source = MaterialManager::instance().get(current->handle);
            std::unique_ptr<MaterialGpuResource> replacement = createResource(current->handle,
                source ? std::move(source) : current->source);
            if (replacement == nullptr)
            {
                for (auto& [replacementKey, resource] : replacements)
                {
                    GE_UNUSED(replacementKey);
                    resource->constantBuffer.finalize();
                }
                LOG_ERROR("[MaterialGpuCache] Shader Hot Reload後のMaterial再生成に失敗しました");
                return false;
            }
            replacements.emplace(key, std::move(replacement));
        }

        if (m_resources.size() > m_retiredResources.max_size() - m_retiredResources.size())
        {
            LOG_ERROR("[MaterialGpuCache] Retired resource capacity is exhausted");
            return false;
        }
        m_retiredResources.reserve(m_retiredResources.size() + m_resources.size());
        for (auto& [key, resource] : m_resources)
        {
            GE_UNUSED(key);
            m_retiredResources.push_back(std::move(resource));
        }
        m_resources.clear();
        m_resources.swap(replacements);
        return true;
    }

    void MaterialGpuCache::collectGarbage()
    {
        if (m_fence == nullptr)
            return;
        std::erase_if(m_retiredResources, [this](std::unique_ptr<MaterialGpuResource>& resource)
            {
                if (resource->lastUsedFenceValue != 0 && !m_fence->isComplete(resource->lastUsedFenceValue))
                    return false;
                if (!resource->constantBuffer.finalize())
                {
                    LOG_ERROR("[MaterialGpuCache] Failed to finalize a retired material resource.");
                    return false;
                }
                return true;
            });
        for (auto iterator = m_resources.begin(); iterator != m_resources.end();)
        {
            MaterialGpuResource& resource = *iterator->second;
            const bool unloaded = MaterialManager::instance().get(resource.handle) == nullptr;
            const bool gpuComplete = resource.lastUsedFenceValue == 0
                || m_fence->isComplete(resource.lastUsedFenceValue);
            if (unloaded && gpuComplete)
            {
                if (!resource.constantBuffer.finalize())
                {
                    LOG_ERROR("[MaterialGpuCache] Failed to finalize an unloaded material resource.");
                    ++iterator;
                    continue;
                }
                iterator = m_resources.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    std::uint64_t MaterialGpuCache::makeKey(const MaterialHandle handle) noexcept
    {
        return (static_cast<std::uint64_t>(handle.generation) << 32) | handle.index;
    }

    std::unique_ptr<MaterialGpuResource> MaterialGpuCache::createResource(const MaterialHandle handle,
        std::shared_ptr<const MaterialAsset> source)
    {
        if (source == nullptr)
            source = MaterialManager::instance().get(handle);
        if (source == nullptr)
            return nullptr;

        auto resource = std::make_unique<MaterialGpuResource>();
        resource->handle = handle;
        resource->source = std::move(source);
        MaterialKeywordMask keywords = resource->source->shaderKeywords & VALID_MATERIAL_KEYWORDS;
        if (resource->source->renderState.surfaceType == MaterialSurfaceType::AlphaTest)
            keywords |= toMask(MaterialKeyword::UseAlphaTest);
        else
            keywords &= ~toMask(MaterialKeyword::UseAlphaTest);
        const std::optional<ShaderVariantID> variant = findStandardShaderVariant(keywords);
        if (!variant)
        {
            LOG_WARNING("[MaterialGpuCache] Unsupported shader keyword combination; using Standard variant: Material='{}', Mask={}",
                resource->source->name, keywords);
        }
        resource->shaderVariantID = variant.value_or(0);
        MaterialConstantData constants;
        constants.standard = MaterialParameterValues{
            .baseColor = resource->source->baseColor,
            .metallic = resource->source->metallic,
            .roughness = resource->source->roughness,
            .emissiveIntensity = resource->source->emissiveIntensity,
            .normalScale = resource->source->normalScale,
            .emissiveColor = resource->source->emissiveColor,
            .occlusionStrength = resource->source->occlusionStrength,
            .alphaCutoff = resource->source->alphaCutoff,
        };
        if (resource->source->terrain && resource->source->terrain->enabled)
        {
            const auto& terrain = *resource->source->terrain;
            constants.terrainOptions.x = static_cast<float>(terrain.layerCount);
            for (std::size_t index = 0; index < TERRAIN_LAYER_COUNT; ++index)
            {
                const auto& layer = terrain.layers[index];
                constants.terrainLayers[index] = {
                    layer.color, Vector4(layer.metallic, layer.roughness, layer.normalScale, layer.uvRepeat) };
            }
        }
        if (!resource->constantBuffer.initialize(*m_device->get(), *m_fence, CONSTANT_BUFFER_ALIGNMENT)
            || !resource->constantBuffer.write(std::as_bytes(std::span{ &constants, 1 })))
        {
            LOG_ERROR("[MaterialGpuCache] Material Constant Bufferの作成に失敗しました: {}", resource->source->name);
            return nullptr;
        }

        TextureManager& textures = TextureManager::instance();
        resource->baseColorTexture = resolveTexture(textures, resource->source->textures.baseColor,
            { .colorSpace = TextureColorSpace::SRGB }, textures.getWhiteTexture(), *resource->source, "BaseColor");
        resource->normalTexture = resolveTexture(textures, resource->source->textures.normal,
            { .colorSpace = TextureColorSpace::Linear }, textures.getNormalTexture(), *resource->source, "Normal");
        resource->metallicRoughnessTexture = resolveTexture(textures, resource->source->textures.metallicRoughness,
            { .colorSpace = TextureColorSpace::Linear }, textures.getMetallicRoughnessTexture(), *resource->source, "MetallicRoughness");
        resource->ambientOcclusionTexture = resolveTexture(textures, resource->source->textures.ambientOcclusion,
            { .colorSpace = TextureColorSpace::Linear }, textures.getWhiteTexture(), *resource->source, "AmbientOcclusion");
        resource->emissiveTexture = resolveTexture(textures, resource->source->textures.emissive,
            { .colorSpace = TextureColorSpace::SRGB }, textures.getBlackTexture(), *resource->source, "Emissive");
        for (std::size_t index = 0; index < TERRAIN_LAYER_COUNT; ++index)
        {
            const TerrainLayer* layer = resource->source->terrain && resource->source->terrain->enabled
                && index < resource->source->terrain->layerCount ? &resource->source->terrain->layers[index] : nullptr;
            resource->terrainTextures[index * 3] = resolveTexture(textures, layer ? layer->baseColorTexture : AssetGUID{},
                { .colorSpace = TextureColorSpace::SRGB }, textures.getWhiteTexture(), *resource->source, "Terrain BaseColor");
            resource->terrainTextures[index * 3 + 1] = resolveTexture(textures, layer ? layer->normalTexture : AssetGUID{},
                { .colorSpace = TextureColorSpace::Linear }, textures.getNormalTexture(), *resource->source, "Terrain Normal");
            resource->terrainTextures[index * 3 + 2] = resolveTexture(textures, layer ? layer->metallicRoughnessTexture : AssetGUID{},
                { .colorSpace = TextureColorSpace::Linear }, textures.getWhiteTexture(),
                *resource->source, "Terrain MetallicRoughness");
        }
        return resource;
    }
} // namespace Engine