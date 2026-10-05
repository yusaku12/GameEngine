#include "Pch.h"
#include "Editor\ImGui\EditorUi.h"
#include "Assets\Model\ModelManager.h"
#include "Core\GameObject\Component\AnimatorComponent.h"
#include "Core\GameObject\Component\CameraComponent.h"
#include "Core\GameObject\Component\ModelRendererComponent.h"
#include "Core\Prefab\Prefab.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Core\System\Dialog.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "Core\Threading\ThreadDebugStats.h"
#include "Editor\Camera\FreeCameraController.h"
#include "Graphics\Camera\CameraManager.h"
#include "Graphics\Debug\DebugPrimitive.h"
#include "Graphics\Shader\ShaderManager.h"
#include <imgui.h>
#include <ImGuizmo.h>

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
            m_observedSceneGuid = scene->getGUID();
            GameObject* camera = createGameObject(*scene, GameObjectCreateType::Camera);
            createGameObject(*scene, GameObjectCreateType::Model);
            if (camera != nullptr)
                selectObject(camera);
        }
    }

    void EditorUi::draw(ShaderManager* shaderManager, const std::uint64_t gameTextureId,
        std::uint32_t& gameWidth, std::uint32_t& gameHeight)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        const ImVec2 viewportEnd(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            viewport->Pos, viewportEnd, ImGui::GetColorU32(ImGuiCol_WindowBg));
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
        drawGameView(gameTextureId, gameWidth, gameHeight);
        drawInspector();
        drawShaderManager(shaderManager);
        drawThreadDebug();
    }

    void EditorUi::drawGameView(const std::uint64_t textureId, std::uint32_t& width, std::uint32_t& height)
    {
        if (!ImGui::Begin("Game"))
        {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("Grid Snap", &m_gridSnapEnabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Move uses the configured step; rotation snaps to 15 degrees and scale to 0.1.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragFloat("Move Step", &m_gridSnapStep, 0.05f, 0.01f, 100.0f, "%.2f");

        const ImVec2 availableSize = ImGui::GetContentRegionAvail();
        if (textureId != 0 && width != 0 && height != 0 && availableSize.x > 0.0f && availableSize.y > 0.0f)
        {
            const ImVec2 cursorPosition = ImGui::GetCursorScreenPos();
            const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
            width = static_cast<std::uint32_t>(std::clamp(
                std::lround(availableSize.x * framebufferScale.x), 1l, 16384l));
            height = static_cast<std::uint32_t>(std::clamp(
                std::lround(availableSize.y * framebufferScale.y), 1l, 16384l));
            ImGui::Image(static_cast<ImTextureID>(textureId), availableSize);
            drawSelectedObjectGizmo(cursorPosition, availableSize);
        }

        ImGui::End();
    }

    void EditorUi::drawSelectedObjectGizmo(const ImVec2& imagePosition, const ImVec2& imageSize)
    {
        const bool acceptsShortcuts = !ImGui::GetIO().WantTextInput
            && !ImGui::IsMouseDown(ImGuiMouseButton_Right)
            && !ImGuizmo::IsUsing();
        if (acceptsShortcuts)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W))
                m_gizmoOperation = GizmoOperation::Translate;
            else if (ImGui::IsKeyPressed(ImGuiKey_E))
                m_gizmoOperation = GizmoOperation::Rotate;
            else if (ImGui::IsKeyPressed(ImGuiKey_R))
                m_gizmoOperation = GizmoOperation::Scale;
        }

        const std::vector<GameObject*> selectedRoots = getSelectedRoots();
        for (const GameObject* const selectedObject : selectedRoots)
        {
            const ModelRendererComponent* const modelRenderer = selectedObject->getComponent<ModelRendererComponent>();
            if (modelRenderer != nullptr && modelRenderer->getModel().isValid())
            {
                const std::shared_ptr<const ModelResource> model = ModelManager::instance().get(modelRenderer->getModel());
                if (model != nullptr)
                {
                    AABB worldBounds;
                    model->boundingBox.Transform(worldBounds, selectedObject->getWorldMatrix());
                    DebugPrimitive::instance().drawBox(Matrix::CreateTranslation(worldBounds.Center), worldBounds.Extents);
                }
            }
        }

        CameraComponent* const camera = CameraManager::instance().getActiveCamera();
        if (camera == nullptr || selectedRoots.empty() || m_selectedObject == nullptr)
            return;

        ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
        if (m_gizmoOperation == GizmoOperation::Rotate)
            operation = ImGuizmo::ROTATE;
        else if (m_gizmoOperation == GizmoOperation::Scale)
            operation = ImGuizmo::SCALE;

        Vector3 pivotPosition = Vector3::Zero;
        for (const GameObject* const selectedObject : selectedRoots)
            pivotPosition += selectedObject->getWorldPosition();
        pivotPosition /= static_cast<float>(selectedRoots.size());
        Matrix worldMatrix = Matrix::CreateFromQuaternion(m_selectedObject->getWorldRotation())
            * Matrix::CreateTranslation(pivotPosition);
        const Matrix& viewMatrix = camera->getViewMatrix();
        const Matrix& projectionMatrix = camera->getProjectionMatrix();
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(imagePosition.x, imagePosition.y, imageSize.x, imageSize.y);
        ImGuizmo::SetOrthographic(camera->getProjectionMode() == CameraProjectionMode::Orthographic);

        const ImGuizmo::MODE mode = m_gizmoOperation == GizmoOperation::Scale
            ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        Vector3 snapValues(m_gridSnapStep, m_gridSnapStep, m_gridSnapStep);
        if (m_gizmoOperation == GizmoOperation::Rotate)
            snapValues = Vector3(15.0f, 15.0f, 15.0f);
        else if (m_gizmoOperation == GizmoOperation::Scale)
            snapValues = Vector3(0.1f, 0.1f, 0.1f);
        const float* const snap = m_gridSnapEnabled ? &snapValues.x : nullptr;
        if (ImGuizmo::Manipulate(&viewMatrix._11, &projectionMatrix._11, operation, mode, &worldMatrix._11,
            nullptr, snap))
        {
            const auto hasFiniteMatrix = [](const Matrix& matrix)
                {
                    const std::array<float, 16> values{
                        matrix._11, matrix._12, matrix._13, matrix._14,
                        matrix._21, matrix._22, matrix._23, matrix._24,
                        matrix._31, matrix._32, matrix._33, matrix._34,
                        matrix._41, matrix._42, matrix._43, matrix._44
                    };
                    return std::ranges::all_of(values, [](const float value) { return std::isfinite(value); });
                };

            const auto hasFiniteInverse = [&hasFiniteMatrix](const Matrix& matrix)
                {
                    const float determinant = matrix.Determinant();
                    if (!std::isfinite(determinant) || determinant == 0.0f)
                        return false;

                    return hasFiniteMatrix(matrix.Invert());
                };

            const Matrix pivotMatrix = Matrix::CreateFromQuaternion(m_selectedObject->getWorldRotation())
                * Matrix::CreateTranslation(pivotPosition);
            if (!hasFiniteInverse(pivotMatrix))
            {
                LOG_WARNING("[Editor] Cannot apply transform gizmo because its pivot matrix is invalid.");
                return;
            }

            for (const GameObject* const selectedObject : selectedRoots)
            {
                const GameObject* const parent = selectedObject->getParent();
                if (parent != nullptr && !hasFiniteInverse(parent->getWorldMatrix()))
                {
                    LOG_WARNING("[Editor] Cannot apply transform gizmo because a selected object's parent matrix is singular.");
                    return;
                }
            }

            if (!hasFiniteMatrix(worldMatrix))
            {
                LOG_WARNING("[Editor] Cannot apply transform gizmo because its output matrix contains invalid values.");
                return;
            }

            const Matrix worldToPivot = pivotMatrix.Invert();
            std::vector<Transform> manipulatedTransforms;
            try
            {
                manipulatedTransforms.reserve(selectedRoots.size());
            }
            catch (const std::bad_alloc&)
            {
                LOG_ERROR("[Editor] Failed to allocate manipulated transforms for the selected objects.");
                return;
            }
            catch (const std::length_error&)
            {
                LOG_ERROR("[Editor] Selected object transform collection exceeds its maximum capacity.");
                return;
            }
            for (GameObject* const selectedObject : selectedRoots)
            {
                const Matrix targetWorld = selectedObject->getWorldMatrix() * worldToPivot * worldMatrix;
                const GameObject* const parent = selectedObject->getParent();
                const Matrix localMatrix = parent == nullptr
                    ? targetWorld : targetWorld * parent->getWorldMatrix().Invert();
                if (!hasFiniteMatrix(localMatrix))
                {
                    LOG_WARNING("[Editor] Cannot apply transform gizmo because a resulting local matrix is invalid.");
                    return;
                }

                Transform manipulatedTransform;
                if (!Transform::tryFromMatrix(localMatrix, manipulatedTransform))
                {
                    LOG_WARNING("[Editor] Cannot apply transform gizmo because a resulting local matrix cannot be represented as TRS.");
                    return;
                }

                const Vector3& position = manipulatedTransform.getPosition();
                const Vector3& scale = manipulatedTransform.getScale();
                const Quaternion& rotation = manipulatedTransform.getRotation();
                if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)
                    || !std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z)
                    || !std::isfinite(rotation.x) || !std::isfinite(rotation.y)
                    || !std::isfinite(rotation.z) || !std::isfinite(rotation.w))
                {
                    LOG_WARNING("[Editor] Cannot apply transform gizmo because decomposition produced invalid values.");
                    return;
                }
                manipulatedTransforms.push_back(manipulatedTransform);
            }

            for (std::size_t index = 0; index < selectedRoots.size(); ++index)
            {
                GameObject* const selectedObject = selectedRoots[index];
                Transform* const localTransform = selectedObject->getTransform();
                localTransform->setPosition(manipulatedTransforms[index].getPosition());
                localTransform->setRotation(manipulatedTransforms[index].getRotation());
                localTransform->setScale(manipulatedTransforms[index].getScale());
            }
        }
    }

    void EditorUi::newScene()
    {
        MainThreadDispatcher::instance().post([this]
            {
                if (Scene* scene = SceneManager::instance().getActiveScene())
                {
                    selectObject(nullptr);
                    m_hierarchyCreateParent = nullptr;
                    m_hierarchyCreateParentGuid = {};
                    m_hierarchyCreateRequested = false;
                    m_hierarchyDuplicateRequested = false;
                    m_hierarchyDeleteRequested = false;
                    m_hierarchyStatus.clear();
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

                if (m_sceneDocument.load(*scene, paths.front()))
                {
                    selectObject(nullptr);
                    m_hierarchyCreateParent = nullptr;
                    m_hierarchyCreateParentGuid = {};
                    m_hierarchyCreateRequested = false;
                    m_hierarchyDuplicateRequested = false;
                    m_hierarchyDeleteRequested = false;
                    m_hierarchyStatus.clear();
                }
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

    GameObject* EditorUi::createGameObject(
        Scene& scene,
        const GameObjectCreateType type,
        GameObject* const parent)
    {
        const char* name = "GameObject";
        switch (type)
        {
        case GameObjectCreateType::Model:
            name = "Model";
            break;
        case GameObjectCreateType::Camera:
            name = "Main Camera";
            break;
        default:
            break;
        }

        GameObject* const object = scene.createGameObject(name);
        if (object == nullptr)
            return nullptr;
        if (parent != nullptr && !object->setParent(parent, false))
        {
            scene.destroyGameObject(object);
            return nullptr;
        }

        switch (type)
        {
        case GameObjectCreateType::Model:
            object->addComponent<ModelRendererComponent>();
            break;
        case GameObjectCreateType::Camera:
            object->getTransform()->setPosition(Vector3(-0.087f, 1.768f, 4.772f));
            object->getTransform()->setEulerAngles(0.181f, 3.121f, 0.0f);
            object->addComponent<CameraComponent>();
            object->addComponent<FreeCameraController>();
            break;
        default:
            break;
        }
        return object;
    }

    void EditorUi::requestGameObjectCreation(
        const GameObjectCreateType type,
        GameObject* const parent) noexcept
    {
        m_hierarchyCreateType = type;
        m_hierarchyCreateParent = parent;
        m_hierarchyCreateParentGuid = parent == nullptr ? ObjectGUID{} : parent->getGUID();
        m_hierarchyCreateRequested = true;
    }

    void EditorUi::drawGameObjectCreationMenu(GameObject* const parent)
    {
        if (ImGui::MenuItem("Create Empty"))
            requestGameObjectCreation(GameObjectCreateType::Empty, parent);
    }

    void EditorUi::drawHierarchy()
    {
        Scene* const scene = SceneManager::instance().getActiveScene();
        const ObjectGUID sceneGuid = scene == nullptr ? ObjectGUID{} : scene->getGUID();
        if (sceneGuid != m_observedSceneGuid)
        {
            selectObject(nullptr);
            m_hierarchyCreateParent = nullptr;
            m_hierarchyCreateRequested = false;
            m_hierarchyDuplicateRequested = false;
            m_hierarchyDeleteRequested = false;
            m_hierarchyCreateType = GameObjectCreateType::Empty;
            m_selectedObjectGuid = {};
            m_selectedObjectGuids.clear();
            m_hierarchyCreateParentGuid = {};
            m_observedSceneGuid = sceneGuid;
        }
        else if (scene != nullptr)
        {
            for (std::size_t index = 0; index < m_selectedObjectGuids.size();)
            {
                GameObject* const selected = scene->find(m_selectedObjectGuids[index]);
                if (selected == nullptr)
                {
                    m_selectedObjectGuids.erase(m_selectedObjectGuids.begin() + static_cast<std::ptrdiff_t>(index));
                    m_selectedObjects.erase(m_selectedObjects.begin() + static_cast<std::ptrdiff_t>(index));
                    continue;
                }
                m_selectedObjects[index] = selected;
                ++index;
            }

            m_selectedObject = m_selectedObjectGuid.isValid() ? scene->find(m_selectedObjectGuid) : nullptr;
            if (m_selectedObject == nullptr)
            {
                m_selectedObject = m_selectedObjects.empty() ? nullptr : m_selectedObjects.back();
                m_selectedObjectGuid = m_selectedObjectGuids.empty() ? ObjectGUID{} : m_selectedObjectGuids.back();
                m_objectName.fill('\0');
                if (m_selectedObject != nullptr)
                    std::snprintf(m_objectName.data(), m_objectName.size(), "%s", m_selectedObject->getName().c_str());
            }
            if (m_hierarchyCreateParentGuid.isValid())
            {
                m_hierarchyCreateParent = scene->find(m_hierarchyCreateParentGuid);
                if (m_hierarchyCreateParent == nullptr)
                {
                    m_hierarchyCreateParentGuid = {};
                    m_hierarchyCreateRequested = false;
                    m_hierarchyStatus = "Could not create object: its parent is no longer in the active scene.";
                }
            }
        }

        if (!ImGui::Begin("Hierarchy"))
        {
            ImGui::End();
            return;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create GameObject");
        if (ImGui::BeginPopup("CreateGameObjectPopup"))
        {
            drawGameObjectCreationMenu(nullptr);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##HierarchySearch", "Search", m_hierarchySearch.data(), m_hierarchySearch.size());
        ImGui::TextDisabled("Ctrl-click: multi-select | Ctrl+D: duplicate | Delete: delete");
        if (!m_hierarchyStatus.empty())
            ImGui::TextDisabled("%s", m_hierarchyStatus.c_str());
        ImGui::Separator();
        if (scene != nullptr)
        {
            ImGui::Selectable(scene->getName().c_str());
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_GAME_OBJECT"))
                {
                    if (payload->Data == nullptr || payload->DataSize != sizeof(ObjectGUID))
                    {
                        m_hierarchyStatus = "Could not move object: invalid drag payload.";
                    }
                    else
                    {
                        ObjectGUID droppedGUID;
                        std::memcpy(&droppedGUID, payload->Data, sizeof(droppedGUID));
                        GameObject* const droppedObject = scene->find(droppedGUID);
                        if (droppedObject == nullptr)
                            m_hierarchyStatus = "Could not move object: it is no longer in the active scene.";
                        else if (!droppedObject->setParent(nullptr))
                            m_hierarchyStatus = "Could not move object to the scene root.";
                        else
                            m_hierarchyStatus.clear();
                    }
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
                drawGameObjectCreationMenu(nullptr);
                if (ImGui::MenuItem("Instantiate Prefab..."))
                    instantiatePrefab();
                ImGui::EndPopup();
            }

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
                && !ImGui::GetIO().WantTextInput)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_selectedObjects.empty())
                    m_hierarchyDeleteRequested = true;
                if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)
                    && !m_selectedObjects.empty())
                    m_hierarchyDuplicateRequested = true;
            }

            if (m_hierarchyCreateRequested)
            {
                GameObject* const object = createGameObject(
                    *scene, m_hierarchyCreateType, m_hierarchyCreateParent);
                selectObject(object);
                m_hierarchyCreateRequested = false;
                m_hierarchyCreateParent = nullptr;
                m_hierarchyCreateParentGuid = {};
                m_hierarchyCreateType = GameObjectCreateType::Empty;
            }
            if (m_hierarchyDuplicateRequested)
            {
                duplicateSelectedObjects();
                m_hierarchyDuplicateRequested = false;
            }
            if (m_hierarchyDeleteRequested)
            {
                for (GameObject* const selectedRoot : getSelectedRoots())
                    scene->destroyGameObject(selectedRoot);
                selectObject(nullptr);
                m_hierarchyDeleteRequested = false;
            }
        }
        ImGui::End();
    }

    void EditorUi::drawGameObjectNode(GameObject& object)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (std::find(m_selectedObjects.begin(), m_selectedObjects.end(), &object) != m_selectedObjects.end())
            flags |= ImGuiTreeNodeFlags_Selected;
        if (object.getChildCount() == 0)
            flags |= ImGuiTreeNodeFlags_Leaf;

        ImGui::PushID(&object);
        const bool open = ImGui::TreeNodeEx(object.getName().c_str(), flags);
        if (ImGui::IsItemClicked())
        {
            if (ImGui::GetIO().KeyCtrl)
                toggleObjectSelection(object);
            else if (m_selectedObjects.size() != 1 || m_selectedObjects.front() != &object)
                selectObject(&object);
        }
        if (ImGui::BeginDragDropSource())
        {
            const ObjectGUID payloadGUID = object.getGUID();
            ImGui::SetDragDropPayload("HIERARCHY_GAME_OBJECT", &payloadGUID, sizeof(payloadGUID));
            ImGui::TextUnformatted(object.getName().c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_GAME_OBJECT"))
            {
                if (payload->Data == nullptr || payload->DataSize != sizeof(ObjectGUID))
                {
                    m_hierarchyStatus = "Could not change object parent: invalid drag payload.";
                }
                else
                {
                    ObjectGUID droppedGUID;
                    std::memcpy(&droppedGUID, payload->Data, sizeof(droppedGUID));
                    Scene* const scene = SceneManager::instance().getActiveScene();
                    GameObject* const droppedObject = scene == nullptr ? nullptr : scene->find(droppedGUID);
                    if (droppedObject == nullptr)
                        m_hierarchyStatus = "Could not change object parent: it is no longer in the active scene.";
                    else if (!droppedObject->setParent(&object))
                        m_hierarchyStatus = "Could not change object parent.";
                    else
                        m_hierarchyStatus.clear();
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::BeginMenu("Create Child"))
            {
                drawGameObjectCreationMenu(&object);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Delete"))
            {
                if (std::find(m_selectedObjects.begin(), m_selectedObjects.end(), &object) == m_selectedObjects.end())
                    selectObject(&object);
                m_hierarchyDeleteRequested = true;
            }
            if (ImGui::MenuItem("Duplicate"))
            {
                if (std::find(m_selectedObjects.begin(), m_selectedObjects.end(), &object) == m_selectedObjects.end())
                    selectObject(&object);
                m_hierarchyDuplicateRequested = true;
            }
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
        m_selectedObjectGuid = object == nullptr ? ObjectGUID{} : object->getGUID();
        m_selectedObjects.clear();
        m_selectedObjectGuids.clear();
        if (object != nullptr)
        {
            m_selectedObjects.push_back(object);
            m_selectedObjectGuids.push_back(m_selectedObjectGuid);
        }
        m_objectName.fill('\0');
        if (object != nullptr)
            std::snprintf(m_objectName.data(), m_objectName.size(), "%s", object->getName().c_str());
    }

    void EditorUi::toggleObjectSelection(GameObject& object)
    {
        const auto selected = std::find(m_selectedObjects.begin(), m_selectedObjects.end(), &object);
        if (selected == m_selectedObjects.end())
        {
            m_selectedObjects.push_back(&object);
            m_selectedObjectGuids.push_back(object.getGUID());
            m_selectedObject = &object;
            m_selectedObjectGuid = object.getGUID();
        }
        else
        {
            const std::size_t index = static_cast<std::size_t>(selected - m_selectedObjects.begin());
            m_selectedObjects.erase(selected);
            m_selectedObjectGuids.erase(m_selectedObjectGuids.begin() + static_cast<std::ptrdiff_t>(index));
            m_selectedObject = m_selectedObjects.empty() ? nullptr : m_selectedObjects.back();
            m_selectedObjectGuid = m_selectedObjectGuids.empty() ? ObjectGUID{} : m_selectedObjectGuids.back();
        }

        m_objectName.fill('\0');
        if (m_selectedObject != nullptr)
            std::snprintf(m_objectName.data(), m_objectName.size(), "%s", m_selectedObject->getName().c_str());
    }

    std::vector<GameObject*> EditorUi::getSelectedRoots() const
    {
        std::vector<GameObject*> roots;
        roots.reserve(m_selectedObjects.size());
        for (GameObject* const object : m_selectedObjects)
        {
            if (object == nullptr)
                continue;

            bool hasSelectedAncestor = false;
            for (const GameObject* parent = object->getParent(); parent != nullptr; parent = parent->getParent())
            {
                if (std::find(m_selectedObjects.begin(), m_selectedObjects.end(), parent) != m_selectedObjects.end())
                {
                    hasSelectedAncestor = true;
                    break;
                }
            }
            if (!hasSelectedAncestor)
                roots.push_back(object);
        }
        return roots;
    }

    void EditorUi::duplicateSelectedObjects()
    {
        Scene* const scene = SceneManager::instance().getActiveScene();
        const std::vector<GameObject*> selectedRoots = getSelectedRoots();
        if (scene == nullptr)
        {
            m_hierarchyStatus = "Duplicate failed: no active scene.";
            return;
        }
        if (selectedRoots.empty())
            return;

        std::vector<Prefab> prefabs(selectedRoots.size());
        for (std::size_t index = 0; index < selectedRoots.size(); ++index)
        {
            if (!prefabs[index].capture(*selectedRoots[index]))
            {
                m_hierarchyStatus = "Duplicate failed: could not capture " + selectedRoots[index]->getName() + ".";
                return;
            }
        }

        std::vector<GameObject*> duplicates;
        duplicates.reserve(prefabs.size());
        bool instantiationFailed = false;
        for (const Prefab& prefab : prefabs)
        {
            GameObject* const duplicate = prefab.instantiate(*scene);
            if (duplicate == nullptr)
            {
                m_hierarchyStatus = "Duplicate partially failed: a selected object could not be instantiated.";
                instantiationFailed = true;
                break;
            }

            duplicate->getTransform()->translate(Vector3(m_gridSnapStep, 0.0f, 0.0f));
            duplicates.push_back(duplicate);
        }

        if (duplicates.empty())
            return;

        m_selectedObjects = std::move(duplicates);
        m_selectedObjectGuids.clear();
        m_selectedObjectGuids.reserve(m_selectedObjects.size());
        for (const GameObject* const duplicate : m_selectedObjects)
            m_selectedObjectGuids.push_back(duplicate->getGUID());
        m_selectedObject = m_selectedObjects.back();
        m_selectedObjectGuid = m_selectedObjectGuids.back();
        m_objectName.fill('\0');
        std::snprintf(m_objectName.data(), m_objectName.size(), "%s", m_selectedObject->getName().c_str());
        if (!instantiationFailed)
            m_hierarchyStatus = "Duplicated " + std::to_string(m_selectedObjects.size()) + " object(s).";
    }

    void EditorUi::drawInspector()
    {
        if (!ImGui::Begin("Inspector"))
        {
            ImGui::End();
            return;
        }
        if (m_selectedObjects.size() > 1)
        {
            ImGui::Text("%zu objects selected", m_selectedObjects.size());
            ImGui::TextDisabled("Use the Game view gizmo to transform the selection together.");
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

        ImGui::Separator();
        if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
            ImGui::OpenPopup("AddComponentPopup");
        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            if (!m_selectedObject->hasComponent<ModelRendererComponent>()
                && ImGui::MenuItem("Model Renderer"))
            {
                m_selectedObject->addComponent<ModelRendererComponent>();
            }
            const bool hasModelRenderer = m_selectedObject->hasComponent<ModelRendererComponent>();
            const bool hasAnimator = m_selectedObject->hasComponent<AnimatorComponent>();
            if (ImGui::MenuItem("Animator", nullptr, false, hasModelRenderer && !hasAnimator))
                m_selectedObject->addComponent<AnimatorComponent>();
            if (!m_selectedObject->hasComponent<CameraComponent>()
                && ImGui::MenuItem("Camera"))
            {
                m_selectedObject->addComponent<CameraComponent>();
            }

            const bool hasCamera = m_selectedObject->hasComponent<CameraComponent>();
            const bool hasController = m_selectedObject->hasComponent<FreeCameraController>();
            if (ImGui::MenuItem("Free Camera Controller", nullptr, false, hasCamera && !hasController))
                m_selectedObject->addComponent<FreeCameraController>();
            ImGui::EndPopup();
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