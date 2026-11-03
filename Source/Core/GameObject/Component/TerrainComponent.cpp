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
#include <imgui.h>

namespace Engine
{
    namespace
    {
        const auto terrainType = ComponentRegistry::instance().registerType<TerrainComponent>(
            "Terrain", false, false, true);
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
    }

    bool TerrainComponent::setTerrain(const Heightmap& heightmap, const TerrainMeshSettings& settings)
    {
        Heightmap replacement = heightmap;
        ModelResource model;
        if (!buildTerrainModel(replacement, settings, model))
            return false;
        const ModelHandle handle = ModelManager::instance().create(std::move(model));
        if (!handle.isValid())
        {
            LOG_ERROR("[Terrain] Cannot register generated terrain model.");
            return false;
        }
        const ModelHandle previous = m_model;
        m_heightmap = std::move(replacement);
        m_settings = settings;
        m_editSettings = settings;
        m_model = handle;
        m_generationFailed = false;
        if (previous.isValid())
            ModelManager::instance().unload(previous);
        return true;
    }

    bool TerrainComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto samples = m_heightmap.samples();
        const auto root = Serialization::CreateTerrainComponentPayload(builder,
            m_heightmap.width(), m_heightmap.height(), builder.CreateVector(samples.data(), samples.size()),
            m_settings.sizeX, m_settings.sizeZ, m_settings.heightScale, m_settings.heightOffset,
            m_settings.uvRepeatX, m_settings.uvRepeatZ, m_castShadows);
        Serialization::FinishTerrainComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }

    bool TerrainComponent::deserializePayload(const std::uint32_t version,
        const std::span<const std::uint8_t> payload)
    {
        flatbuffers::Verifier verifier(payload.data(), payload.size());
        if (version != 1 || payload.size() < 8 || !Serialization::VerifyTerrainComponentPayloadBuffer(verifier))
        {
            LOG_ERROR("[Terrain] Invalid terrain payload or version.");
            return false;
        }
        const auto* source = Serialization::GetTerrainComponentPayload(payload.data());
        const auto* samples = source->samples();
        Heightmap heightmap;
        if (!heightmap.assign(source->width(), source->height(), { samples->data(), samples->size() })
            || !setTerrain(heightmap, { source->size_x(), source->size_z(), source->height_scale(),
                source->height_offset(), source->uv_repeat_x(), source->uv_repeat_z() }))
            return false;
        m_castShadows = source->cast_shadows();
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
        if (!target->setTerrain(m_heightmap, m_settings))
            return false;
        target->m_castShadows = m_castShadows;
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
        submission.materials.push_back(MaterialManager::instance().getDefaultMaterial());
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

    void TerrainComponent::onImGui()
    {
        ImGui::Text("Heightmap: %u x %u", m_heightmap.width(), m_heightmap.height());
        ImGui::Text("Vertices: %zu  Triangles: %zu", m_heightmap.samples().size(),
            static_cast<std::size_t>(m_heightmap.width() - 1) * (m_heightmap.height() - 1) * 2);
        ImGui::TextWrapped("Local XZ grid centered at the origin. +Y is up. No collider.");
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
        if (!m_status.empty())
            ImGui::TextWrapped("%s", m_status.c_str());
    }
}
