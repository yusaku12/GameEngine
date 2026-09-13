#include "Pch.h"
#include "Core\GameObject\Component\ModelRendererComponent.h"
#include "Assets\Material\MaterialManager.h"
#include "Assets\Model\ModelManager.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\Scene\SceneManager.h"
#include "Core\System\Dialog.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Graphics\Renderer\ModelRenderSubmission.h"
#include <imgui.h>

namespace Engine
{
    namespace
    {
        std::atomic_uint32_t nextModelRendererObjectID = 1;
        const ComponentTypeID modelRendererType = ComponentRegistry::instance().registerType<ModelRendererComponent>(
            "Model Renderer", false, false, true);

        MaterialParameterValues parameterValues(const MaterialAsset& material) noexcept
        {
            return {
                .baseColor = material.baseColor,
                .metallic = material.metallic,
                .roughness = material.roughness,
                .emissiveIntensity = material.emissiveIntensity,
                .normalScale = material.normalScale,
                .emissiveColor = material.emissiveColor,
                .occlusionStrength = material.occlusionStrength,
                .alphaCutoff = material.alphaCutoff,
            };
        }

        void setPropertyValue(MaterialPropertyBlock& block, const MaterialParameterDescriptor& descriptor,
            const void* value) noexcept
        {
            if (descriptor.type == MaterialParameterType::Float)
                block.setFloat(descriptor.id, *static_cast<const float*>(value));
            else if (descriptor.type == MaterialParameterType::Vector3)
                block.setVector3(descriptor.id, *static_cast<const Vector3*>(value));
            else
                block.setVector4(descriptor.id, *static_cast<const Vector4*>(value));
        }

