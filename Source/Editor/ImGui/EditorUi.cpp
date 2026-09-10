#include "Pch.h"
#include "Editor\ImGui\EditorUi.h"
#include "Graphics\Shader\ShaderManager.h"

#include <imgui.h>

namespace Engine
{
    EditorUi::EditorUi()
    {
        Scene* scene = m_sceneManager.createScene("EditorScene");
        if (scene != nullptr)
        {
            GameObject* camera = scene->createGameObject("Main Camera");
            GameObject* light = scene->createGameObject("Directional Light");
            GameObject* triangle = scene->createGameObject("Triangle");
            if (camera != nullptr)
                m_selectedObject = camera;
            if (light != nullptr && triangle != nullptr)
                light->setParent(triangle, false);
        }
    }

    void EditorUi::draw(ShaderManager* shaderManager)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        constexpr ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoNavFocus
            | ImGuiWindowFlags_NoBackground;
        ImGui::Begin("##EditorRoot", nullptr, rootFlags);
        const ImGuiID dockspaceId = ImGui::GetID("GameEngineDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::End();

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("ファイル"))
            {
                ImGui::MenuItem("新規シーン");
                ImGui::MenuItem("シーンを保存");
                ImGui::Separator();
                ImGui::MenuItem("終了");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window"))
            {
                ImGui::MenuItem("Shader Manager", nullptr, &m_showShaderManager);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        drawHierarchy();
        drawInspector();
        drawShaderManager(shaderManager);
    }

    void EditorUi::drawHierarchy()
    {
        if (!ImGui::Begin("Hierarchy"))
        {
            ImGui::End();
            return;
        }
        ImGui::TextUnformatted("シーン");
        ImGui::Separator();
        Scene* scene = m_sceneManager.getActiveScene();
        if (scene != nullptr)
        {
            for (const auto& object : scene->getGameObjects())
            {
                if (object->getParent() == nullptr)
                    drawGameObjectNode(*object);
            }
        }
        ImGui::End();
    }

    void EditorUi::drawGameObjectNode(GameObject& object)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
        if (m_selectedObject == &object)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (object.getChildCount() == 0)
            flags |= ImGuiTreeNodeFlags_Leaf;

        ImGui::PushID(&object);
        const bool open = ImGui::TreeNodeEx(object.getName().c_str(), flags);
        if (ImGui::IsItemClicked())
            m_selectedObject = &object;
        if (open)
        {
            for (std::size_t index = 0; index < object.getChildCount(); ++index)
                drawGameObjectNode(*object.getChild(index));
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void EditorUi::drawInspector()
    {
        if (!ImGui::Begin("Inspector"))
        {
            ImGui::End();
            return;
        }
        ImGui::Text("選択中: %s", m_selectedObject == nullptr ? "なし" : m_selectedObject->getName().c_str());
        ImGui::Separator();
        if (m_selectedObject != nullptr && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            Transform* transform = m_selectedObject->getTransform();
            Vector3 position = transform->getPosition();
            Vector3 rotation = transform->getEulerAngles();
            Vector3 scale = transform->getScale();
            float positionData[3] = { position.x, position.y, position.z };
            float rotationData[3] = { rotation.x, rotation.y, rotation.z };
            float scaleData[3] = { scale.x, scale.y, scale.z };
            if (ImGui::DragFloat3("位置", positionData, 0.1f))
                transform->setPosition(Vector3(positionData[0], positionData[1], positionData[2]));
            if (ImGui::DragFloat3("回転", rotationData, 0.01f))
                transform->setEulerAngles(rotationData[0], rotationData[1], rotationData[2]);
            if (ImGui::DragFloat3("スケール", scaleData, 0.01f))
                transform->setScale(Vector3(scaleData[0], scaleData[1], scaleData[2]));
        }
        if (m_selectedObject != nullptr && ImGui::CollapsingHeader("GameObject", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool active = m_selectedObject->isActiveSelf();
            if (ImGui::Checkbox("Active", &active))
                m_selectedObject->setActive(active);
            int tag = static_cast<int>(m_selectedObject->getTag());
            if (ImGui::InputInt("Tag", &tag) && tag >= 0)
                m_selectedObject->setTag(static_cast<TagID>(tag));
            int layer = static_cast<int>(m_selectedObject->getLayer());
            if (ImGui::InputInt("Layer", &layer) && layer >= 0 && layer < 32)
                m_selectedObject->setLayer(static_cast<LayerID>(layer));
        }
        ImGui::End();
    }

    void EditorUi::drawShaderManager(ShaderManager* shaderManager)
    {
        if (!m_showShaderManager)
            return;

        if (!ImGui::Begin("Shader Manager", &m_showShaderManager))
        {
            ImGui::End();
            return;
        }

        if (shaderManager == nullptr)
        {
            ImGui::TextUnformatted("ShaderManager is not available.");
            ImGui::End();
            return;
        }

        const char* modeString = "Unknown";
        switch (shaderManager->getMode())
        {
        case ShaderMode::Runtime: modeString = "Runtime (.cso load only)"; break;
        case ShaderMode::Editor: modeString = "Editor (Hot Reload Active)"; break;
        case ShaderMode::Development: modeString = "Development (Hot Reload Active)"; break;
        }
        ImGui::Text("Mode: %s", modeString);
        ImGui::Separator();

        if (ImGui::Button("Reload All CSO"))
        {
            shaderManager->reloadAll();
        }
        ImGui::SameLine();
        if (ImGui::Button("Recompile All HLSL"))
        {
            shaderManager->recompileAll();
        }

        ImGui::Separator();
        const std::vector<ShaderID> ids = shaderManager->getAllShaderIDs();
        if (ids.empty())
        {
            ImGui::TextUnformatted("No shaders registered.");
        }
        else if (ImGui::BeginTable("ShaderTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Entry / Stage", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("CSO Path", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (const ShaderID id : ids)
            {
                const ShaderDetails details = shaderManager->getShaderDetails(id);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%u", id);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s (%s)", details.compileDesc.entryPoint.c_str(), shaderStagePrefix(details.compileDesc.stage));

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", shaderTargetProfile(details.compileDesc.stage, details.compileDesc.shaderModel).c_str());

                ImGui::TableSetColumnIndex(3);
                switch (details.status)
                {
                case ShaderStatus::Loaded:
                    ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "[OK] Loaded");
                    break;
                case ShaderStatus::Compiling:
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "[...] Compiling");
                    break;
                case ShaderStatus::Reloading:
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "[...] Reloading");
                    break;
                case ShaderStatus::CompileFailed:
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[ERR] Compile Failed");
                    break;
                case ShaderStatus::ReloadFailed:
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[ERR] Reload Failed");
                    break;
                default:
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Unloaded");
                    break;
                }

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%s", details.compileDesc.sourcePath.filename().string().c_str());

                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%s", details.compileDesc.outputPath.filename().string().c_str());

                ImGui::TableSetColumnIndex(6);
                ImGui::PushID(id);
                if (ImGui::Button("Reload"))
                {
                    shaderManager->recompileShader(id);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }
} // namespace Engine