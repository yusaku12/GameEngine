#pragma once

#include "Core\GameObject\Component.h"
#include "Core\GameObject\Component\TransformComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\Math\Transform.h"
#include "Core\Object\ObjectGUID.h"
#include "Core\Scene\LayerManager.h"
#include "Core\Scene\TagManager.h"

namespace Engine
{
    class GameObjectManager;

    /**
     * @brief Componentを所有し、親子階層を形成するゲームオブジェクト。
     * @note ECS Entityではありません。Scene/Managerから所有される通常のオブジェクトです。
     * @thread_safety Main thread only.
     */
    class GameObject
    {
    public:

        ~GameObject();

        GameObject(const GameObject&) = delete;
        GameObject& operator=(const GameObject&) = delete;
        GameObject(GameObject&&) = delete;
        GameObject& operator=(GameObject&&) = delete;

        /**
         * @brief GameObjectのGUIDを取得する。
         */
        const ObjectGUID& getGUID() const noexcept { return m_guid; }

        /**
         * @brief GameObjectの名前を取得する。
         */
        const std::string& getName() const noexcept { return m_name; }

        /**
         * @brief GameObjectの名前を設定する。
         * @param name 設定する名前
         */
        void setName(std::string name) { m_name = std::move(name); }

        /**
         * @brief GameObjectのTagを取得する。
         * @return GameObjectのTag
         */
        TagID getTag() const noexcept { return m_tag; }

        /**
         * @brief GameObjectのTagを設定する。
         * @param tag 設定するTag
         */
        void setTag(TagID tag) noexcept { m_tag = tag; }

        /**
         * @brief GameObjectのLayerを取得する。
         * @return GameObjectのLayer
         */
        LayerID getLayer() const noexcept { return m_layer; }

        /**
         * @brief GameObjectのLayerを設定する。
         * @param layer 設定するLayer
         */
        void setLayer(LayerID layer) noexcept { m_layer = layer; }

        /**
         * @brief 自身のアクティブ状態を取得する。
         * @return 自身のアクティブ状態
         */
        bool isActiveSelf() const noexcept { return m_activeSelf; }

        /**
         * @brief 親階層を含めたアクティブ状態を取得する。
         * @return 親階層を含めたアクティブ状態
         */
        bool isActiveInHierarchy() const noexcept;

        /**
         * @brief 自身のアクティブ状態を設定する。
         * @param active 設定するアクティブ状態
         */
        void setActive(bool active) noexcept;

        /**
         * @brief ローカルTransformを取得する。
         * @return ローカルTransform
         */
        Transform* getTransform() noexcept;
        const Transform* getTransform() const noexcept;

        /**
         * @brief ワールドTransformを取得する。
         * @return ワールドTransform
         */
        const Transform& getWorldTransform() const noexcept;

        /**
         * @brief ワールド座標を取得する。
         */
        Vector3 getWorldPosition() const noexcept;

        /**
         * @brief ワールド回転を取得する。
         * @return ワールド回転
         */
        Quaternion getWorldRotation() const noexcept;

        /**
         * @brief ワールドスケールを取得する。
         * @return ワールドスケール
         */
        Vector3 getWorldScale() const noexcept;

        /**
         * @brief ワールド変換行列を取得する。
         * @return ワールド変換行列
         */
        Matrix getWorldMatrix() const noexcept;

        /**
         * @brief 親GameObjectを取得する。
         * @return 親GameObjectのポインタ。親が存在しない場合はnullptr。
         */
        GameObject* getParent() const noexcept { return m_parent; }

        /**
         * @brief 親を設定する。必要に応じてワールドTransformを維持する。
         * @param parent 設定する親GameObjectのポインタ
         * @param worldPositionStays ワールドTransformを維持するかどうか
         * @return 設定に成功した場合はtrue、失敗した場合はfalse
         */
        bool setParent(GameObject* parent, bool worldPositionStays = true) noexcept;

        /**
         * @brief 直下の子GameObject数を取得する。
         * @return 直下の子GameObject数
         */
        std::size_t getChildCount() const noexcept { return m_children.size(); }

        /**
         * @brief インデックスで直下の子GameObjectを取得する。.
         * @param index 取得する子GameObjectのインデックス
         * @return 取得した子GameObjectのポインタ。インデックスが範囲外の場合はnullptr。
         */
        GameObject* getChild(std::size_t index) noexcept;
        const GameObject* getChild(std::size_t index) const noexcept;

