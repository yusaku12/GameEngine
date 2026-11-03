#include "Pch.h"
#include "Core\GameObject\Component\TerrainComponent.h"
#include "Assets\Material\MaterialManager.h"
#include "Assets\Model\ModelManager.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Scene\SceneManager.h"
#include "Core\System\Dialog.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Generated\FlatBuffers\TerrainComponent_generated.h"
#include "Graphics\Renderer\ModelRenderSubmission.h"
#include "Terrain\TerrainMaterialSerialization.h"
#include "Graphics\Texture\TextureManager.h"
#include "Physics\Collider\TerrainColliderComponent.h"
#include <imgui.h>

namespace Engine
{
    namespace
    {
        const auto terrainType = ComponentRegistry::instance().registerType<TerrainComponent>(
            "Terrain", false, false, true);

        struct PendingTerrainResources
        {
            ModelHandle model;
            MaterialHandle material;
            ~PendingTerrainResources()
            {
                if (model.isValid())
                    ModelManager::instance().unload(model);
                if (material.isValid())
                    MaterialManager::instance().unload(material);
            }
        };
    }

    TerrainComponent::TerrainComponent()
        : m_objectID(allocateModelRenderObjectID())
    {
        m_heightmap.createFlat(65, 65);
    }

    TerrainComponent::~TerrainComponent()
    {
        cancelGeneration();
        if (m_model.isValid())
            ModelManager::instance().unload(m_model);
        if (m_material.isValid())
            MaterialManager::instance().unload(m_material);
    }

    bool TerrainComponent::setTerrain(const Heightmap& heightmap, const TerrainMeshSettings& settings)
    {
        return rebuild(heightmap, settings, m_materialSettings, m_chunkSettings, m_lodSettings);
    }

    bool TerrainComponent::setMaterialSettings(const TerrainMaterialSettings& settings)
    {
        return rebuild(m_heightmap, m_settings, settings, m_chunkSettings, m_lodSettings);
    }

    bool TerrainComponent::setChunkSettings(const TerrainChunkSettings& settings)
    {
        return rebuild(m_heightmap, m_settings, m_materialSettings, settings, m_lodSettings);
    }

    bool TerrainComponent::setLodSettings(const TerrainLodSettings& settings)
    {
        return rebuild(m_heightmap, m_settings, m_materialSettings, m_chunkSettings, settings);
    }

    bool TerrainComponent::rebuild(const Heightmap& heightmap, const TerrainMeshSettings& settings,
        const TerrainMaterialSettings& surface, const TerrainChunkSettings& chunkSettings, const TerrainLodSettings& lodSettings)
    {
        auto request = makeBuildRequest();
        request.heightmap = heightmap;
        request.mesh = settings;
        request.material = surface;
        request.chunks = chunkSettings;
        request.lod = lodSettings;
        if (!resolveTerrainMaterialTextures(request.material))
            return false;
        TerrainBuildResult result;
        if (!buildTerrain(std::move(request), result) || !applyBuild(std::move(result)))
            return false;
        cancelGeneration();
        return true;
    }

    TerrainBuildRequest TerrainComponent::makeBuildRequest() const
    {
        TerrainBuildRequest request;
        request.mesh = m_settings;
        request.material = m_materialSettings;
        request.chunks = m_chunkSettings;
        request.lod = m_lodSettings;
        request.noise = m_noiseSettings;
        request.erosion = m_erosionSettings;
        request.collision = getGameObject() && getGameObject()->hasComponent<TerrainColliderComponent>();
        return request;
    }

    bool TerrainComponent::requestTerrain(const Heightmap& heightmap, const TerrainMeshSettings& mesh)
    {
        auto request = makeBuildRequest();
        request.heightmap = heightmap;
        request.mesh = mesh;
        return queueBuild(std::move(request));
    }

    bool TerrainComponent::requestNoise(const TerrainNoiseSettings& noise, const TerrainMeshSettings& mesh)
    {
        auto request = makeBuildRequest();
        request.source = TerrainBuildSource::Noise;
        request.noise = noise;
        request.mesh = mesh;
        return queueBuild(std::move(request));
    }

    bool TerrainComponent::requestFlat(const std::uint32_t width, const std::uint32_t height, const float value,
        const TerrainMeshSettings& mesh)
    {
        auto request = makeBuildRequest();
        request.source = TerrainBuildSource::Flat;
        request.flatWidth = width;
        request.flatHeight = height;
        request.flatHeightValue = value;
        request.mesh = mesh;
        return queueBuild(std::move(request));
    }

