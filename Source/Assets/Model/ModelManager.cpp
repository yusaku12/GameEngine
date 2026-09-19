#include "Pch.h"
#include "Assets\Model\ModelManager.h"
#include "Assets\Animation\AnimationAssetBuilder.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Assets\Material\MaterialManager.h"
#include "Assets\Model\Import\AssimpModelImporter.h"
#include "Assets\Model\Serialization\ModelSerializer.h"
#include "Graphics\Texture\TextureManager.h"

namespace Engine
{
    namespace
    {
        /**
         * @brief ModelResourceのMaterialResourceからMaterialAssetを作成し、ModelMaterialSlotに登録する。
         * @param model ModelResource
         */
        void createMaterialSlots(ModelResource& model)
        {
            if (model.materialSlots.size() < model.materials.size())
                model.materialSlots.resize(model.materials.size());

            MaterialManager& materialManager = MaterialManager::instance();
            TextureManager& textureManager = TextureManager::instance();
            for (std::size_t index = 0; index < model.materials.size(); ++index)
            {
                const MaterialResource& legacyMaterial = model.materials[index];
                ModelMaterialSlot& slot = model.materialSlots[index];
                if (slot.name.empty())
                    slot.name = legacyMaterial.name;
                if (slot.defaultMaterialGuid.isValid()
                    && materialManager.findByGuid(slot.defaultMaterialGuid).isValid())
                    continue;

                MaterialAsset material;
                material.guid = slot.defaultMaterialGuid;
                material.name = legacyMaterial.name;
                material.baseColor = legacyMaterial.baseColor;
                material.baseColor.w *= legacyMaterial.opacity;
                material.metallic = legacyMaterial.metallic;
                material.roughness = legacyMaterial.roughness;
                material.emissiveColor = legacyMaterial.emissive;
                if (legacyMaterial.opacity < 1.0f)
                    material.renderState.surfaceType = MaterialSurfaceType::Transparent;
                const auto registerTexture = [&model, &textureManager](const std::string& sourcePath,
                    std::filesystem::path& storedPath)
                    {
                        if (sourcePath.empty())
                            return AssetGUID{};
                        std::filesystem::path texturePath = sourcePath;
                        if (texturePath.is_relative() && !model.sourcePath.empty())
                            texturePath = model.sourcePath.parent_path() / texturePath;
                        storedPath = texturePath.lexically_normal();
                        return textureManager.registerAssetPath(texturePath);
                    };
                material.textures.baseColor = registerTexture(legacyMaterial.textures.baseColor, material.textures.baseColorPath);
                material.textures.normal = registerTexture(legacyMaterial.textures.normal, material.textures.normalPath);
                material.textures.metallicRoughness = registerTexture(legacyMaterial.textures.metallicRoughness, material.textures.metallicRoughnessPath);
                material.textures.ambientOcclusion = registerTexture(legacyMaterial.textures.ambientOcclusion, material.textures.ambientOcclusionPath);
                material.textures.emissive = registerTexture(legacyMaterial.textures.emissive, material.textures.emissivePath);
                const MaterialHandle handle = materialManager.create(std::move(material));
                const std::shared_ptr<const MaterialAsset> created = materialManager.get(handle);
                if (created != nullptr)
                    slot.defaultMaterialGuid = created->guid;
            }
        }

        /**
         * @brief ModelResourceのAnimationResourceからAnimationAssetを作成し、GUIDを登録する。
         * @param model ModelResource
         * @return 成功した場合はtrue
         */
        bool migrateAnimationAssets(ModelResource& model)
        {
            if (!model.skeleton)
                return model.animations.empty();

            AnimationAssetBuilder builder;
            SkeletonAsset skeleton;
            if (!builder.buildSkeleton(*model.skeleton, model.sourcePath, skeleton))
                return false;
            if (model.skeletonAssetGuid.isValid())
                skeleton.guid = model.skeletonAssetGuid;

            AnimationAssetManager& manager = AnimationAssetManager::instance();
            const SkeletonHandle skeletonHandle = manager.createSkeleton(std::move(skeleton));
            const auto skeletonSnapshot = manager.getSkeleton(skeletonHandle);
            if (skeletonSnapshot == nullptr)
                return false;
            model.skeletonAssetGuid = skeletonSnapshot->guid;

            std::vector<AssetGUID> clipGuids;
            clipGuids.reserve(model.animations.size());
            for (std::size_t index = 0; index < model.animations.size(); ++index)
            {
                AnimationClipAsset clip;
                if (!builder.buildClip(model.animations[index], *skeletonSnapshot, model.sourcePath, clip))
                    return false;
                if (index < model.animationClipGuids.size() && model.animationClipGuids[index].isValid())
                    clip.guid = model.animationClipGuids[index];
                const AnimationClipHandle clipHandle = manager.createClip(std::move(clip));
                const auto clipSnapshot = manager.getClip(clipHandle);
                if (clipSnapshot == nullptr)
                    return false;
                clipGuids.push_back(clipSnapshot->guid);
            }
            model.animationClipGuids = std::move(clipGuids);
            return true;
        }
    }

