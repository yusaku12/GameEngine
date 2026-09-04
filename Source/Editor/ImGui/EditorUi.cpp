#include "Pch.h"
#include "Editor\ImGui\EditorUi.h"
#include "Graphics\Shader\ShaderManager.h"

#include <imgui.h>

namespace Engine
{
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
            if (ImGui::BeginMenu("編集"))
            {
                ImGui::MenuItem("元に戻す", "Ctrl+Z");
                ImGui::MenuItem("やり直す", "Ctrl+Y");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window"))
            {
                ImGui::MenuItem("統計", nullptr, &m_showStats);
                ImGui::MenuItem("グリッド", nullptr, &m_showGrid);
                ImGui::MenuItem("Shader Manager", nullptr, &m_showShaderManager);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        drawHierarchy();
        drawSceneView();
        drawGameView();
        drawInspector();
        drawProject();
        drawConsole();
        drawShaderManager(shaderManager);
        drawStatusBar();
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
        constexpr const char* objects[] = { "Main Camera", "Directional Light", "Triangle" };
        for (int index = 0; index < static_cast<int>(std::size(objects)); ++index)
        {
            ImGui::PushID(index);
            if (ImGui::Selectable(objects[index], m_selectedObject == index))
                m_selectedObject = index;
            ImGui::PopID();
        }
        ImGui::End();
    }

    void EditorUi::drawSceneView()
    {
        if (!ImGui::Begin("Scene"))
        {
            ImGui::End();
            return;
        }
        ImGui::TextUnformatted("Scene View");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.0f);
        ImGui::SetNextItemWidth(80.0f);
        ImGui::Combo("##Gizmo", &m_selectedObject, "Move\0Rotate\0Scale\0");
        ImGui::Separator();
        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        const ImVec2 canvasPosition = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("SceneCanvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const ImU32 canvasColor = ImGui::GetColorU32(ImGuiCol_FrameBg);
        ImGui::GetWindowDrawList()->AddRectFilled(canvasPosition, ImVec2(canvasPosition.x + canvasSize.x, canvasPosition.y + canvasSize.y), canvasColor);
        if (m_showGrid)
        {
            const ImU32 gridColor = ImGui::GetColorU32(ImGuiCol_Border);
            for (float x = canvasPosition.x; x < canvasPosition.x + canvasSize.x; x += 32.0f)
                ImGui::GetWindowDrawList()->AddLine(ImVec2(x, canvasPosition.y), ImVec2(x, canvasPosition.y + canvasSize.y), gridColor);
            for (float y = canvasPosition.y; y < canvasPosition.y + canvasSize.y; y += 32.0f)
                ImGui::GetWindowDrawList()->AddLine(ImVec2(canvasPosition.x, y), ImVec2(canvasPosition.x + canvasSize.x, y), gridColor);
        }
        ImGui::End();
    }

    void EditorUi::drawGameView()
    {
        if (!ImGui::Begin("Game"))
        {
            ImGui::End();
            return;
        }
        ImGui::TextUnformatted("Game View");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 72.0f);
        if (ImGui::Button(m_playing ? "停止" : "再生"))
            m_playing = !m_playing;
        ImGui::Separator();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + size.x, ImGui::GetCursorScreenPos().y + size.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + size.y * 0.5f - ImGui::GetTextLineHeight() * 0.5f);
        ImGui::Text("%s", m_playing ? "ゲーム実行中" : "ゲーム停止中");
        ImGui::End();
    }

    void EditorUi::drawInspector()
    {
        if (!ImGui::Begin("Inspector"))
        {
            ImGui::End();
            return;
        }
        ImGui::Text("選択中: %s", m_selectedObject == 0 ? "Main Camera" : m_selectedObject == 1 ? "Directional Light" : "Triangle");
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static float position[3] = { 0.0f, 0.0f, 0.0f };
            static float rotation[3] = { 0.0f, 0.0f, 0.0f };
            static float scale[3] = { 1.0f, 1.0f, 1.0f };
            ImGui::DragFloat3("位置", position, 0.1f);
            ImGui::DragFloat3("回転", rotation, 1.0f);
            ImGui::DragFloat3("スケール", scale, 0.01f);
        }
        if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static bool visible = true;
            static float color[4] = { 0.25f, 0.65f, 1.0f, 1.0f };
            ImGui::Checkbox("表示", &visible);
            ImGui::ColorEdit4("カラー", color);
        }
        ImGui::End();
    }

    void EditorUi::drawProject()
    {
        if (!ImGui::Begin("Project"))
        {
            ImGui::End();
            return;
        }
        ImGui::TextUnformatted("Assets");
        ImGui::Separator();
        ImGui::BulletText("Shaders/ColorTriangle.hlsl");
        ImGui::BulletText("Scenes/Sample.scene");
        ImGui::End();
    }

    void EditorUi::drawConsole()
    {
        if (!ImGui::Begin("Console"))
        {
            ImGui::End();
            return;
        }
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "[Info] Editor initialized");
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "[Debug] DirectX 12 renderer ready");
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

    void EditorUi::drawStatusBar()
    {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkPos.x, ImGui::GetMainViewport()->WorkPos.y + ImGui::GetMainViewport()->WorkSize.y - 24.0f));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetMainViewport()->WorkSize.x, 24.0f));
        if (!ImGui::Begin("##StatusBar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::End();
            return;
        }
        ImGui::TextUnformatted(m_playing ? "再生中" : "編集モード");
        ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
        ImGui::TextUnformatted("DirectX 12 | Ready");
        ImGui::End();
    }
} // namespace Engine