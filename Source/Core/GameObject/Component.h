#pragma once

#include <type_traits>
#include "Core\Math\Transform.h"

namespace Engine
{
    class GameObject;

    /**
     * @brief GameObjectが所有する機能拡張の基底クラス。
     * @thread_safety Main thread only.
     */
    class Component
    {
    public:

        virtual ~Component() = default;

        /**
         * @brief Componentが所属するGameObjectを取得する。
         * @return GameObjectのポインタ。
         */
        GameObject* getGameObject() noexcept { return m_gameObject; }
        const GameObject* getGameObject() const noexcept { return m_gameObject; }

        /**
         * @brief Componentが所属するGameObjectのTransformを取得する。
         * @return Transformのポインタ。
         */
        Transform* getTransform() noexcept;
        const Transform* getTransform() const noexcept;

        /**
         * @brief Componentが有効かどうかを取得する。
         * @return 有効な場合はtrue、無効な場合はfalse。
         */
        bool isEnabled() const noexcept { return m_enabled; }

        /**
         * @brief Componentの有効状態を設定する。
         * @param enabled 有効にする場合はtrue、無効にする場合はfalse。
         */
        void setEnabled(bool enabled) noexcept;

        /**
         * @brief ComponentのInspector UIを描画する。
         * @details EditorUiが選択中のGameObjectを描画するときに呼ばれる。
         */
        void drawImGui() { onImGui(); }

    protected:

        friend class GameObject;

        /**
         * @brief Componentが所属するGameObjectを設定する。
         * @param gameObject 所属するGameObjectのポインタ。
         */
        void setGameObject(GameObject* gameObject) noexcept { m_gameObject = gameObject; }

        /**
         * @brief Componentの有効状態を設定する。
         * @param enabled 有効にする場合はtrue、無効にする場合はfalse。
         */
        virtual void onAwake() {}

        /**
         * @brief Componentが有効になったときに呼ばれる。
         */
        virtual void onEnable() {}

        /**
         * @brief Componentが開始されたときに呼ばれる。
         */
        virtual void onStart() {}

        /**
         * @brief Componentが更新されるときに呼ばれる。
         * @param deltaTime 前フレームからの経過時間（秒）。
         */
        virtual void onUpdate(float) {}

        /**
         * @brief Componentが固定時間間隔で更新されるときに呼ばれる。
         * @param fixedDeltaTime 固定時間間隔（秒）。
         */
        virtual void onFixedUpdate(float) {}

        /**
         * @brief Componentが遅延更新されるときに呼ばれる。
         * @param deltaTime 前フレームからの経過時間（秒）。
         */
        virtual void onLateUpdate(float) {}

        /**
         * @brief Componentが無効になったときに呼ばれる。
         */
        virtual void onDisable() {}

        /**
         * @brief Componentが破棄されるときに呼ばれる。
         */
        virtual void onDestroy() {}

        /**
         * @brief ComponentのInspector UIを描画するときに呼ばれる。
         */
        virtual void onImGui() {}

        GameObject* m_gameObject = nullptr; //!< 所属するGameObjectのポインタ。nullptrの場合は所属していない。
        bool m_enabled = true;              //!< Componentが有効かどうか。GameObjectが非アクティブの場合はfalse。

    private:

        friend class GameObjectManager;

        /**
         * @brief ComponentのLifecycleを有効にするかどうかを設定する。
         * @param enabled 有効にする場合はtrue、無効にする場合はfalse。
         */
        void setLifecycleEnabled(bool enabled) noexcept { m_lifecycleEnabled = enabled; }

        /**
         * @brief ComponentのAwake処理を呼び出す。
         * @details GameObjectManagerが管理するGameObjectのAwake時に呼ばれる。
         */
        void invokeAwake() noexcept;

        /**
         * @brief Componentの有効化処理を呼び出す。
         * @details GameObjectがアクティブになったときに呼ばれる。
         */
        void invokeEnable() noexcept;

        /**
         * @brief Componentの開始処理を呼び出す。
         * @details GameObjectManagerが管理するGameObjectの開始時に呼ばれる。
         */
        void invokeStart() noexcept;

        /**
         * @brief Componentの更新処理を呼び出す。
         * @details GameObjectManagerが管理するGameObjectの更新時に呼ばれる。
         * @param deltaTime 前フレームからの経過時間（秒）。
         */
        void invokeUpdate(float deltaTime) noexcept;

        /**
         * @brief Componentの固定更新処理を呼び出す。
         * @details GameObjectManagerが管理するGameObjectの固定更新時に呼ばれる。
         * @param fixedDeltaTime 固定時間間隔（秒）。
         */
        void invokeFixedUpdate(float fixedDeltaTime) noexcept;

        /**
         * @brief Componentの遅延更新処理を呼び出す。
         * @details GameObjectManagerが管理するGameObjectの遅延更新時に呼ばれる。
         * @param deltaTime 前フレームからの経過時間（秒）。
         */
        void invokeLateUpdate(float deltaTime) noexcept;

        /**
         * @brief Componentの無効化処理を呼び出す。
         * @details GameObjectが非アクティブになったときに呼ばれる。
         */
        void invokeDisable() noexcept;

        /**
         * @brief Componentの破棄処理を呼び出す。
         * @details GameObjectManagerが管理するGameObjectが破棄されるときに呼ばれる。
         */
        void invokeDestroy() noexcept;

        bool m_lifecycleEnabled = false; //!< Lifecycleを有効にするかどうか。GameObjectが非アクティブの場合はfalse。
        bool m_awakened = false;         //!< Awakeが呼ばれたかどうか。
        bool m_started = false;          //!< Startが呼ばれたかどうか。
        bool m_lifecycleActive = false;  //!< Lifecycleが有効かどうか。GameObjectが非アクティブの場合はfalse。
    };
} // namespace Engine