    bool TerrainComponent::requestGpuNoise(const TerrainNoiseSettings& noise, const TerrainMeshSettings& mesh)
    {
        auto request = makeBuildRequest();
        request.source = TerrainBuildSource::Noise;
        request.noise = noise;
        request.mesh = mesh;
        request.gpuNoise = true;
        return queueBuild(std::move(request));
    }

    bool TerrainComponent::requestHeightmap(const std::filesystem::path& path, const TerrainMeshSettings& mesh)
    {
        auto request = makeBuildRequest();
        request.source = TerrainBuildSource::Image;
        request.image = path;
        request.mesh = mesh;
        return queueBuild(std::move(request));
    }

    bool TerrainComponent::requestRebuild(const TerrainMeshSettings& mesh, const TerrainMaterialSettings& material,
        const TerrainChunkSettings& chunks, const TerrainLodSettings& lod)
    {
        auto request = makeBuildRequest();
        request.heightmap = m_heightmap;
        request.mesh = mesh;
        request.material = material;
        request.chunks = chunks;
        request.lod = lod;
        return queueBuild(std::move(request));
    }

    bool TerrainComponent::queueBuild(TerrainBuildRequest request)
    {
        if (!validateTerrainBuildRequest(request) || !resolveTerrainMaterialTextures(request.material))
            return false;
        const DraftSettings drafts{ m_editSettings, m_editMaterialSettings, m_editChunkSettings, m_editLodSettings,
            m_editNoiseSettings, m_editErosionSettings };
        auto task = TerrainGenerationQueue::instance().submit(std::move(request), [this, drafts](TerrainBuildResult&& result)
        {
            std::string success = "Background terrain applied.";
            std::string failure = "Background apply failed; previous terrain preserved. See log.";
            const bool applied = applyBuild(std::move(result), &drafts);
            m_status = applied ? std::move(success) : std::move(failure);
            return applied;
        });
        if (!task)
            return false;
        cancelGeneration();
        m_generation = std::move(task);
        m_status = "Background terrain queued; previous terrain remains active.";
        return true;
    }

    void TerrainComponent::cancelGeneration() noexcept
    {
        if (m_generation && m_generation->status().pending())
            m_generation->cancel();
    }

    TerrainGenerationStatus TerrainComponent::getGenerationStatus() const noexcept
    {
        return m_generation ? m_generation->status() : TerrainGenerationStatus{};
    }

    bool TerrainComponent::applyErosion(const TerrainErosionSettings& settings)
    {
        auto request = makeBuildRequest();
        request.heightmap = m_heightmap;
        request.applyErosion = true;
        request.erosion = settings;
        if (!resolveTerrainMaterialTextures(request.material))
            return false;
        TerrainBuildResult result;
        if (!buildTerrain(std::move(request), result) || !applyBuild(std::move(result)))
            return false;
        cancelGeneration();
        return true;
    }

    bool TerrainComponent::requestErosion(const TerrainErosionSettings& settings)
    {
        auto request = makeBuildRequest();
        request.heightmap = m_heightmap;
        request.applyErosion = true;
        request.erosion = settings;
        return queueBuild(std::move(request));
    }