        /**
         * @brief 子孫を含めて名前からGameObjectを検索する。.
         * @param name 検索するGameObjectの名前
         * @return 見つかったGameObjectのポインタ。見つからなかった場合はnullptr。
         */
        GameObject* find(const std::string& name) noexcept;

        /**
         * @brief GameObjectを破棄する。
         * @details GameObjectManagerが管理するGameObjectのDestroy時に呼ばれる。
         */
        void destroy() noexcept;

        /**
         * @brief GameObjectに指定した型のComponentを追加する。
         * @tparam T 追加するComponentの型。
         * @tparam Args Componentのコンストラクタに渡す引数の型。
         * @param args Componentのコンストラクタに渡す引数。
         * @return 追加されたComponentのポインタ。既に存在する場合はnullptr。
         */
        template <typename T, typename... Args>
        T* addComponent(Args&&... args)
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            const ComponentTypeInfo* typeInfo = ComponentRegistry::instance().get<T>();
            ComponentRegistry::instance().ensureRegistered<T>();
            typeInfo = ComponentRegistry::instance().get<T>();
            const auto type = std::type_index(typeid(T));
            auto& components = m_components[type];
            if (!typeInfo->allowMultiple && !components.empty())
                return static_cast<T*>(components.front().get());

            auto component = std::make_unique<T>(std::forward<Args>(args)...);
            T* result = component.get();
            result->setGameObject(this);
            result->setLifecycleEnabled(typeInfo->executeLifecycle);
            components.push_back(std::move(component));
            if (m_lifecycleAwake)
            {
                result->invokeAwake();
                if (isActiveInHierarchy() && result->isEnabled())
                    result->invokeEnable();
            }
            return result;
        }

