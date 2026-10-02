#pragma once

#include "Editor\Scene\SceneDocument.h"

struct ImVec2;

namespace Engine
{
    class GameObject;
    class Scene;
    class ShaderManager;

    /**
     * @brief Unity風のEditor DockSpaceと標準Panelを描画するクラス
     * @thread_safety Main thread only.
     */
    class EditorUi
    {
    public:

        /**
         * @brief Editor UIを構築する。
         */
        EditorUi();

        /**
         * @brief Editorのメニューバー、DockSpace、標準Panelを描画する
         * @param shaderManager ShaderManager オブジェクトのポインタ (省略可能)
         * @param gameTextureId Game Viewに表示するImGui Texture Descriptor。
         * @param gameWidth Game Viewの物理ピクセル幅。描画後に必要寸法へ更新する。
         * @param gameHeight Game Viewの物理ピクセル高さ。描画後に必要寸法へ更新する。
         */
        void draw(ShaderManager* shaderManager, std::uint64_t gameTextureId,
            std::uint32_t& gameWidth, std::uint32_t& gameHeight);

    private:

        /**
         * @brief Editorで生成するGameObjectの種類。
         */
        enum class GameObjectCreateType
        {
            Empty,
            Model,
            Camera
        };

        /**
         * @brief Scene ViewのTransform Gizmo操作。
         */
        enum class GizmoOperation
        {
            Translate,
            Rotate,
            Scale
        };

        /**
         * @brief Main threadで新規Sceneを作成する。
         */
        void newScene();

        /**
         * @brief Windows標準ダイアログからSceneを選択して読み込む。
         */
        void openScene();

        /**
         * @brief 現在の保存先へSceneを保存する。
         */
        void saveScene();

        /**
         * @brief Windows標準ダイアログから保存先を選択してSceneを保存する。
         */
        void saveSceneAs();

        /**
         * @brief 選択中のGameObject階層をPrefabとして保存する。
         */
        void saveSelectedAsPrefab();

        /**
         * @brief ファイルからPrefabを読み込み、現在のSceneへ配置する。
         */
        void instantiatePrefab();

        /**
         * @brief Editor操作に応じたGameObjectを生成する。
         * @param scene 生成先Scene。
         * @param type 生成するGameObjectの種類。
         * @param parent 親GameObject。Rootへ生成する場合はnullptr。
         * @return 生成したGameObject。失敗した場合はnullptr。
         */
        GameObject* createGameObject(Scene& scene, GameObjectCreateType type, GameObject* parent = nullptr);

        /**
         * @brief HierarchyでGameObject生成を予約する。
         * @param type 生成するGameObjectの種類。
         * @param parent 親GameObject。Rootへ生成する場合はnullptr。
         */
        void requestGameObjectCreation(GameObjectCreateType type, GameObject* parent = nullptr) noexcept;

        /**
         * @brief GameObject生成Menuを描画する。
         * @param parent 生成先の親GameObject。
         */
        void drawGameObjectCreationMenu(GameObject* parent);

        /**
         * @brief Editorのメニューバーを描画する
         */
        void drawHierarchy();

        /**
         * @brief 選択中GameObject群のTransform Gizmoを描画する。
         */
        void drawSelectedObjectGizmo(const ImVec2& imagePosition, const ImVec2& imageSize);

        /**
         * @brief RendererのGame Textureを表示する。
         * @param textureId ImGuiで参照するTexture Descriptor。
         * @param width Texture幅。
         * @param height Texture高さ。
         */
        void drawGameView(std::uint64_t textureId, std::uint32_t& width, std::uint32_t& height);

        /**
         * @brief Hierarchy内のGameObjectノードを再帰的に描画する
         */
        void drawGameObjectNode(GameObject& object);

        /**
         * @brief Inspectorで操作するGameObjectを選択する
         * @param object 選択するGameObject。選択解除時はnullptr
         */
        void selectObject(GameObject* object);

        /**
         * @brief Hierarchyの複数選択状態を切り替える。
         * @param object 選択状態を切り替えるGameObject。
         */
        void toggleObjectSelection(GameObject& object);

        /**
         * @brief 複数選択されたGameObjectのうち、選択済み祖先を持たないものを取得する。
         * @return 選択階層のルートGameObject。
         */
        std::vector<GameObject*> getSelectedRoots() const;

        /**
         * @brief 選択中のGameObject階層を複製する。
         */
        void duplicateSelectedObjects();

        /**
         * @brief EditorのDockSpaceを描画する
         */
        void drawInspector();

        /**
         * @brief Shader Debug Window / Hot Reload 管理パネルを描画する
         * @param shaderManager ShaderManager オブジェクトのポインタ
         */
        void drawShaderManager(ShaderManager* shaderManager);

        /**
         * @brief Thread Debugパネルを描画する
         */
        void drawThreadDebug();

        bool m_showShaderManager = false;                                         //!< Shader Managerパネルの表示フラグ
        bool m_showThreadDebug = false;                                           //!< Thread Debugパネルの表示フラグ
        GameObject* m_selectedObject = nullptr;                                   //!< Inspectorで選択中のGameObject
        std::vector<GameObject*> m_selectedObjects;                               //!< Hierarchyで選択中のGameObject
        GameObject* m_hierarchyCreateParent = nullptr;                            //!< 作成するGameObjectの親。nullptrならRoot
        std::array<char, 128> m_hierarchySearch{};                                //!< Hierarchyの検索文字列
        std::array<char, 128> m_objectName{};                                     //!< Inspectorで編集中のGameObject名
        Editor::SceneDocument m_sceneDocument;                                    //!< 編集中Sceneのファイル状態
        std::string m_prefabStatus;                                               //!< 直近のPrefab保存結果
        std::string m_hierarchyStatus;                                            //!< Hierarchy操作の結果
        GameObjectCreateType m_hierarchyCreateType = GameObjectCreateType::Empty; //!< 生成予定のGameObject種別
        GizmoOperation m_gizmoOperation = GizmoOperation::Translate;              //!< Transform Gizmo操作
        float m_gridSnapStep = 1.0f;                                              //!< 移動スナップ間隔
        bool m_hierarchyCreateRequested = false;                                  //!< GameObject作成要求
        bool m_hierarchyDuplicateRequested = false;                               //!< GameObject複製要求
        bool m_hierarchyDeleteRequested = false;                                  //!< GameObject削除要求
        bool m_gridSnapEnabled = false;                                           //!< Transform Gizmoのスナップ有効状態
    };
} // namespace Engine
