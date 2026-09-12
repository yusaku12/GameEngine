#include "Pch.h"
#include "Editor\ImGui\EditorUi.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Core\System\Dialog.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Core\Threading\ThreadDebugStats.h"
#include "Graphics\Shader\ShaderManager.h"

#include <imgui.h>

namespace Engine
{
    namespace
    {
        /**
         * @brief 大文字小文字を区別せずに文字列が含まれているかを判定する。
         * @param text 検索対象の文字列
         * @param query 検索する文字列
         * @return 含まれている場合はtrue、含まれていない場合はfalse
         */
        bool containsCaseInsensitive(const std::string_view text, const std::string_view query)
        {
            if (query.empty())
                return true;
            if (query.size() > text.size())
                return false;

            const auto equalsIgnoreCase = [](const char left, const char right)
                {
                    const auto toLower = [](const unsigned char value)
                        {
                            return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
                        };
                    return toLower(static_cast<unsigned char>(left)) == toLower(static_cast<unsigned char>(right));
                };
            return std::search(text.begin(), text.end(), query.begin(), query.end(), equalsIgnoreCase) != text.end();
        }

        /**
         * @brief GameObjectの階層構造を再帰的に検索し、名前が一致するかを判定する。
         * @param object 検索対象のGameObject
         * @param query 検索する文字列
         * @return 一致する場合はtrue、一致しない場合はfalse
         */
        bool hierarchyMatches(const GameObject& object, const std::string_view query)
        {
            if (containsCaseInsensitive(object.getName(), query))
                return true;
            for (std::size_t index = 0; index < object.getChildCount(); ++index)
            {
                if (hierarchyMatches(*object.getChild(index), query))
                    return true;
            }
            return false;
        }

        /**
         * @brief GameObjectの階層構造を再帰的に検索し、指定したGameObjectが含まれているかを判定する。
         * @param root 検索対象のGameObject
         * @param object 検索するGameObject
         * @return 含まれている場合はtrue、含まれていない場合はfalse
         */
        bool containsObject(const GameObject& root, const GameObject* object)
        {
            if (&root == object)
                return true;
            for (std::size_t index = 0; index < root.getChildCount(); ++index)
            {
                if (containsObject(*root.getChild(index), object))
                    return true;
            }
            return false;
        }

        /**
         * @brief ThreadDebugTaskの列挙値を文字列に変換する。
         * @param task ThreadDebugTaskの列挙値
         * @return 文字列
         */
        const char* threadDebugTaskName(const ThreadDebugTask task)
        {
            switch (task)
            {
            case ThreadDebugTask::GameUpdate: return "Game Update";
            case ThreadDebugTask::RenderUpdate: return "Render Update";
            case ThreadDebugTask::JobExecution: return "Job Execution";
            default: return "Unknown";
            }
        }
    }

