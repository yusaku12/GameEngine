#include "Pch.h"
#include "Graphics\Material\MaterialGpuCache.h"
#include "Graphics\DirectX12\Device.h"
#include "Graphics\DirectX12\Fence.h"
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
        m_resources.clear();
        m_retiredResources.clear();
        m_device = nullptr;
        m_fence = nullptr;
        return succeeded;
    }

    MaterialGpuResource* MaterialGpuCache::getOrCreate(const MaterialHandle handle)
    {
        if (m_device == nullptr || m_fence == nullptr)
            return nullptr;
        const std::uint64_t key = makeKey(handle);
        if (const auto found = m_resources.find(key); found != m_resources.end())
        {
            const std::shared_ptr<const MaterialAsset> current = MaterialManager::instance().get(handle);
            if (current == nullptr || found->second->source == current)
                return found->second.get();

            std::unique_ptr<MaterialGpuResource> replacement = createResource(handle);
            if (replacement == nullptr)
                return found->second.get();
            m_retiredResources.push_back(std::move(found->second));
            found->second = std::move(replacement);
            return found->second.get();
        }

        std::unique_ptr<MaterialGpuResource> resource = createResource(handle);
        if (resource == nullptr)
        {
            const MaterialHandle errorMaterial = MaterialManager::instance().getErrorMaterial();
            if (handle != errorMaterial)
                return getOrCreate(errorMaterial);
            return nullptr;
        }
        MaterialGpuResource* const result = resource.get();
        m_resources.emplace(key, std::move(resource));
        return result;
    }

    bool MaterialGpuCache::markUsed(const MaterialHandle handle, const std::uint64_t fenceValue)
    {
        const auto found = m_resources.find(makeKey(handle));
        if (found == m_resources.end())
            return false;
        found->second->lastUsedFenceValue = fenceValue;
        return found->second->constantBuffer.markUsed(fenceValue);
    }

    bool MaterialGpuCache::rebuildAll()
    {
        if (m_device == nullptr || m_fence == nullptr)
            return false;

        std::unordered_map<std::uint64_t, std::unique_ptr<MaterialGpuResource>> replacements;
        replacements.reserve(m_resources.size());
        for (const auto& [key, current] : m_resources)
        {
            std::unique_ptr<MaterialGpuResource> replacement = createResource(current->handle);
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

        for (auto& [key, resource] : m_resources)
        {
            GE_UNUSED(key);
            resource->constantBuffer.finalize();
        }
        for (std::unique_ptr<MaterialGpuResource>& resource : m_retiredResources)
            resource->constantBuffer.finalize();
        m_retiredResources.clear();
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
                resource->constantBuffer.finalize();
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
                resource.constantBuffer.finalize();
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

    std::unique_ptr<MaterialGpuResource> MaterialGpuCache::createResource(const MaterialHandle handle)
    {
        std::shared_ptr<const MaterialAsset> source = MaterialManager::instance().get(handle);
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
        const MaterialParameterValues constants{
            .baseColor = resource->source->baseColor,
            .metallic = resource->source->metallic,
            .roughness = resource->source->roughness,
            .emissiveIntensity = resource->source->emissiveIntensity,
            .normalScale = resource->source->normalScale,
            .emissiveColor = resource->source->emissiveColor,
            .occlusionStrength = resource->source->occlusionStrength,
            .alphaCutoff = resource->source->alphaCutoff,
        };
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
        return resource;
    }
} // namespace Engine