        /**
         * @brief GameObjectが指定した型のComponentを取得する。
         * @tparam T 取得するComponentの型。
         * @return 見つかったComponentのポインタ。見つからなかった場合はnullptr。
         */
        template <typename T>
        T* getComponent() noexcept
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            const auto found = m_components.find(std::type_index(typeid(T)));
            return found == m_components.end() || found->second.empty()
                ? nullptr : static_cast<T*>(found->second.front().get());
        }

        /**
         * @brief GameObjectが指定した型のComponentを取得する。
         * @tparam T 取得するComponentの型。
         * @return 見つかったComponentのポインタ。見つからなかった場合はnullptr。
         */
        template <typename T>
        const T* getComponent() const noexcept
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            const auto found = m_components.find(std::type_index(typeid(T)));
            return found == m_components.end() || found->second.empty()
                ? nullptr : static_cast<const T*>(found->second.front().get());
        }

        /**
         * @brief GameObjectが指定した型のComponentを持っているかどうかを判定する。
         * @tparam T 判定するComponentの型。
         * @return 持っている場合はtrue、持っていない場合はfalse。
         */
        template <typename T>
        bool hasComponent() const noexcept { return getComponent<T>() != nullptr; }

        /**
         * @brief GameObjectから指定した型のComponentを削除する。
         * @tparam T 削除するComponentの型。
         * @return 削除に成功した場合はtrue、削除するComponentが存在しなかった場合はfalse。
         */
        template <typename T>
        bool removeComponent() noexcept
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            const auto found = m_components.find(std::type_index(typeid(T)));
            if (found == m_components.end())
                return false;

            if (const ComponentTypeInfo* typeInfo = ComponentRegistry::instance().get<T>(); typeInfo != nullptr && typeInfo->required)
                return false;

            for (const auto& component : found->second)
            {
                component->invokeDisable();
                component->invokeDestroy();
                component->setGameObject(nullptr);
            }
            m_components.erase(found);
            return true;
        }

        /**
         * @brief GameObjectが持つComponentの型名を取得する。
         * @return Componentの型名の配列。
         */
        std::vector<std::string_view> getComponentTypeNames() const
        {
            std::vector<std::string_view> result;
            for (const auto& [type, components] : m_components)
            {
                if (!components.empty())
                {
                    if (type == std::type_index(typeid(TransformComponent)))
                        continue;
                    if (const ComponentTypeInfo* info = ComponentRegistry::instance().get(type))
                        result.push_back(info->name);
                }
            }
            return result;
        }

        /**
         * @brief 所有する各Componentに関数を適用する。
         * @tparam Function 呼び出す関数の型。
         * @param function Componentと型情報を受け取る関数。
         */
        template <typename Function>
        void forEachComponent(Function&& function)
        {
            for (auto& [type, components] : m_components)
            {
                for (auto& component : components)
                    function(*component, type);
            }
        }

    private:

        friend class GameObjectManager;

        /**
         * @brief GameObjectを生成する。
         * @param manager 所属するGameObjectManagerの参照。
         * @param guid GameObjectのGUID。
         * @param name GameObjectの名前。
         */
        GameObject(GameObjectManager& manager, ObjectGUID guid, std::string name);

        /**
         * @brief 指定したGameObjectが自身の子孫かどうかを判定する。
         * @param object 判定するGameObjectの参照。
         * @return 子孫である場合はtrue、そうでない場合はfalse。
         */
        bool isDescendantOf(const GameObject& object) const noexcept;

        /**
         * @brief 親GameObjectから切り離す。
         * @details 親GameObjectの子リストから自身を削除し、親ポインタをnullptrに設定する。
         */
        void detachFromParent() noexcept;

        /**
         * @brief ワールド変換を更新する。
         * @details 親のTransformが変更された場合に呼ばれる。
         */
        void updateWorldTransform() const noexcept;

        /**
         * @brief GameObjectのLifecycleを初期化する。
         * @details GameObjectManagerが管理するGameObjectのAwake時に呼ばれる。
         */
        void initializeLifecycle() noexcept;

        /**
         * @brief GameObjectのLifecycleを開始する。
         * @details GameObjectManagerが管理するGameObjectの開始時に呼ばれる。
         */
        void startLifecycle() noexcept;

        /**
         * @brief GameObjectのLifecycleを更新する。
         * @details GameObjectManagerが管理するGameObjectの更新時に呼ばれる。
         * @param deltaTime 前フレームからの経過時間（秒）。
         */
        void updateLifecycle(float deltaTime) noexcept;

        /**
         * @brief GameObjectのLifecycleを固定時間間隔で更新する。
         * @details GameObjectManagerが管理するGameObjectの固定更新時に呼ばれる。
         * @param fixedDeltaTime 固定時間間隔（秒）。
         */
        void fixedUpdateLifecycle(float fixedDeltaTime) noexcept;

        /**
         * @brief GameObjectのLifecycleを遅延更新する。
         * @details GameObjectManagerが管理するGameObjectの遅延更新時に呼ばれる。
         * @param deltaTime 前フレームからの経過時間（秒）。
         */
        void lateUpdateLifecycle(float deltaTime) noexcept;

        /**
         * @brief GameObjectのLifecycleを終了する。
         * @details GameObjectManagerが管理するGameObjectの破棄時に呼ばれる。
         */
        void shutdownLifecycle() noexcept;

        /**
         * @brief GameObjectのアクティブ状態が変化したことを子GameObjectとComponentに伝播する。
         * @param wasActive 以前のアクティブ状態
         * @param isActive 現在のアクティブ状態
         */
        void propagateActiveState(bool wasActive, bool isActive) noexcept;

        GameObjectManager* m_manager = nullptr; //!< 所属するGameObjectManagerのポインタ
        ObjectGUID m_guid;   //!< GameObjectのGUID
        std::string m_name;  //!< GameObjectの名前
        TagID m_tag = 0;     //!< タグID
        LayerID m_layer = 0; //!< レイヤーID
        bool m_activeSelf = true; //!< 自身のアクティブ状態
        bool m_destroyRequested = false; //!< 破棄要求が出ているかどうか
        TransformComponent* m_transformComponent = nullptr; //!< ローカルTransformを保持する必須Component。所有しない
        GameObject* m_parent = nullptr; //!< 親GameObjectのポインタ
        std::vector<GameObject*> m_children; //!< 子GameObjectの配列
        std::unordered_map<std::type_index, std::vector<std::unique_ptr<Component>>> m_components; //!< Componentの型ごとのマップ
        mutable Transform m_worldTransform; //!< Transformのワールド変換をキャッシュするための変数
        mutable std::uint64_t m_cachedLocalRevision = 0;  //!< 自身のTransformの変更リビジョンをキャッシュするための変数
        mutable std::uint64_t m_cachedParentRevision = 0; //!< 親のTransformの変更リビジョンをキャッシュするための変数
        mutable std::uint64_t m_worldRevision = 0; //!< Transformの変更リビジョンをキャッシュするための変数
        bool m_lifecycleAwake = false; //!< LifecycleのAwakeが呼ばれたかどうか
    };
} // namespace Engine