        void drawParameterEditor(MaterialPropertyBlock& block)
        {
            MaterialParameterValues values = block.getValues();
            const std::span<const MaterialParameterDescriptor> parameters = MaterialParameterLayout::standard();
            for (std::size_t index = 0; index < parameters.size(); ++index)
            {
                const MaterialParameterDescriptor& parameter = parameters[index];
                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(index);
                bool overridden = (block.getOverrideMask() & bit) != 0;
                ImGui::PushID(static_cast<int>(parameter.id));
                if (ImGui::Checkbox("##Override", &overridden))
                {
                    if (overridden)
                    {
                        const auto* value = reinterpret_cast<const std::byte*>(&values) + parameter.offset;
                        setPropertyValue(block, parameter, value);
                    }
                    else
                    {
                        block.clear(parameter.id);
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!overridden);
                auto* value = reinterpret_cast<std::byte*>(&values) + parameter.offset;
                bool changed = false;
                if (parameter.type == MaterialParameterType::Float)
                    changed = ImGui::SliderFloat(parameter.name.data(), reinterpret_cast<float*>(value), parameter.minimum, parameter.maximum);
                else if (parameter.type == MaterialParameterType::Vector3)
                    changed = ImGui::ColorEdit3(parameter.name.data(), reinterpret_cast<float*>(value));
                else
                    changed = ImGui::ColorEdit4(parameter.name.data(), reinterpret_cast<float*>(value));
                if (changed)
                    setPropertyValue(block, parameter, value);
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }

        void drawSharedMaterial(const MaterialAsset& material)
        {
            ImGui::Text("Shared Material (Read Only)");
            ImGui::TextWrapped("Name: %s", material.name.c_str());
            ImGui::Text("Keyword Mask: 0x%08X", material.shaderKeywords);
            MaterialParameterValues values = parameterValues(material);
            ImGui::BeginDisabled();
            for (const MaterialParameterDescriptor& parameter : MaterialParameterLayout::standard())
            {
                auto* value = reinterpret_cast<std::byte*>(&values) + parameter.offset;
                if (parameter.type == MaterialParameterType::Float)
                    ImGui::SliderFloat(parameter.name.data(), reinterpret_cast<float*>(value), parameter.minimum, parameter.maximum);
                else if (parameter.type == MaterialParameterType::Vector3)
                    ImGui::ColorEdit3(parameter.name.data(), reinterpret_cast<float*>(value));
                else
                    ImGui::ColorEdit4(parameter.name.data(), reinterpret_cast<float*>(value));
            }
            ImGui::EndDisabled();
        }
    }

    ModelRendererComponent::ModelRendererComponent() noexcept
        : m_objectID(nextModelRendererObjectID.fetch_add(1, std::memory_order_relaxed))
    {
        GE_UNUSED(modelRendererType);
    }

    void ModelRendererComponent::setModel(const ModelHandle model) noexcept
    {
        const std::shared_ptr<const ModelResource> previousModel = ModelManager::instance().get(m_model);
        const std::shared_ptr<const ModelResource> nextModel = ModelManager::instance().get(model);
        std::vector<MaterialHandle> nextOverrides(nextModel ? nextModel->materialSlots.size() : 0);

        if (previousModel != nullptr && nextModel != nullptr)
        {
            for (std::size_t nextIndex = 0; nextIndex < nextModel->materialSlots.size(); ++nextIndex)
            {
                const std::string& slotName = nextModel->materialSlots[nextIndex].name;
                const auto previousSlot = std::find_if(previousModel->materialSlots.begin(), previousModel->materialSlots.end(),
                    [&slotName](const ModelMaterialSlot& slot) { return slot.name == slotName; });
                if (previousSlot == previousModel->materialSlots.end())
                    continue;
                const std::size_t previousIndex = static_cast<std::size_t>(previousSlot - previousModel->materialSlots.begin());
                if (previousIndex < m_materialOverrides.size())
                    nextOverrides[nextIndex] = m_materialOverrides[previousIndex];
            }
        }

        m_model = model;
        m_materialOverrides = std::move(nextOverrides);
    }

    bool ModelRendererComponent::setMaterialOverride(const std::size_t slotIndex, const MaterialHandle material) noexcept
    {
        const std::shared_ptr<const ModelResource> model = ModelManager::instance().get(m_model);
        if (model == nullptr || slotIndex >= model->materialSlots.size())
            return false;
        if (material.isValid() && MaterialManager::instance().get(material) == nullptr)
            return false;
        if (m_materialOverrides.size() != model->materialSlots.size())
            m_materialOverrides.resize(model->materialSlots.size());
        m_materialOverrides[slotIndex] = material;
        return true;
    }

    void ModelRendererComponent::clearMaterialOverride(const std::size_t slotIndex) noexcept
    {
        if (slotIndex < m_materialOverrides.size())
            m_materialOverrides[slotIndex] = MaterialHandle::Invalid();
    }

    void ModelRendererComponent::onLateUpdate([[maybe_unused]] const float deltaTime)
    {
        if (!m_model.isValid() || getGameObject() == nullptr)
            return;

        const std::shared_ptr<const ModelResource> model = ModelManager::instance().get(m_model);
        if (model == nullptr)
            return;

        const Matrix worldMatrix = getGameObject()->getWorldMatrix();
        AABB worldBounds;
        model->boundingBox.Transform(worldBounds, worldMatrix);
        MaterialManager& materialManager = MaterialManager::instance();
        std::vector<MaterialHandle> resolvedMaterials(model->materialSlots.size());
        for (std::size_t slotIndex = 0; slotIndex < model->materialSlots.size(); ++slotIndex)
        {
            if (slotIndex < m_materialOverrides.size()
                && materialManager.get(m_materialOverrides[slotIndex]) != nullptr)
            {
                resolvedMaterials[slotIndex] = m_materialOverrides[slotIndex];
            }
            else
            {
                resolvedMaterials[slotIndex] = materialManager.findByGuid(
                    model->materialSlots[slotIndex].defaultMaterialGuid);
            }
            if (materialManager.get(resolvedMaterials[slotIndex]) == nullptr)
                resolvedMaterials[slotIndex] = materialManager.getDefaultMaterial();
        }
        ModelRenderSubmissionQueue::instance().submit(ModelRenderSubmission{
            .model = m_model,
            .materials = std::move(resolvedMaterials),
            .materialProperties = m_propertyBlock,
            .worldMatrix = worldMatrix,
            .worldBounds = worldBounds,
            .objectID = m_objectID,
            .layer = getGameObject()->getLayer(),
            .castShadows = m_castShadows,
            });
    }

    void ModelRendererComponent::onImGui()
    {
        const std::shared_ptr<const ModelResource> model = ModelManager::instance().get(m_model);
        if (model != nullptr && !model->sourcePath.empty())
            ImGui::TextWrapped("Model: %s", model->sourcePath.string().c_str());
        else
            ImGui::TextDisabled("Model: None");

        if (ImGui::Button("Open Model..."))
        {
            const GameObject* const gameObject = getGameObject();
            if (gameObject != nullptr)
            {
                const ObjectGUID objectGuid = gameObject->getGUID();
                const HWND ownerWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
                MainThreadDispatcher::instance().post([objectGuid, ownerWindow]
                    {
                        SceneManager& sceneManager = SceneManager::instance();
                        GameObject* object = nullptr;
                        if (Scene* scene = sceneManager.getActiveScene())
                            object = scene->find(objectGuid);
                        if (object == nullptr)
                            object = sceneManager.getPersistentScene()->find(objectGuid);
                        ModelRendererComponent* component = object != nullptr
                            ? object->getComponent<ModelRendererComponent>() : nullptr;
                        if (component == nullptr)
                            return;

                        static constexpr std::array filters = {
                            FileDialogFilter{ L"Model Files", L"*.model;*.mdl;*.fbx;*.obj;*.gltf;*.glb;*.dae;*.3ds;*.ply;*.stl;*.pmx" },
                            FileDialogFilter{ L"GameEngine Model", L"*.model;*.mdl" },
                            FileDialogFilter{ L"FBX", L"*.fbx" },
                            FileDialogFilter{ L"glTF", L"*.gltf;*.glb" },
                            FileDialogFilter{ L"All Files", L"*.*" },
                        };
                        std::filesystem::path initialPath = "Assets/Models";
                        if (const std::shared_ptr<const ModelResource> current = ModelManager::instance().get(component->getModel());
                            current != nullptr && !current->sourcePath.empty())
                        {
                            initialPath = current->sourcePath;
                        }

                        std::vector<std::filesystem::path> paths;
                        const DialogResult result = Dialog::openFile(
                            paths, L"モデルを開く", initialPath, filters, false, ownerWindow);
                        if (result == DialogResult::Cancel)
                            return;
                        if (result != DialogResult::Ok || paths.empty())
                        {
                            component->m_loadStatus = "Model dialog failed.";
                            return;
                        }

                        const ModelHandle handle = ModelManager::instance().load(paths.front());
                        if (!handle.isValid())
                        {
                            component->m_loadStatus = "Model load failed: " + paths.front().string();
                            return;
                        }
                        component->setModel(handle);
                        component->m_loadStatus = "Loaded: " + paths.front().string();
                    });
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            setModel(ModelHandle::Invalid());
            m_loadStatus = "Model cleared.";
        }
        ImGui::Checkbox("Cast Shadows", &m_castShadows);

        if (model != nullptr && ImGui::CollapsingHeader("Material Slots", ImGuiTreeNodeFlags_DefaultOpen))
        {
            MaterialManager& materials = MaterialManager::instance();
            const std::vector<MaterialHandle> availableMaterials = materials.getAllHandles();
            for (std::size_t slotIndex = 0; slotIndex < model->materialSlots.size(); ++slotIndex)
            {
                const ModelMaterialSlot& slot = model->materialSlots[slotIndex];
                const MaterialHandle overrideMaterial = slotIndex < m_materialOverrides.size()
                    ? m_materialOverrides[slotIndex] : MaterialHandle::Invalid();
                MaterialHandle resolvedMaterial = materials.get(overrideMaterial) != nullptr
                    ? overrideMaterial : materials.findByGuid(slot.defaultMaterialGuid);
                if (materials.get(resolvedMaterial) == nullptr)
                    resolvedMaterial = materials.getDefaultMaterial();
                const std::shared_ptr<const MaterialAsset> resolved = materials.get(resolvedMaterial);
                const std::shared_ptr<const MaterialAsset> overridden = materials.get(overrideMaterial);

                ImGui::PushID(static_cast<int>(slotIndex));
                ImGui::TextUnformatted(slot.name.c_str());
                const std::string preview = overridden != nullptr
                    ? overridden->name : std::string("Model Default: ") + (resolved != nullptr ? resolved->name : "None");
                if (ImGui::BeginCombo("Material", preview.c_str()))
                {
                    if (ImGui::Selectable("Use Model Default", overridden == nullptr))
                        clearMaterialOverride(slotIndex);
                    for (const MaterialHandle handle : availableMaterials)
                    {
                        const std::shared_ptr<const MaterialAsset> material = materials.get(handle);
                        if (material != nullptr && ImGui::Selectable(material->name.c_str(), handle == overrideMaterial))
                            setMaterialOverride(slotIndex, handle);
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Open Material"))
                    m_inspectedMaterial = resolvedMaterial;
                ImGui::Separator();
                ImGui::PopID();
            }
        }

        if (const std::shared_ptr<const MaterialAsset> inspected = MaterialManager::instance().get(m_inspectedMaterial);
            inspected != nullptr && ImGui::CollapsingHeader("Material Inspector", ImGuiTreeNodeFlags_DefaultOpen))
        {
            drawSharedMaterial(*inspected);
        }

        if (ImGui::CollapsingHeader("Material Property Block", ImGuiTreeNodeFlags_DefaultOpen))
        {
            drawParameterEditor(m_propertyBlock);
            if (!m_propertyBlock.empty() && ImGui::Button("Clear Overrides"))
                m_propertyBlock.clear();
        }
        if (!m_loadStatus.empty())
            ImGui::TextWrapped("%s", m_loadStatus.c_str());
    }
} // namespace Engine