#include "Pch.h"
#include "Assets\Model\ModelManager.h"
#include "Assets\Animation\AnimationAssetBuilder.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Assets\Material\MaterialManager.h"
#include "Assets\Model\Import\FbxModelImporter.h"
#include "Assets\Model\Serialization\ModelSerializer.h"
#include "Graphics\Texture\TextureManager.h"

namespace Engine
{
    namespace
    {
        void registerMaterialTexturePaths(const MaterialAsset& material, const std::filesystem::path& modelPath)
        {
            TextureManager& textures = TextureManager::instance();
            const std::array references = {
                std::pair{ material.textures.baseColor, material.textures.baseColorPath },
                std::pair{ material.textures.normal, material.textures.normalPath },
                std::pair{ material.textures.metallicRoughness, material.textures.metallicRoughnessPath },
                std::pair{ material.textures.ambientOcclusion, material.textures.ambientOcclusionPath },
                std::pair{ material.textures.emissive, material.textures.emissivePath },
            };
            for (const auto& [guid, storedPath] : references)
            {
                if (!guid.isValid() || storedPath.empty())
                    continue;
                std::filesystem::path path = storedPath;
                if (path.is_relative() && !modelPath.empty())
                    path = modelPath.parent_path() / path;
                if (!textures.registerAssetPath(guid, path.lexically_normal()))
                    LOG_WARNING("[ModelManager] Embedded Material Texture path registration failed: {}", path.string());
            }
        }

        /**
         * @brief ModelResourceのMaterialResourceからMaterialAssetを作成し、ModelMaterialSlotに登録する。
         * @param model ModelResource
         */
        bool createMaterialSlots(ModelResource& model)
        {
            if (model.materialSlots.size() < model.materials.size())
                model.materialSlots.resize(model.materials.size());
            if (model.embeddedMaterials.size() < model.materialSlots.size())
                model.embeddedMaterials.resize(model.materialSlots.size());

            MaterialManager& materialManager = MaterialManager::instance();
            TextureManager& textureManager = TextureManager::instance();
            for (std::size_t index = 0; index < model.materialSlots.size(); ++index)
            {
                const MaterialResource legacyFallback;
                const MaterialResource& legacyMaterial = index < model.materials.size()
                    ? model.materials[index] : legacyFallback;
                ModelMaterialSlot& slot = model.materialSlots[index];
                if (slot.name.empty())
                    slot.name = legacyMaterial.name;
                if (!slot.defaultMaterialPath.empty())
                {
                    std::filesystem::path materialPath = slot.defaultMaterialPath;
                    if (materialPath.is_relative() && !model.sourcePath.empty())
                        materialPath = model.sourcePath.parent_path() / materialPath;
                    materialPath = materialPath.lexically_normal();
                    const MaterialHandle savedMaterial = materialManager.load(materialPath);
                    const std::shared_ptr<const MaterialAsset> snapshot = materialManager.get(savedMaterial);
                    if (snapshot != nullptr && (!slot.defaultMaterialGuid.isValid()
                        || snapshot->guid == slot.defaultMaterialGuid))
                    {
                        slot.defaultMaterialGuid = snapshot->guid;
                        slot.defaultMaterialPath = materialPath;
                        model.embeddedMaterials[index] = *snapshot;
                        continue;
                    }
                    LOG_WARNING("[ModelManager] Material Assetの参照を解決できません: {}", materialPath.string());
                }
                if (slot.defaultMaterialGuid.isValid())
                {
                    const MaterialHandle existing = materialManager.findByGuid(slot.defaultMaterialGuid);
                    if (index < model.embeddedMaterials.size()
                        && model.embeddedMaterials[index].guid == slot.defaultMaterialGuid)
                    {
                        if (existing.isValid())
                            materialManager.update(existing, model.embeddedMaterials[index]);
                        else
                            materialManager.create(model.embeddedMaterials[index]);
                        const MaterialHandle restored = materialManager.findByGuid(slot.defaultMaterialGuid);
                        const std::shared_ptr<const MaterialAsset> snapshot = materialManager.get(restored);
                        if (snapshot != nullptr)
                        {
                            model.embeddedMaterials[index] = *snapshot;
                            registerMaterialTexturePaths(*snapshot, model.sourcePath);
                            continue;
                        }
                    }
                    else if (existing.isValid())
                    {
                        const std::shared_ptr<const MaterialAsset> snapshot = materialManager.get(existing);
                        if (snapshot != nullptr)
                        {
                            model.embeddedMaterials[index] = *snapshot;
                            continue;
                        }
                    }
                }

                if (model.embeddedMaterials[index].guid.isValid())
                {
                    const AssetGUID embeddedGuid = model.embeddedMaterials[index].guid;
                    if (!slot.defaultMaterialGuid.isValid())
                        slot.defaultMaterialGuid = embeddedGuid;
                    if (slot.defaultMaterialGuid != embeddedGuid)
                    {
                        LOG_ERROR("[ModelManager] Embedded Material GUIDとSlot GUIDが一致しません: {}", slot.name);
                        return false;
                    }
                    const MaterialHandle handle = materialManager.create(model.embeddedMaterials[index]);
                    const std::shared_ptr<const MaterialAsset> snapshot = materialManager.get(handle);
                    if (snapshot != nullptr)
                    {
                        model.embeddedMaterials[index] = *snapshot;
                        registerMaterialTexturePaths(*snapshot, model.sourcePath);
                        continue;
                    }
                }

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
                {
                    slot.defaultMaterialGuid = created->guid;
                    model.embeddedMaterials[index] = *created;
                }
            }
            return true;
        }