    EditorUi::EditorUi()
    {
        Scene* scene = SceneManager::instance().createScene("EditorScene");
        if (scene != nullptr)
        {
            GameObject* camera = scene->createGameObject("Main Camera");
            scene->createGameObject("Directional Light");
            if (camera != nullptr)
                selectObject(camera);
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
                if (ImGui::MenuItem("新規シーン"))
                    newScene();
                if (ImGui::MenuItem("シーンを開く..."))
                    openScene();
                if (ImGui::MenuItem("シーンを保存"))
                    saveScene();
                ImGui::Separator();
                if (!m_sceneDocument.status().empty())
                {
                    ImGui::Separator();
                    ImGui::TextDisabled("%s", m_sceneDocument.status().c_str());
                }
                if (!m_prefabStatus.empty())
                    ImGui::TextDisabled("%s", m_prefabStatus.c_str());
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window"))
            {
                ImGui::MenuItem("Shader Manager", nullptr, &m_showShaderManager);
                ImGui::MenuItem("Thread Debug", nullptr, &m_showThreadDebug);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        drawHierarchy();
        drawInspector();
        drawShaderManager(shaderManager);
        drawThreadDebug();
    }

    void EditorUi::newScene()
    {
        MainThreadDispatcher::instance().post([this]
            {
                if (Scene* scene = SceneManager::instance().getActiveScene())
                {
                    selectObject(nullptr);
                    m_hierarchyCreateParent = nullptr;
                    m_hierarchyDeleteTarget = nullptr;
                    m_hierarchyCreateRequested = false;
                    scene->clear();
                    m_sceneDocument.reset();
                }
            });
    }

    void EditorUi::openScene()
    {
        const HWND ownerWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
        MainThreadDispatcher::instance().post([this, ownerWindow]
            {
                static constexpr std::array filters = {
                    FileDialogFilter{ L"GameEngine Scene", L"*.scene" },
                    FileDialogFilter{ L"All Files", L"*.*" }
                };
                const std::filesystem::path initialPath = m_sceneDocument.hasPath()
                    ? m_sceneDocument.path() : std::filesystem::path("Assets/Scenes");
                std::vector<std::filesystem::path> paths;
                if (Dialog::openFile(paths, L"シーンを開く", initialPath, filters, false, ownerWindow) != DialogResult::Ok)
                    return;

                Scene* scene = SceneManager::instance().getActiveScene();
                if (scene == nullptr || paths.empty())
                    return;

                selectObject(nullptr);
                m_hierarchyCreateParent = nullptr;
                m_hierarchyDeleteTarget = nullptr;
                m_hierarchyCreateRequested = false;
                static_cast<void>(m_sceneDocument.load(*scene, paths.front()));
            });
    }

    void EditorUi::saveScene()
    {
        if (!m_sceneDocument.hasPath())
        {
            saveSceneAs();
            return;
        }

        MainThreadDispatcher::instance().post([this]
            {
                if (const Scene* scene = SceneManager::instance().getActiveScene())
                    static_cast<void>(m_sceneDocument.save(*scene));
            });
    }

    void EditorUi::saveSceneAs()
    {
        const HWND ownerWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
        MainThreadDispatcher::instance().post([this, ownerWindow]
            {
                static constexpr std::array filters = {
                    FileDialogFilter{ L"GameEngine Scene", L"*.scene" },
                    FileDialogFilter{ L"All Files", L"*.*" }
                };
                Scene* scene = SceneManager::instance().getActiveScene();
                if (scene == nullptr)
                    return;

                const std::filesystem::path initialPath = m_sceneDocument.hasPath()
                    ? m_sceneDocument.path()
                    : std::filesystem::path("Assets/Scenes") / (scene->getName() + ".scene");
                std::error_code error;
                std::filesystem::create_directories(initialPath.parent_path(), error);

                std::filesystem::path path;
                if (Dialog::saveFile(path, L"シーンを保存", initialPath, L"scene", filters, ownerWindow) == DialogResult::Ok)
                    static_cast<void>(m_sceneDocument.saveAs(*scene, path));
            });
    }

    void EditorUi::saveSelectedAsPrefab()
    {
        if (m_selectedObject == nullptr)
            return;

        const ObjectGUID selectedGuid = m_selectedObject->getGUID();
        const HWND ownerWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
        MainThreadDispatcher::instance().post([this, selectedGuid, ownerWindow]
            {
                Scene* scene = SceneManager::instance().getActiveScene();
                GameObject* selectedObject = scene == nullptr ? nullptr : scene->find(selectedGuid);
                if (selectedObject == nullptr)
                {
                    m_prefabStatus = "Prefab save failed: object no longer exists.";
                    return;
                }

                const std::filesystem::path initialPath = std::filesystem::path("Assets/Prefabs")
                    / (selectedObject->getName() + ".prefab");
                std::error_code error;
                std::filesystem::create_directories(initialPath.parent_path(), error);
                if (error)
                {
                    m_prefabStatus = "Prefab save failed: " + error.message();
                    return;
                }

                static constexpr std::array filters = {
                    FileDialogFilter{ L"GameEngine Prefab", L"*.prefab" },
                    FileDialogFilter{ L"All Files", L"*.*" }
                };
                std::filesystem::path path;
                if (Dialog::saveFile(path, L"Prefabとして保存", initialPath, L"prefab", filters, ownerWindow) != DialogResult::Ok)
                    return;

                Prefab prefab;
                if (!prefab.capture(*selectedObject) || !Serialization::PrefabSerializer{}.save(path, prefab))
                {
                    m_prefabStatus = "Prefab save failed: " + path.string();
                    return;
                }
                m_prefabStatus = "Prefab saved: " + path.string();
            });
    }

    void EditorUi::instantiatePrefab()
    {
        const HWND ownerWindow = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
        MainThreadDispatcher::instance().post([this, ownerWindow]
            {
                static constexpr std::array filters = {
                    FileDialogFilter{ L"GameEngine Prefab", L"*.prefab" },
                    FileDialogFilter{ L"All Files", L"*.*" }
                };
                std::vector<std::filesystem::path> paths;
                if (Dialog::openFile(paths, L"Prefabを配置", "Assets/Prefabs", filters, false, ownerWindow) != DialogResult::Ok)
                    return;

                Scene* scene = SceneManager::instance().getActiveScene();
                if (scene == nullptr || paths.empty())
                {
                    m_prefabStatus = "Prefab load failed: no active scene.";
                    return;
                }

                Prefab prefab;
                if (!Serialization::PrefabSerializer{}.load(paths.front(), prefab))
                {
                    m_prefabStatus = "Prefab load failed: " + paths.front().string();
                    return;
                }

                GameObject* root = prefab.instantiate(*scene);
                if (root == nullptr)
                {
                    m_prefabStatus = "Prefab instantiate failed: " + paths.front().string();
                    return;
                }

                selectObject(root);
                m_prefabStatus = "Prefab instantiated: " + paths.front().string();
            });
    }

    void EditorUi::drawHierarchy()
    {
        if (!ImGui::Begin("Hierarchy"))
        {
            ImGui::End();
            return;
        }
        if (ImGui::Button("+"))
        {
            m_hierarchyCreateParent = nullptr;
            m_hierarchyCreateRequested = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create Empty GameObject");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##HierarchySearch", "Search", m_hierarchySearch.data(), m_hierarchySearch.size());
        ImGui::Separator();
        Scene* scene = SceneManager::instance().getActiveScene();
        if (scene != nullptr)
        {
            ImGui::Selectable(scene->getName().c_str());
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_GAME_OBJECT"))
                {
                    GameObject* droppedObject = *static_cast<GameObject* const*>(payload->Data);
                    droppedObject->setParent(nullptr);
                }
                ImGui::EndDragDropTarget();
            }

            const std::string_view query(m_hierarchySearch.data());
            for (const auto& object : scene->getGameObjects())
            {
                if (object->getParent() == nullptr && hierarchyMatches(*object, query))
                    drawGameObjectNode(*object);
            }

            if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                if (ImGui::MenuItem("Create Empty"))
                {
                    m_hierarchyCreateParent = nullptr;
                    m_hierarchyCreateRequested = true;
                }
                if (ImGui::MenuItem("Instantiate Prefab..."))
                    instantiatePrefab();
                ImGui::EndPopup();
            }

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
                && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput)
                m_hierarchyDeleteTarget = m_selectedObject;

            if (m_hierarchyCreateRequested)
            {
                GameObject* object = scene->createGameObject("GameObject");
                if (object != nullptr && m_hierarchyCreateParent != nullptr)
                    object->setParent(m_hierarchyCreateParent, false);
                selectObject(object);
                m_hierarchyCreateRequested = false;
                m_hierarchyCreateParent = nullptr;
            }
            if (m_hierarchyDeleteTarget != nullptr)
            {
                if (containsObject(*m_hierarchyDeleteTarget, m_selectedObject))
                    selectObject(nullptr);
                scene->destroyGameObject(m_hierarchyDeleteTarget);
                m_hierarchyDeleteTarget = nullptr;
            }
        }
        ImGui::End();
    }

    void EditorUi::drawGameObjectNode(GameObject& object)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_selectedObject == &object)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (object.getChildCount() == 0)
            flags |= ImGuiTreeNodeFlags_Leaf;

        ImGui::PushID(&object);
        const bool open = ImGui::TreeNodeEx(object.getName().c_str(), flags);
        if (ImGui::IsItemClicked())
            selectObject(&object);
        if (ImGui::BeginDragDropSource())
        {
            GameObject* payloadObject = &object;
            ImGui::SetDragDropPayload("HIERARCHY_GAME_OBJECT", &payloadObject, sizeof(payloadObject));
            ImGui::TextUnformatted(object.getName().c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_GAME_OBJECT"))
            {
                GameObject* droppedObject = *static_cast<GameObject* const*>(payload->Data);
                droppedObject->setParent(&object);
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Create Empty Child"))
            {
                m_hierarchyCreateParent = &object;
                m_hierarchyCreateRequested = true;
            }
            if (ImGui::MenuItem("Delete"))
                m_hierarchyDeleteTarget = &object;
            if (ImGui::MenuItem("Save as Prefab..."))
            {
                selectObject(&object);
                saveSelectedAsPrefab();
            }
            ImGui::EndPopup();
        }
        if (open)
        {
            for (std::size_t index = 0; index < object.getChildCount(); ++index)
            {
                GameObject* child = object.getChild(index);
                if (hierarchyMatches(*child, m_hierarchySearch.data()))
                    drawGameObjectNode(*child);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void EditorUi::selectObject(GameObject* object)
    {
        m_selectedObject = object;
        m_objectName.fill('\0');
        if (object != nullptr)
            std::snprintf(m_objectName.data(), m_objectName.size(), "%s", object->getName().c_str());
    }

    void EditorUi::drawInspector()
    {
        if (!ImGui::Begin("Inspector"))
        {
            ImGui::End();
            return;
        }
        if (m_selectedObject == nullptr)
        {
            ImGui::TextDisabled("Select a GameObject to inspect it.");
            ImGui::End();
            return;
        }

        bool active = m_selectedObject->isActiveSelf();
        if (ImGui::Checkbox("##Active", &active))
            m_selectedObject->setActive(active);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##Name", m_objectName.data(), m_objectName.size()))
            m_selectedObject->setName(m_objectName.data());

        if (ImGui::BeginTable("GameObjectSettings", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextColumn();
            int tag = static_cast<int>(m_selectedObject->getTag());
            if (ImGui::InputInt("Tag", &tag) && tag >= 0)
                m_selectedObject->setTag(static_cast<TagID>(tag));
            ImGui::TableNextColumn();
            int layer = static_cast<int>(m_selectedObject->getLayer());
            if (ImGui::InputInt("Layer", &layer) && layer >= 0 && layer < 32)
                m_selectedObject->setLayer(static_cast<LayerID>(layer));
            ImGui::EndTable();
        }

        ImGui::Separator();
        m_selectedObject->forEachComponent([](Component& component, const std::type_index& type)
            {
                const ComponentTypeInfo* typeInfo = ComponentRegistry::instance().get(type);
                if (typeInfo == nullptr)
                    return;

                ImGui::PushID(&component);
                if (!typeInfo->required)
                {
                    bool enabled = component.isEnabled();
                    if (ImGui::Checkbox("##Enabled", &enabled))
                        component.setEnabled(enabled);
                    ImGui::SameLine();
                }
                if (ImGui::CollapsingHeader(typeInfo->name.data(), ImGuiTreeNodeFlags_DefaultOpen))
                    component.drawImGui();
                ImGui::PopID();
            });
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

    void EditorUi::drawThreadDebug()
    {
        if (!m_showThreadDebug)
            return;

        if (!ImGui::Begin("Thread Debug", &m_showThreadDebug))
        {
            ImGui::End();
            return;
        }

        const ThreadDebugSnapshot snapshot = ThreadDebugStats::instance().capture();
        ImGui::Text("Worker Threads: %u / Hardware Threads: %u", snapshot.workerCount, snapshot.hardwareThreadCount);
        ImGui::Separator();

        if (ImGui::BeginTable("ThreadDebugTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Task", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Running", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Runs", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Last Thread", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Last ms", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Avg ms", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (uint32_t taskValue = 0; taskValue < static_cast<uint32_t>(ThreadDebugTask::Count); ++taskValue)
            {
                const ThreadDebugTask task = static_cast<ThreadDebugTask>(taskValue);
                const ThreadDebugTaskSnapshot& taskSnapshot = snapshot.tasks[taskValue];
                const double lastMilliseconds = static_cast<double>(taskSnapshot.lastDurationMicroseconds) / 1000.0;
                const double averageMilliseconds = taskSnapshot.totalRuns == 0
                    ? 0.0
                    : static_cast<double>(taskSnapshot.totalDurationMicroseconds) / static_cast<double>(taskSnapshot.totalRuns) / 1000.0;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(threadDebugTaskName(task));
                ImGui::TableSetColumnIndex(1);
                if (taskSnapshot.activeCount > 0)
                    ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "%u", taskSnapshot.activeCount);
                else
                    ImGui::TextUnformatted("0");
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%llu", static_cast<unsigned long long>(taskSnapshot.totalRuns));
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u", taskSnapshot.lastThreadId);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.3f", lastMilliseconds);
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%.3f", averageMilliseconds);
            }

            ImGui::EndTable();
        }

        ImGui::End();
    }
} // namespace Engine