    bool TerrainComponent::applyBuild(TerrainBuildResult&& result, const DraftSettings* submittedDrafts)
    {
        auto& request = result.applied;
        auto* collider = getGameObject() ? getGameObject()->getComponent<TerrainColliderComponent>() : nullptr;
        if (request.collision != (collider != nullptr))
        {
            LOG_ERROR("[Terrain] Collider presence changed during generation; request generation again.");
            return false;
        }
        const bool updateMesh = !submittedDrafts || m_editSettings == submittedDrafts->mesh;
        const bool updateMaterial = !submittedDrafts || m_editMaterialSettings == submittedDrafts->material;
        const bool updateChunks = !submittedDrafts || m_editChunkSettings == submittedDrafts->chunks;
        const bool updateLod = !submittedDrafts || m_editLodSettings == submittedDrafts->lod;
        const bool updateNoise = !submittedDrafts || m_editNoiseSettings == submittedDrafts->noise;
        const bool updateErosion = !submittedDrafts || m_editErosionSettings == submittedDrafts->erosion;
        TerrainMaterialSettings editMaterialSettings;
        if (updateMaterial)
            editMaterialSettings = request.material;
        MaterialAsset material;
        if (request.material.enabled)
        {
            material.name = "Terrain Layers";
            material.terrain = request.material;
        }
        PendingTerrainResources pending;
        pending.model = ModelManager::instance().create(std::move(result.model));
        if (!pending.model.isValid())
        {
            LOG_ERROR("[Terrain] Cannot register generated terrain model.");
            return false;
        }
        if (request.material.enabled)
        {
            pending.material = MaterialManager::instance().create(std::move(material));
            if (!pending.material.isValid())
            {
                LOG_ERROR("[Terrain] Cannot register generated terrain material.");
                return false;
            }
        }
        const ModelHandle previous = m_model;
        const MaterialHandle previousMaterial = m_material;
        if (collider && !collider->replaceAssets(std::move(result.collision), pending.model))
            return false;
        m_heightmap = std::move(request.heightmap);
        m_settings = request.mesh;
        m_chunkSettings = request.chunks;
        m_chunks = std::move(result.chunks);
        m_lodSettings = request.lod;
        m_lodSnapshot = std::move(result.lod);
        m_model = std::exchange(pending.model, ModelHandle{});
        m_material = std::exchange(pending.material, MaterialHandle{});
        m_materialSettings = std::move(request.material);
        if (updateMesh)
            m_editSettings = request.mesh;
        if (updateChunks)
            m_editChunkSettings = request.chunks;
        if (updateLod)
            m_editLodSettings = request.lod;
        if (updateMaterial)
            m_editMaterialSettings = std::move(editMaterialSettings);
        if (request.source == TerrainBuildSource::Noise)
        {
            m_noiseSettings = request.noise;
            if (updateNoise)
                m_editNoiseSettings = request.noise;
        }
        if (request.applyErosion)
        {
            m_erosionSettings = request.erosion;
            if (updateErosion)
                m_editErosionSettings = request.erosion;
        }
        m_generationFailed = false;
        if (previous.isValid())
            ModelManager::instance().unload(previous);
        if (previousMaterial.isValid())
            MaterialManager::instance().unload(previousMaterial);
        return true;
    }

    std::optional<TerrainSurface> TerrainComponent::sampleLocalSurface(const float x, const float z) const noexcept
    {
        return sampleTerrainSurface(m_heightmap, m_settings, x, z);
    }

    std::optional<TerrainSurface> TerrainComponent::sampleWorldSurface(const Vector3& position) const noexcept
    {
        return sampleTerrainSurfaceWorld(m_heightmap, m_settings,
            getGameObject() ? getGameObject()->getWorldMatrix() : Matrix::Identity, position);
    }

    void TerrainComponent::onDestroy()
    {
        cancelGeneration();
        if (auto* object = getGameObject())
            if (auto* collider = object->getComponent<TerrainColliderComponent>())
                collider->detachTerrain();
    }

    void TerrainComponent::onDisable()
    {
        cancelGeneration();
    }