    ModelManager& ModelManager::instance() noexcept
    {
        static ModelManager manager;
        return manager;
    }

    ModelHandle ModelManager::load(const std::filesystem::path& path)
    {
        const std::filesystem::path normalizedPath = normalizePath(path);
        if (normalizedPath.empty())
        {
            LOG_ERROR("[ModelManager] モデルPathが無効です");
            return ModelHandle::Invalid();
        }

        {
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_pathCache.find(normalizedPath); found != m_pathCache.end())
                return found->second;
        }

        std::string extension = normalizedPath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });

        ModelResource model;
        if (extension == ".model" || extension == ".mdl")
        {
            Serialization::ModelSerializer serializer;
            if (!serializer.load(normalizedPath, model))
            {
                LOG_ERROR("[ModelManager] モデルのロードに失敗しました: {}", normalizedPath.string());
                return ModelHandle::Invalid();
            }
        }
        else
        {
            std::shared_ptr<ModelResource> imported = AssimpModelImporter{}.importModel(normalizedPath);
            if (imported == nullptr)
                return ModelHandle::Invalid();
            model = std::move(*imported);
        }
        model.sourcePath = normalizedPath;
        return create(std::move(model), normalizedPath);
    }

    ModelHandle ModelManager::create(ModelResource model, const std::filesystem::path& cacheKey)
    {
        const std::filesystem::path normalizedKey = cacheKey.empty() ? std::filesystem::path{} : normalizePath(cacheKey);
        if (!normalizedKey.empty())
        {
            const std::scoped_lock lock(m_mutex);
            if (const auto found = m_pathCache.find(normalizedKey); found != m_pathCache.end())
                return found->second;
        }

        if (!migrateAnimationAssets(model))
        {
            LOG_ERROR("[ModelManager] Animation Asset migration failed: {}", model.sourcePath.string());
            return ModelHandle::Invalid();
        }

        // モデルのMaterialResourceからMaterialAssetを作成し、ModelMaterialSlotに登録する
        createMaterialSlots(model);

        const std::scoped_lock lock(m_mutex);
        if (!normalizedKey.empty())
        {
            if (const auto found = m_pathCache.find(normalizedKey); found != m_pathCache.end())
                return found->second;
        }
        if (m_nextGeneration == 0)
            ++m_nextGeneration;

        const ModelHandle handle{
            .index = static_cast<std::uint32_t>(m_entries.size()),
            .generation = m_nextGeneration++,
        };
        m_entries.push_back(Entry{
            .resource = std::make_shared<const ModelResource>(std::move(model)),
            .path = normalizedKey,
            .generation = handle.generation,
            });
        if (!normalizedKey.empty())
            m_pathCache.emplace(normalizedKey, handle);
        return handle;
    }

    bool ModelManager::save(const ModelHandle handle, const std::filesystem::path& path) const
    {
        const std::filesystem::path normalizedPath = normalizePath(path);
        if (normalizedPath.empty())
            return false;

        const std::shared_ptr<const ModelResource> model = get(handle);
        return model != nullptr && Serialization::ModelSerializer{}.save(normalizedPath, *model);
    }

    std::shared_ptr<const ModelResource> ModelManager::get(const ModelHandle handle) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_entries.size())
            return nullptr;
        const Entry& entry = m_entries[handle.index];
        return entry.generation == handle.generation ? entry.resource : nullptr;
    }

    void ModelManager::unload(const ModelHandle handle) noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_entries.size())
            return;

        Entry& entry = m_entries[handle.index];
        if (entry.generation != handle.generation || entry.resource == nullptr)
            return;
        if (!entry.path.empty())
            m_pathCache.erase(entry.path);
        entry.resource.reset();
        entry.path.clear();
        entry.generation = 0;
    }

    void ModelManager::clear() noexcept
    {
        const std::scoped_lock lock(m_mutex);
        m_entries.clear();
        m_pathCache.clear();
    }

    std::filesystem::path ModelManager::normalizePath(const std::filesystem::path& path)
    {
        if (path.empty())
            return {};
        std::error_code error;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
        if (error)
        {
            error.clear();
            normalized = std::filesystem::absolute(path, error);
        }
        return error ? std::filesystem::path{} : normalized.lexically_normal();
    }
} // namespace Engine