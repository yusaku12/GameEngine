#include "Pch.h"
#include "Core\GameObject\Component\ModelRendererComponent.h"
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
    }

    ModelRendererComponent::ModelRendererComponent() noexcept
        : m_objectID(nextModelRendererObjectID.fetch_add(1, std::memory_order_relaxed))
    {
        GE_UNUSED(modelRendererType);
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
        ModelRenderSubmissionQueue::instance().submit(ModelRenderSubmission{
            .model = m_model,
            .worldMatrix = worldMatrix,
            .worldBounds = worldBounds,
            .objectID = m_objectID,
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
            m_model = ModelHandle::Invalid();
            m_loadStatus = "Model cleared.";
        }
        ImGui::Checkbox("Cast Shadows", &m_castShadows);
        if (!m_loadStatus.empty())
            ImGui::TextWrapped("%s", m_loadStatus.c_str());
    }
} // namespace Engine