    bool TerrainComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto samples = m_heightmap.samples();
        const auto noise = Serialization::CreateTerrainNoiseSettings(builder,
            static_cast<std::uint8_t>(m_noiseSettings.mode), m_noiseSettings.seed,
            m_noiseSettings.width, m_noiseSettings.height, m_noiseSettings.frequency,
            m_noiseSettings.offsetX, m_noiseSettings.offsetZ, m_noiseSettings.octaves,
            m_noiseSettings.persistence, m_noiseSettings.lacunarity);
        flatbuffers::Offset<Serialization::TerrainMaterialData> surface;
        if (!Serialization::writeTerrainMaterial(builder, m_materialSettings, surface))
            return false;
        const auto lod = Serialization::CreateTerrainLodSettings(builder,
            m_lodSettings.enabled, m_lodSettings.maxLevel, m_lodSettings.distance);
        const auto erosion = Serialization::CreateTerrainErosionSettings(builder, m_erosionSettings.seed,
            m_erosionSettings.droplets, m_erosionSettings.maxLifetime, m_erosionSettings.radius,
            m_erosionSettings.inertia, m_erosionSettings.capacity, m_erosionSettings.minSlope,
            m_erosionSettings.depositRate, m_erosionSettings.erosionRate, m_erosionSettings.evaporation,
            m_erosionSettings.gravity, m_erosionSettings.initialWater, m_erosionSettings.initialSpeed);
        const auto root = Serialization::CreateTerrainComponentPayload(builder,
            m_heightmap.width(), m_heightmap.height(), builder.CreateVector(samples.data(), samples.size()),
            m_settings.sizeX, m_settings.sizeZ, m_settings.heightScale, m_settings.heightOffset,
            m_settings.uvRepeatX, m_settings.uvRepeatZ, m_castShadows, noise, surface, m_chunkSettings.cellsPerChunk, lod, erosion);
        Serialization::FinishTerrainComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool TerrainComponent::deserializePayload(const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if ((version < 1 || version > 6) || payload.size() < 8
            || !Serialization::VerifyTerrainComponentPayloadBuffer(verifier))
        {
            LOG_ERROR("[Terrain] Invalid terrain payload or version.");
            return false;
        }
        const auto* source = Serialization::GetTerrainComponentPayload(payload.data());
        const auto* samples = source->samples();
        TerrainNoiseSettings noise;
        if (const auto* saved = source->noise_settings())
        {
            noise = { static_cast<TerrainNoiseMode>(saved->mode()), saved->seed(), saved->width(), saved->height(),
                saved->frequency(), saved->offset_x(), saved->offset_z(), saved->octaves(),
                saved->persistence(), saved->lacunarity() };
        }
        else if (version >= 2)
        {
            LOG_ERROR("[Terrain] Payload is missing noise authoring settings.");
            return false;
        }
        if (!validateTerrainNoiseSettings(noise))
            return false;
        TerrainMaterialSettings surface;
        if (source->material_settings())
        {
            if (!Serialization::readTerrainMaterial(source->material_settings(), surface))
                return false;
        }
        else if (version >= 3)
        {
            LOG_ERROR("[Terrain] Version 3 payload is missing material settings.");
            return false;
        }
        TerrainLodSettings lod;
        if (version >= 5)
        {
            const auto* saved = source->lod_settings();
            if (!saved)
            {
                LOG_ERROR("[Terrain] Version 5 payload is missing LOD settings.");
                return false;
            }
            lod = { saved->enabled(), saved->max_level(), saved->distance() };
        }
        TerrainErosionSettings erosion;
        if (version >= 6)
        {
            const auto* saved = source->erosion_settings();
            if (!saved)
            {
                LOG_ERROR("[Terrain] Version 6 payload is missing erosion settings.");
                return false;
            }
            erosion = { saved->seed(), saved->droplets(), saved->max_lifetime(), saved->radius(),
                saved->inertia(), saved->capacity(), saved->min_slope(), saved->deposit_rate(),
                saved->erosion_rate(), saved->evaporation(), saved->gravity(), saved->initial_water(), saved->initial_speed() };
        }
        if (!validateTerrainErosionSettings(erosion))
            return false;
        Heightmap heightmap;
        if (!heightmap.assign(source->width(), source->height(), { samples->data(), samples->size() })
            || !rebuild(heightmap, { source->size_x(), source->size_z(), source->height_scale(),
                source->height_offset(), source->uv_repeat_x(), source->uv_repeat_z() }, surface,
                { version >= 4 ? source->chunk_cells() : 1024u }, lod))
            return false;
        m_castShadows = source->cast_shadows();
        m_noiseSettings = noise;
        m_editNoiseSettings = noise;
        m_erosionSettings = erosion;
        m_editErosionSettings = erosion;
        return true;
    }

    bool TerrainComponent::generateNoise(const TerrainNoiseSettings& noise, const TerrainMeshSettings& mesh)
    {
        if (!validateTerrainMeshSettings(mesh))
            return false;
        Heightmap heightmap;
        if (!generateTerrainNoise(noise, heightmap) || !setTerrain(heightmap, mesh))
            return false;
        m_noiseSettings = noise;
        m_editNoiseSettings = noise;
        return true;
    }

    bool TerrainComponent::copyConfigurationTo(Component& destination) const
    {
        auto* target = dynamic_cast<TerrainComponent*>(&destination);
        if (target == nullptr)
        {
            LOG_ERROR("[Terrain] Clone type mismatch.");
            return false;
        }
        if (!target->rebuild(m_heightmap, m_settings, m_materialSettings, m_chunkSettings, m_lodSettings))
            return false;
        target->m_castShadows = m_castShadows;
        target->m_noiseSettings = m_noiseSettings;
        target->m_editNoiseSettings = m_noiseSettings;
        target->m_erosionSettings = m_erosionSettings;
        target->m_editErosionSettings = m_erosionSettings;
        return true;
    }

    void TerrainComponent::onLateUpdate([[maybe_unused]] const float deltaTime)
    {
        submitRenderData();
    }