        /**
         * @brief ModelResourceのAnimationResourceからAnimationAssetを作成し、GUIDを登録する。
         * @param model ModelResource
         * @return 成功した場合はtrue
         */
        bool migrateAnimationAssets(ModelResource& model)
        {
            if (!model.skeleton)
                return model.animations.empty() && model.animationClipGuids.empty();

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

            const std::vector<AssetGUID> previousGuids = model.animationClipGuids;
            const std::vector<std::filesystem::path> previousPaths = model.animationClipPaths;
            std::vector<AssetGUID> clipGuids;
            std::vector<std::filesystem::path> clipPaths;
            const std::size_t referenceCount = std::max(previousGuids.size(), previousPaths.size());
            clipGuids.reserve(std::max(model.animations.size(), referenceCount));
            clipPaths.reserve(std::max(model.animations.size(), referenceCount));
            for (std::size_t index = 0; index < model.animations.size(); ++index)
            {
                const AssetGUID savedGuid = index < previousGuids.size() ? previousGuids[index] : AssetGUID{};
                std::filesystem::path savedPath = index < previousPaths.size()
                    ? previousPaths[index] : std::filesystem::path{};
                if (!savedPath.empty())
                {
                    if (savedPath.is_relative() && !model.sourcePath.empty())
                        savedPath = model.sourcePath.parent_path() / savedPath;
                    savedPath = savedPath.lexically_normal();
                    const AnimationClipHandle handle = manager.loadClip(savedPath);
                    const std::shared_ptr<const AnimationClipAsset> snapshot = manager.getClip(handle);
                    if (snapshot == nullptr || (savedGuid.isValid() && snapshot->guid != savedGuid)
                        || snapshot->skeletonGuid != skeletonSnapshot->guid
                        || snapshot->skeletonSignature != skeletonSnapshot->signature)
                    {
                        LOG_ERROR("[ModelManager] Animation Clip参照を解決できません: {}", savedPath.string());
                        return false;
                    }
                    clipGuids.push_back(snapshot->guid);
                    clipPaths.push_back(savedPath);
                    continue;
                }

                AnimationClipAsset clip;
                if (!builder.buildClip(model.animations[index], *skeletonSnapshot, model.sourcePath, clip))
                    return false;
                if (savedGuid.isValid())
                    clip.guid = savedGuid;
                const AnimationClipHandle clipHandle = manager.createClip(std::move(clip));
                const auto clipSnapshot = manager.getClip(clipHandle);
                if (clipSnapshot == nullptr)
                    return false;
                clipGuids.push_back(clipSnapshot->guid);
                clipPaths.push_back(manager.getClipPath(clipHandle));
            }
            for (std::size_t index = model.animations.size(); index < referenceCount; ++index)
            {
                if (index >= previousGuids.size() || !previousGuids[index].isValid())
                    return false;
                std::filesystem::path clipPath = index < previousPaths.size()
                    ? previousPaths[index] : std::filesystem::path{};
                if (clipPath.is_relative() && !model.sourcePath.empty())
                    clipPath = model.sourcePath.parent_path() / clipPath;
                clipPath = clipPath.lexically_normal();
                const AnimationClipHandle handle = clipPath.empty()
                    ? manager.findClipByGuid(previousGuids[index]) : manager.loadClip(clipPath);
                const std::shared_ptr<const AnimationClipAsset> snapshot = manager.getClip(handle);
                if (snapshot == nullptr || snapshot->guid != previousGuids[index]
                    || snapshot->skeletonGuid != skeletonSnapshot->guid
                    || snapshot->skeletonSignature != skeletonSnapshot->signature)
                {
                    LOG_ERROR("[ModelManager] 関連Animation Clipが見つからないかSkeletonに適合しません: {}",
                        clipPath.string());
                    return false;
                }
                clipGuids.push_back(snapshot->guid);
                clipPaths.push_back(clipPath.empty() ? manager.getClipPath(handle) : clipPath);
            }
            model.animationClipGuids = std::move(clipGuids);
            model.animationClipPaths = std::move(clipPaths);
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
            std::shared_ptr<ModelResource> imported = FbxModelImporter{}.importModel(normalizedPath);
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
        if (!createMaterialSlots(model))
        {
            LOG_ERROR("[ModelManager] Material Asset migration failed: {}", model.sourcePath.string());
            return ModelHandle::Invalid();
        }

        try
        {
            const std::shared_ptr<const ModelResource> resource =
                std::make_shared<const ModelResource>(std::move(model));
            std::filesystem::path storedPath = normalizedKey;
            const std::scoped_lock lock(m_mutex);
            if (!normalizedKey.empty())
            {
                if (const auto found = m_pathCache.find(normalizedKey); found != m_pathCache.end())
                    return found->second;
            }
            if (m_nextGeneration == 0 || m_entries.size() >= ModelHandle::INVALID_INDEX
                || m_entries.size() == m_entries.max_size()
                || (!normalizedKey.empty() && m_pathCache.size() == m_pathCache.max_size()))
            {
                LOG_ERROR("[ModelManager] Model cache capacity exhausted");
                return ModelHandle::Invalid();
            }

            m_entries.reserve(m_entries.size() + 1);
            if (!normalizedKey.empty())
                m_pathCache.reserve(m_pathCache.size() + 1);

            const ModelHandle handle{
                .index = static_cast<std::uint32_t>(m_entries.size()),
                .generation = m_nextGeneration,
            };
            bool pathInserted = false;
            try
            {
                if (!normalizedKey.empty())
                {
                    pathInserted = m_pathCache.emplace(storedPath, handle).second;
                    if (!pathInserted)
                        return m_pathCache.at(normalizedKey);
                }
                m_entries.push_back(Entry{
                    .resource = resource,
                    .path = std::move(storedPath),
                    .generation = handle.generation,
                    });
            }
            catch (...)
            {
                if (pathInserted)
                    m_pathCache.erase(normalizedKey);
                throw;
            }
            ++m_nextGeneration;
            return handle;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ModelManager] Failed to allocate memory while registering Model.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ModelManager] Model cache capacity exceeded.");
        }
        return ModelHandle::Invalid();
    }

    bool ModelManager::save(const ModelHandle handle, const std::filesystem::path& path)
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_entries.size())
            return false;
        Entry& entry = m_entries[handle.index];
        if (entry.generation != handle.generation || entry.resource == nullptr)
            return false;
        const std::filesystem::path normalizedPath = path.empty() ? entry.path : normalizePath(path);
        if (normalizedPath.empty())
            return false;
        std::string extension = normalizedPath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (extension != ".model" && extension != ".mdl")
            return false;
        if (const auto conflict = m_pathCache.find(normalizedPath);
            conflict != m_pathCache.end() && conflict->second != handle)
        {
            return false;
        }
        ModelResource snapshot = *entry.resource;
        MaterialManager& materials = MaterialManager::instance();
        const bool saveAs = normalizedPath != entry.path;
        snapshot.embeddedMaterials.resize(snapshot.materialSlots.size());
        for (std::size_t index = 0; index < snapshot.materialSlots.size(); ++index)
        {
            ModelMaterialSlot& slot = snapshot.materialSlots[index];
            const MaterialHandle material = materials.findByGuid(slot.defaultMaterialGuid);
            const std::shared_ptr<const MaterialAsset> materialSnapshot = materials.get(material);
            const std::filesystem::path materialPath = materials.getPath(material);
            MaterialAsset embedded;
            if (materialSnapshot != nullptr)
                embedded = *materialSnapshot;
            else if (snapshot.embeddedMaterials[index].guid.isValid()
                && (!slot.defaultMaterialGuid.isValid()
                    || snapshot.embeddedMaterials[index].guid == slot.defaultMaterialGuid))
                embedded = snapshot.embeddedMaterials[index];
            else
            {
                LOG_ERROR("[ModelManager] Model SlotのMaterial snapshotを取得できません: {}", slot.name);
                return false;
            }

            if (saveAs || !materialPath.empty() || !slot.defaultMaterialPath.empty())
                embedded.guid = AssetGUID::generate();
            if (!embedded.guid.isValid())
            {
                LOG_ERROR("[ModelManager] Model Slot MaterialのGUIDが無効です: {}", slot.name);
                return false;
            }
            slot.defaultMaterialGuid = embedded.guid;
            slot.defaultMaterialPath.clear();
            snapshot.embeddedMaterials[index] = std::move(embedded);
        }
        snapshot.animationClipPaths.resize(snapshot.animationClipGuids.size());
        AnimationAssetManager& animations = AnimationAssetManager::instance();
        for (std::size_t index = 0; index < snapshot.animationClipGuids.size(); ++index)
        {
            if (snapshot.animationClipPaths[index].empty())
            {
                const AnimationClipHandle clip = animations.findClipByGuid(snapshot.animationClipGuids[index]);
                snapshot.animationClipPaths[index] = animations.getClipPath(clip);
            }
            if (index >= snapshot.animations.size() && snapshot.animationClipPaths[index].empty())
            {
                LOG_ERROR("[ModelManager] Model保存前に外部Animation ClipをAssetとして保存してください");
                return false;
            }
        }
        std::shared_ptr<const ModelResource> updatedResource;
        std::filesystem::path stagedPath = normalizedPath;
        try
        {
            updatedResource = std::make_shared<const ModelResource>(snapshot);
            if (normalizedPath != entry.path)
            {
                if (m_pathCache.size() == m_pathCache.max_size())
                    return false;
                m_pathCache.reserve(m_pathCache.size() + 1);
                const auto [cacheEntry, inserted] = m_pathCache.emplace(stagedPath, handle);
                GE_UNUSED(cacheEntry);
                if (!inserted)
                    return false;
            }
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[ModelManager] Failed to allocate memory while preparing Model save.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[ModelManager] Model path cache capacity exceeded while saving.");
            return false;
        }

        const bool pathStaged = normalizedPath != entry.path;
        if (!Serialization::ModelSerializer{}.save(normalizedPath, snapshot))
        {
            if (pathStaged)
                m_pathCache.erase(stagedPath);
            return false;
        }
        for (std::size_t index = 0; index < snapshot.embeddedMaterials.size(); ++index)
        {
            const MaterialAsset& material = snapshot.embeddedMaterials[index];
            const MaterialHandle materialHandle = materials.findByGuid(material.guid);
            if (materialHandle.isValid())
                materials.update(materialHandle, material);
            else if (!materials.create(material).isValid())
            {
                if (pathStaged)
                    m_pathCache.erase(stagedPath);
                return false;
            }
            registerMaterialTexturePaths(material, normalizedPath);
        }
        entry.resource = std::move(updatedResource);
        if (pathStaged)
        {
            entry.path.swap(stagedPath);
            if (!stagedPath.empty())
                m_pathCache.erase(stagedPath);
        }
        return true;
    }

    bool ModelManager::associateAnimationClip(const ModelHandle model, const AnimationClipHandle clip)
    {
        AnimationAssetManager& animations = AnimationAssetManager::instance();
        const std::shared_ptr<const AnimationClipAsset> clipAsset = animations.getClip(clip);
        if (clipAsset == nullptr)
            return false;

        const std::scoped_lock lock(m_mutex);
        if (!model.isValid() || model.index >= m_entries.size())
            return false;
        Entry& entry = m_entries[model.index];
        if (entry.generation != model.generation || entry.resource == nullptr)
            return false;
        const std::shared_ptr<const ModelResource>& current = entry.resource;
        if (!current->skeletonAssetGuid.isValid() || current->skeletonAssetGuid != clipAsset->skeletonGuid
            || current->animationClipGuids.size() != current->animationClipPaths.size())
            return false;

        ModelResource updated = *current;
        const auto found = std::find(updated.animationClipGuids.begin(), updated.animationClipGuids.end(), clipAsset->guid);
        const std::filesystem::path clipPath = animations.getClipPath(clip);
        if (found == updated.animationClipGuids.end())
        {
            updated.animationClipGuids.push_back(clipAsset->guid);
            updated.animationClipPaths.push_back(clipPath);
        }
        else
        {
            const std::size_t index = static_cast<std::size_t>(found - updated.animationClipGuids.begin());
            if (index >= updated.animationClipPaths.size())
                return false;
            if (index < updated.animations.size())
                return true;
            updated.animationClipPaths[index] = clipPath;
        }
        entry.resource = std::make_shared<const ModelResource>(std::move(updated));
        return true;
    }

    MaterialHandle ModelManager::setMaterialSlotMaterial(const ModelHandle model, const std::size_t slotIndex, MaterialAsset material)
    {
        if (!material.guid.isValid())
            return MaterialHandle::Invalid();
        material.guid = AssetGUID::generate();
        const MaterialAsset embedded = material;
        const std::scoped_lock lock(m_mutex);
        if (!model.isValid() || model.index >= m_entries.size())
            return MaterialHandle::Invalid();
        Entry& entry = m_entries[model.index];
        if (entry.generation != model.generation || entry.resource == nullptr
            || slotIndex >= entry.resource->materialSlots.size())
            return MaterialHandle::Invalid();

        MaterialManager& materials = MaterialManager::instance();
        const MaterialHandle materialHandle = materials.create(std::move(material));
        if (!materials.get(materialHandle))
            return MaterialHandle::Invalid();
        registerMaterialTexturePaths(embedded, {});

        ModelResource updated = *entry.resource;
        if (updated.embeddedMaterials.size() < updated.materialSlots.size())
            updated.embeddedMaterials.resize(updated.materialSlots.size());
        updated.materialSlots[slotIndex].defaultMaterialGuid = embedded.guid;
        updated.materialSlots[slotIndex].defaultMaterialPath.clear();
        updated.embeddedMaterials[slotIndex] = embedded;
        entry.resource = std::make_shared<const ModelResource>(std::move(updated));
        return materialHandle;
    }

    std::vector<AnimationClipHandle> ModelManager::importAnimationClips(const ModelHandle model, const std::filesystem::path& path)
    {
        const std::shared_ptr<const ModelResource> source = get(model);
        if (source == nullptr || !source->skeletonAssetGuid.isValid())
            return {};

        AnimationAssetManager& assets = AnimationAssetManager::instance();
        const SkeletonHandle skeletonHandle = assets.findSkeletonByGuid(source->skeletonAssetGuid);
        const std::shared_ptr<const SkeletonAsset> skeleton = assets.getSkeleton(skeletonHandle);
        if (skeleton == nullptr)
            return {};

        const std::vector<AnimationResource> imported = FbxModelImporter{}.importAnimations(path);
        if (imported.empty())
            return {};

        struct ImportedClip
        {
            AnimationResource animation;
            AssetGUID guid;
            AnimationClipHandle handle;
        };
        std::vector<ImportedClip> clips;
        clips.reserve(imported.size());
        AnimationAssetBuilder builder;
        for (const AnimationResource& animation : imported)
        {
            AnimationClipAsset asset;
            if (!builder.buildClip(animation, *skeleton, path, asset))
                return {};
            const AssetGUID guid = asset.guid;
            const AnimationClipHandle handle = assets.createClip(std::move(asset));
            const std::shared_ptr<const AnimationClipAsset> snapshot = assets.getClip(handle);
            if (snapshot == nullptr || snapshot->guid != guid
                || snapshot->skeletonGuid != skeleton->guid || snapshot->skeletonSignature != skeleton->signature)
                return {};
            clips.push_back({ animation, guid, handle });
        }

        std::vector<AnimationClipHandle> handles;
        handles.reserve(clips.size());
        for (const ImportedClip& clip : clips)
            handles.push_back(clip.handle);

        const std::scoped_lock lock(m_mutex);
        if (!model.isValid() || model.index >= m_entries.size())
            return {};
        Entry& entry = m_entries[model.index];
        if (entry.generation != model.generation || entry.resource != source)
            return {};

        ModelResource updated = *source;
        if (updated.animationClipGuids.size() != updated.animationClipPaths.size()
            || updated.animationClipGuids.size() < updated.animations.size())
            return {};
        for (const ImportedClip& clip : clips)
        {
            const auto found = std::find(updated.animationClipGuids.begin(), updated.animationClipGuids.end(), clip.guid);
            if (found != updated.animationClipGuids.end() && found < updated.animationClipGuids.begin()
                + static_cast<std::ptrdiff_t>(updated.animations.size()))
            {
                const std::size_t index = static_cast<std::size_t>(found - updated.animationClipGuids.begin());
                updated.animations[index] = clip.animation;
                updated.animationClipPaths[index].clear();
                continue;
            }
            if (found != updated.animationClipGuids.end())
            {
                const std::size_t externalIndex = static_cast<std::size_t>(found - updated.animationClipGuids.begin());
                updated.animationClipGuids.erase(found);
                updated.animationClipPaths.erase(updated.animationClipPaths.begin()
                    + static_cast<std::ptrdiff_t>(externalIndex));
            }
            const std::size_t insertIndex = updated.animations.size();
            updated.animations.push_back(clip.animation);
            updated.animationClipGuids.insert(updated.animationClipGuids.begin()
                + static_cast<std::ptrdiff_t>(insertIndex), clip.guid);
            updated.animationClipPaths.insert(updated.animationClipPaths.begin()
                + static_cast<std::ptrdiff_t>(insertIndex), {});
        }
        entry.resource = std::make_shared<const ModelResource>(std::move(updated));
        return handles;
    }

    std::shared_ptr<const ModelResource> ModelManager::get(const ModelHandle handle) const noexcept
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_entries.size())
            return nullptr;
        const Entry& entry = m_entries[handle.index];
        return entry.generation == handle.generation ? entry.resource : nullptr;
    }

    std::filesystem::path ModelManager::getPath(const ModelHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        if (!handle.isValid() || handle.index >= m_entries.size())
            return {};
        const Entry& entry = m_entries[handle.index];
        return entry.generation == handle.generation && entry.resource != nullptr
            ? entry.path : std::filesystem::path{};
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