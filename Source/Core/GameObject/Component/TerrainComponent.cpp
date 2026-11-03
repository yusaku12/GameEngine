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
        if (m_model.isValid())
            ModelManager::instance().unload(m_model);
        if (m_material.isValid())
            MaterialManager::instance().unload(m_material);
    }

    bool TerrainComponent::setTerrain(const Heightmap& heightmap, const TerrainMeshSettings& settings)
    {
        return rebuild(heightmap, settings, m_materialSettings);
    }

    bool TerrainComponent::setMaterialSettings(const TerrainMaterialSettings& settings)
    {
        return rebuild(m_heightmap, m_settings, settings);
    }

    bool TerrainComponent::rebuild(const Heightmap& heightmap, const TerrainMeshSettings& settings,
        const TerrainMaterialSettings& surface)
    {
        Heightmap replacement = heightmap;
        TerrainMaterialSettings materialSettings = surface;
        if (!resolveTerrainMaterialTextures(materialSettings))
            return false;
        TerrainMaterialSettings editMaterialSettings = materialSettings;
        ModelResource model;
        if (!buildTerrainModel(replacement, settings, model)
            || !applyTerrainLayerWeights(replacement, materialSettings, model))
            return false;
        MaterialAsset material;
        if (materialSettings.enabled)
        {
            material.name = "Terrain Layers";
            material.terrain = materialSettings;
        }
        PendingTerrainResources pending;
        auto* collider = getGameObject() ? getGameObject()->getComponent<TerrainColliderComponent>() : nullptr;
        std::shared_ptr<const PhysicsMeshAsset> collision;
        if (collider)
        {
            collision = PhysicsMeshAsset::cookTriangle(model.meshes[0]);
            if (!collision)
                return false;
        }
        pending.model = ModelManager::instance().create(std::move(model));
        if (!pending.model.isValid())
        {
            LOG_ERROR("[Terrain] Cannot register generated terrain model.");
            return false;
        }
        if (materialSettings.enabled)
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
        if (collider && !collider->replaceAsset(std::move(collision), pending.model))
            return false;
        m_heightmap = std::move(replacement);
        m_settings = settings;
        m_editSettings = settings;
        m_model = std::exchange(pending.model, ModelHandle{});
        m_material = std::exchange(pending.material, MaterialHandle{});
        m_materialSettings = std::move(materialSettings);
        m_editMaterialSettings = std::move(editMaterialSettings);
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
        if (auto* object = getGameObject())
            if (auto* collider = object->getComponent<TerrainColliderComponent>())
                collider->detachTerrain();
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
        const auto root = Serialization::CreateTerrainComponentPayload(builder,
            m_heightmap.width(), m_heightmap.height(), builder.CreateVector(samples.data(), samples.size()),
            m_settings.sizeX, m_settings.sizeZ, m_settings.heightScale, m_settings.heightOffset,
            m_settings.uvRepeatX, m_settings.uvRepeatZ, m_castShadows, noise, surface);
        Serialization::FinishTerrainComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool TerrainComponent::deserializePayload(const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if ((version < 1 || version > 3) || payload.size() < 8
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
        Heightmap heightmap;
        if (!heightmap.assign(source->width(), source->height(), { samples->data(), samples->size() })
            || !rebuild(heightmap, { source->size_x(), source->size_z(), source->height_scale(),
                source->height_offset(), source->uv_repeat_x(), source->uv_repeat_z() }, surface))
            return false;
        m_castShadows = source->cast_shadows();
        m_noiseSettings = noise;
        m_editNoiseSettings = noise;
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
        if (!target->rebuild(m_heightmap, m_settings, m_materialSettings))
            return false;
        target->m_castShadows = m_castShadows;
        target->m_noiseSettings = m_noiseSettings;
        target->m_editNoiseSettings = m_noiseSettings;
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
        const HWND ownerWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
        MainThreadDispatcher::instance().post([objectGuid, ownerWindow]
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
            if (terrain == nullptr || result == DialogResult::Cancel)
                return;
            if (result != DialogResult::Ok || paths.empty())
            {
                terrain->m_status = "Heightmap dialog failed.";
                LOG_ERROR("[Terrain] Heightmap file dialog failed.");
                return;
            }
            Heightmap heightmap;
            if (!heightmap.loadImage(paths.front()) || !terrain->setTerrain(heightmap, terrain->m_editSettings))
            {
                terrain->m_status = "Heightmap import failed; previous terrain preserved. See log.";
                return;
            }
            terrain->m_status = "Heightmap imported and embedded in terrain.";
        });
    }

    void TerrainComponent::requestLayerTexture(const std::size_t layer, const std::size_t texture)
    {
        const GameObject* object = getGameObject();
        if (object == nullptr)
            return;
        const ObjectGUID objectGuid = object->getGUID();
        const HWND ownerWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
        MainThreadDispatcher::instance().post([objectGuid, ownerWindow, layer, texture]
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
            if (terrain == nullptr || result == DialogResult::Cancel)
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
            m_status = setMaterialSettings(m_editMaterialSettings)
                ? "Terrain material applied." : "Material apply failed; previous terrain preserved. See log.";
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
        if (ImGui::Button("Apply / Rebuild"))
            m_status = setTerrain(m_heightmap, m_editSettings)
                ? "Terrain rebuilt." : "Rebuild failed; previous terrain preserved. See log.";
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
            Heightmap heightmap;
            const bool valid = m_flatWidth >= 2 && m_flatHeight >= 2;
            if (!valid)
                LOG_ERROR("[Terrain] Flat grid dimensions must be at least 2.");
            m_status = valid && heightmap.createFlat(static_cast<std::uint32_t>(m_flatWidth),
                static_cast<std::uint32_t>(m_flatHeight), m_flatValue) && setTerrain(heightmap, m_editSettings)
                ? "Flat terrain created." : "Flat terrain creation failed; previous terrain preserved. See log.";
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
            m_status = generateNoise(m_editNoiseSettings, m_editSettings)
                ? "Noise terrain generated." : "Noise generation failed; previous terrain preserved. See log.";
        if (!m_status.empty())
            ImGui::TextWrapped("%s", m_status.c_str());
    }
}