    void TerrainComponent::submitRenderData()
    {
        if (getGameObject() == nullptr)
            return;
        if (!m_model.isValid())
        {
            if (getGenerationStatus().pending())
                return;
            if (m_generationFailed)
                return;
            if (!setTerrain(m_heightmap, m_settings))
            {
                m_generationFailed = true;
                m_status = "Terrain generation failed. Apply settings to retry.";
                return;
            }
        }
        auto source = ModelManager::instance().get(m_model);
        if (source == nullptr)
        {
            LOG_ERROR("[Terrain] Generated model was unloaded externally.");
            return;
        }
        ModelRenderSubmission submission;
        submission.model = m_model;
        submission.modelSnapshot = std::move(source);
        submission.terrainLod = m_lodSnapshot;
        submission.materials.push_back(m_materialSettings.enabled
            ? m_material : MaterialManager::instance().getDefaultMaterial());
        if (m_materialSettings.enabled)
        {
            submission.materialSnapshot = MaterialManager::instance().get(m_material);
            if (submission.materialSnapshot == nullptr)
            {
                LOG_ERROR("[Terrain] Generated material was unloaded externally.");
                return;
            }
        }
        submission.worldMatrix = getGameObject()->getWorldMatrix();
        submission.modelSnapshot->boundingBox.Transform(submission.worldBounds, submission.worldMatrix);
        submission.objectID = m_objectID;
        submission.layer = getGameObject()->getLayer();
        submission.castShadows = m_castShadows;
        ModelRenderSubmissionQueue::instance().submit(std::move(submission));
    }

    void TerrainComponent::requestHeightmapImport()
    {
        const GameObject* object = getGameObject();
        if (object == nullptr)
            return;
        const ObjectGUID objectGuid = object->getGUID();
        const auto componentID = m_objectID;
        const HWND ownerWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
        MainThreadDispatcher::instance().post([objectGuid, componentID, ownerWindow]
        {
            static constexpr std::array filters = {
                FileDialogFilter{ L"Heightmap Images", L"*.png;*.tif;*.tiff;*.bmp" },
            };
            std::vector<std::filesystem::path> paths;
            const DialogResult result = Dialog::openFile(paths, L"Import Heightmap",
                std::filesystem::path(L"Assets"), filters, false, ownerWindow);

            SceneManager& manager = SceneManager::instance();
            GameObject* target = manager.getActiveScene() != nullptr
                ? manager.getActiveScene()->find(objectGuid) : nullptr;
            if (target == nullptr)
                target = manager.getPersistentScene()->find(objectGuid);
            auto* terrain = target != nullptr ? target->getComponent<TerrainComponent>() : nullptr;
            if (terrain == nullptr || terrain->m_objectID != componentID || result == DialogResult::Cancel)
                return;
            if (result != DialogResult::Ok || paths.empty())
            {
                terrain->m_status = "Heightmap dialog failed.";
                LOG_ERROR("[Terrain] Heightmap file dialog failed.");
                return;
            }
            if (!terrain->requestHeightmap(paths.front(), terrain->m_editSettings))
            {
                terrain->m_status = "Heightmap import failed; previous terrain preserved. See log.";
                return;
            }
        });
    }

    void TerrainComponent::requestLayerTexture(const std::size_t layer, const std::size_t texture)
    {
        const GameObject* object = getGameObject();
        if (object == nullptr)
            return;
        const ObjectGUID objectGuid = object->getGUID();
        const auto componentID = m_objectID;
        const HWND ownerWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
        MainThreadDispatcher::instance().post([objectGuid, componentID, ownerWindow, layer, texture]
        {
            static constexpr std::array filters = {
                FileDialogFilter{ L"Terrain Textures", L"*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.tif;*.tiff;*.dds" },
            };
            std::vector<std::filesystem::path> paths;
            const DialogResult result = Dialog::openFile(paths, L"Select Terrain Layer Texture",
                std::filesystem::path(L"Assets"), filters, false, ownerWindow);
            SceneManager& manager = SceneManager::instance();
            GameObject* target = manager.getActiveScene() != nullptr
                ? manager.getActiveScene()->find(objectGuid) : nullptr;
            if (target == nullptr)
                target = manager.getPersistentScene()->find(objectGuid);
            auto* terrain = target != nullptr ? target->getComponent<TerrainComponent>() : nullptr;
            if (terrain == nullptr || terrain->m_objectID != componentID || result == DialogResult::Cancel)
                return;
            if (result != DialogResult::Ok || paths.empty())
            {
                LOG_ERROR("[Terrain] Layer texture file dialog failed.");
                terrain->m_status = "Texture dialog failed. See log.";
                return;
            }
            const AssetGUID guid = TextureManager::instance().registerAssetPath(paths.front());
            if (!guid.isValid())
            {
                LOG_ERROR("[Terrain] Cannot register selected layer texture.");
                terrain->m_status = "Texture registration failed. See log.";
                return;
            }
            auto& settings = terrain->m_editMaterialSettings.layers[layer];
            const std::array references = {
                std::pair{ &settings.baseColorTexture, &settings.baseColorPath },
                std::pair{ &settings.normalTexture, &settings.normalPath },
                std::pair{ &settings.metallicRoughnessTexture, &settings.metallicRoughnessPath },
            };
            *references[texture].first = guid;
            *references[texture].second = TextureManager::instance().getAssetPath(guid);
            terrain->m_status = "Texture selected. Apply Terrain Material to update the surface.";
        });
    }

    void TerrainComponent::drawMaterialInspector()
    {
        if (!ImGui::CollapsingHeader("Terrain Material Layers"))
            return;
        ImGui::Checkbox("Enable Layered Material", &m_editMaterialSettings.enabled);
        int count = static_cast<int>(m_editMaterialSettings.layerCount);
        if (ImGui::SliderInt("Layer Count", &count, 1, static_cast<int>(TERRAIN_LAYER_COUNT)))
            m_editMaterialSettings.layerCount = static_cast<std::uint32_t>(count);
        ImGui::TextWrapped("Height = normalized heightmap sample. Slope = local-space degrees. "
            "Higher layers overlay lower ones. Layer 0 fills gaps. Blend weights follow mesh resolution.");
        static constexpr std::array names = { "Soil / Base", "Grass", "Rock", "Snow" };
        static constexpr std::array textureNames = { "BaseColor...", "Normal...", "Metallic-Roughness..." };
        for (std::size_t index = 0; index < m_editMaterialSettings.layerCount; ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::TreeNode(names[index]))
            {
                auto& layer = m_editMaterialSettings.layers[index];
                ImGui::ColorEdit3("Color (linear)", &layer.color.x, ImGuiColorEditFlags_Float);
                ImGui::SliderFloat("Metallic", &layer.metallic, 0, 1);
                ImGui::SliderFloat("Roughness", &layer.roughness, 0, 1);
                ImGui::SliderFloat("Normal Scale", &layer.normalScale, 0, 2);
                ImGui::DragFloat("Layer UV Repeat", &layer.uvRepeat, 0.05f, 0.001f, 10000);
                if (index != 0)
                {
                    ImGui::SliderFloat("Minimum Height", &layer.minimumHeight, 0, 1);
                    ImGui::SliderFloat("Maximum Height", &layer.maximumHeight, 0, 1);
                    ImGui::SliderFloat("Height Blend Width", &layer.heightBlend, 0, 1);
                    ImGui::SliderFloat("Minimum Slope", &layer.minimumSlope, 0, 90);
                    ImGui::SliderFloat("Maximum Slope", &layer.maximumSlope, 0, 90);
                    ImGui::SliderFloat("Slope Blend Width", &layer.slopeBlend, 0, 90);
                    ImGui::SliderFloat("Coverage Strength", &layer.strength, 0, 1);
                }
                const std::array references = {
                    std::pair{ &layer.baseColorTexture, &layer.baseColorPath },
                    std::pair{ &layer.normalTexture, &layer.normalPath },
                    std::pair{ &layer.metallicRoughnessTexture, &layer.metallicRoughnessPath },
                };
                for (std::size_t texture = 0; texture < references.size(); ++texture)
                {
                    ImGui::PushID(static_cast<int>(texture));
                    if (ImGui::Button(textureNames[texture]))
                        requestLayerTexture(index, texture);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear"))
                    {
                        *references[texture].first = {};
                        references[texture].second->clear();
                    }
                    ImGui::TextWrapped("%s", references[texture].second->empty()
                        ? (references[texture].first->isValid() ? "GUID reference" : "Built-in default")
                        : references[texture].second->string().c_str());
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Reset Layer Draft"))
            m_editMaterialSettings = TerrainMaterialSettings{};
        ImGui::SameLine();
        if (ImGui::Button("Apply Terrain Material"))
            m_status = requestRebuild(m_settings, m_editMaterialSettings, m_chunkSettings, m_lodSettings)
                ? "Terrain material queued." : "Material request failed; previous terrain preserved. See log.";
    }

    void TerrainComponent::onImGui()
    {
        ImGui::Text("Heightmap: %u x %u", m_heightmap.width(), m_heightmap.height());
        ImGui::Text("Vertices: %zu  Triangles: %zu", m_heightmap.samples().size(),
            static_cast<std::size_t>(m_heightmap.width() - 1) * (m_heightmap.height() - 1) * 2);
        ImGui::TextWrapped("Local XZ grid centered at the origin. +Y is up. Add TerrainCollider for PhysX collision.");
        ImGui::DragFloat("Size X", &m_editSettings.sizeX, 0.1f, 0.01f, 100000.0f);
        ImGui::DragFloat("Size Z", &m_editSettings.sizeZ, 0.1f, 0.01f, 100000.0f);
        ImGui::DragFloat("Height Scale", &m_editSettings.heightScale, 0.1f, 0.0f, 100000.0f);
        ImGui::DragFloat("Height Offset", &m_editSettings.heightOffset, 0.1f, -100000.0f, 100000.0f);
        ImGui::DragFloat("UV Repeat X", &m_editSettings.uvRepeatX, 0.1f, 0.001f, 10000.0f);
        ImGui::DragFloat("UV Repeat Z", &m_editSettings.uvRepeatZ, 0.1f, 0.001f, 10000.0f);
        ImGui::Checkbox("Cast Shadows", &m_castShadows);
        ImGui::InputScalar("Chunk Cells (16..1024)", ImGuiDataType_U32, &m_editChunkSettings.cellsPerChunk);
        ImGui::Text("Applied Chunks: %zu", m_chunks.size());
        ImGui::TextWrapped("Chunk edges share height, normal, UV and layer data. All chunks remain resident.");
        if (ImGui::Button("Apply Chunk Settings"))
            m_status = requestRebuild(m_settings, m_materialSettings, m_editChunkSettings, m_lodSettings)
                ? "Terrain chunks queued." : "Chunk request failed; previous terrain preserved. See log.";
        ImGui::Checkbox("Enable Terrain LOD", &m_editLodSettings.enabled);
        ImGui::InputScalar("Maximum LOD (1..4)", ImGuiDataType_U32, &m_editLodSettings.maxLevel);
        ImGui::DragFloat("LOD Distance", &m_editLodSettings.distance, 1, 0.1f, 1000000);
        ImGui::TextWrapped("Distance to chunk bounds; thresholds double each level. "
            "Borders stay full resolution. Physics is unchanged. Chunk frustum culling is per view.");
        if (ImGui::Button("Apply LOD Settings"))
            m_status = requestRebuild(m_settings, m_materialSettings, m_chunkSettings, m_editLodSettings)
                ? "Terrain LOD queued." : "LOD request failed; previous terrain preserved. See log.";
        if (ImGui::Button("Apply / Rebuild"))
            m_status = requestTerrain(m_heightmap, m_editSettings)
                ? "Terrain rebuild queued." : "Rebuild request failed; previous terrain preserved. See log.";
        ImGui::SameLine();
        if (ImGui::Button("Import Heightmap..."))
            requestHeightmapImport();
        ImGui::Separator();
        ImGui::InputInt("Flat Samples X", &m_flatWidth);
        ImGui::InputInt("Flat Samples Z", &m_flatHeight);
        ImGui::SliderFloat("Flat Height", &m_flatValue, 0.0f, 1.0f);
        ImGui::TextDisabled("2..%u samples per axis. Replaces current heights.", Heightmap::MAX_DIMENSION);
        if (ImGui::Button("Create Flat"))
        {
            const bool valid = m_flatWidth >= 2 && m_flatHeight >= 2;
            if (!valid)
                LOG_ERROR("[Terrain] Flat grid dimensions must be at least 2.");
            m_status = valid && requestFlat(static_cast<std::uint32_t>(m_flatWidth),
                static_cast<std::uint32_t>(m_flatHeight), m_flatValue, m_editSettings)
                ? "Flat terrain queued." : "Flat request failed; previous terrain preserved. See log.";
        }
        ImGui::Separator();
        drawMaterialInspector();
        ImGui::Separator();
        ImGui::TextUnformatted("Procedural Heightmap");
        int mode = static_cast<int>(m_editNoiseSettings.mode);
        if (ImGui::Combo("Noise Mode", &mode, "Perlin\0FBM\0"))
            m_editNoiseSettings.mode = static_cast<TerrainNoiseMode>(mode);
        ImGui::InputScalar("Seed", ImGuiDataType_U32, &m_editNoiseSettings.seed);
        ImGui::InputScalar("Noise Samples X", ImGuiDataType_U32, &m_editNoiseSettings.width);
        ImGui::InputScalar("Noise Samples Z", ImGuiDataType_U32, &m_editNoiseSettings.height);
        ImGui::DragFloat("Frequency", &m_editNoiseSettings.frequency, 0.05f, 0.001f, 1024.0f);
        ImGui::DragFloat("Noise Offset X", &m_editNoiseSettings.offsetX, 0.05f, -100000.0f, 100000.0f);
        ImGui::DragFloat("Noise Offset Z", &m_editNoiseSettings.offsetZ, 0.05f, -100000.0f, 100000.0f);
        ImGui::BeginDisabled(m_editNoiseSettings.mode == TerrainNoiseMode::Perlin);
        ImGui::InputScalar("Octaves (1..12)", ImGuiDataType_U32, &m_editNoiseSettings.octaves);
        ImGui::SliderFloat("Persistence", &m_editNoiseSettings.persistence, 0.0f, 1.0f);
        ImGui::SliderFloat("Lacunarity", &m_editNoiseSettings.lacunarity, 1.0f, 4.0f);
        ImGui::EndDisabled();
        ImGui::TextWrapped("Generate replaces heights and applies mesh settings above. "
            "Frequency spans each grid axis; offsets are noise-space coordinates.");
        if (ImGui::Button("Generate Noise"))
            m_status = requestNoise(m_editNoiseSettings, m_editSettings)
                ? "Noise generation queued." : "Noise request failed; previous terrain preserved. See log.";
        ImGui::SameLine();
        if (ImGui::Button("Generate Noise (GPU)"))
            m_status = requestGpuNoise(m_editNoiseSettings, m_editSettings)
                ? "GPU noise generation queued." : "GPU request failed; previous terrain preserved. See log.";
        ImGui::TextWrapped("GPU uses an independent Compute Queue and readback on the terrain worker. "
            "Float interpolation can differ slightly from CPU noise. Saved samples and PhysX use the same GPU result.");
        ImGui::Separator();
        if (ImGui::TreeNode("Hydraulic Erosion"))
        {
            ImGui::InputScalar("Erosion Seed", ImGuiDataType_U32, &m_editErosionSettings.seed);
            ImGui::InputScalar("Droplets (1..1000000)", ImGuiDataType_U32, &m_editErosionSettings.droplets);
            ImGui::InputScalar("Droplet Lifetime (1..256)", ImGuiDataType_U32, &m_editErosionSettings.maxLifetime);
            ImGui::InputScalar("Erosion Radius (1..8)", ImGuiDataType_U32, &m_editErosionSettings.radius);
            ImGui::SliderFloat("Inertia", &m_editErosionSettings.inertia, 0, 1);
            ImGui::DragFloat("Sediment Capacity", &m_editErosionSettings.capacity, 0.1f, 0.01f, 100);
            ImGui::DragFloat("Minimum Slope", &m_editErosionSettings.minSlope, 0.0001f, 0.00001f, 1, "%.5f");
            ImGui::SliderFloat("Deposition Rate", &m_editErosionSettings.depositRate, 0, 1);
            ImGui::SliderFloat("Erosion Rate", &m_editErosionSettings.erosionRate, 0, 1);
            ImGui::SliderFloat("Evaporation", &m_editErosionSettings.evaporation, 0, 1);
            ImGui::DragFloat("Erosion Gravity", &m_editErosionSettings.gravity, 0.1f, 0, 100);
            ImGui::DragFloat("Initial Water", &m_editErosionSettings.initialWater, 0.1f, 0.01f, 10);
            ImGui::DragFloat("Initial Speed", &m_editErosionSettings.initialSpeed, 0.1f, 0, 10);
            ImGui::TextWrapped("Erodes applied heights using applied mesh settings. Grid-cell/normalized-height units; "
                "mesh scaling does not change erosion. Open edges export sediment. Repeated applications are cumulative; "
                "Rebuild does not repeat erosion. Pending requests replace, not combine with, earlier requests.");
            if (ImGui::Button("Apply Hydraulic Erosion"))
                m_status = requestErosion(m_editErosionSettings)
                    ? "Hydraulic erosion queued." : "Erosion request failed; previous terrain preserved. See log.";
            ImGui::SameLine();
            if (ImGui::Button("Reset Erosion Draft"))
                m_editErosionSettings = TerrainErosionSettings{};
            ImGui::TreePop();
        }
        const auto generation = getGenerationStatus();
        ImGui::Text("Background: %s", terrainGenerationStageName(generation.stage));
        if (generation.pending())
        {
            ImGui::TextWrapped("Current terrain stays active. New requests replace older work; unapplied edits are not saved.");
            if (ImGui::Button("Cancel Terrain Generation"))
            {
                cancelGeneration();
                m_status = "Background generation cancelled; previous terrain preserved.";
            }
        }
        if (generation.buildMilliseconds > 0)
            ImGui::Text("Build: %.2f ms  Apply: %.2f ms", generation.buildMilliseconds, generation.applyMilliseconds);
        if (generation.stage == TerrainGenerationStage::Failed)
            ImGui::TextWrapped("Background generation/apply failed. Previous terrain preserved; see log and retry.");
        else if (!m_status.empty())
            ImGui::TextWrapped("%s", m_status.c_str());
    }
}
