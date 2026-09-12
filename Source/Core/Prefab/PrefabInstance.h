#pragma once

#include "Core\Prefab\Prefab.h"

namespace Engine
{
    /**
     * @brief Scene上に生成されたPrefabと元アセットの対応を保持する参照。
     * @thread_safety Main thread only.
     */
    class PrefabInstance
    {
    public:

        /**
         * @brief 無効なPrefabInstanceを生成する。
         */
        PrefabInstance() = default;

        /**
         * @brief Prefabアセットと生成済みルートを関連付ける。
         * @param prefab 対応するPrefabアセット。所有しない
         * @param root 生成済みPrefabのルート。所有しない
         */
        PrefabInstance(const Prefab* prefab, GameObject* root) noexcept
            : m_prefab(prefab), m_root(root) {
        }

        /**
         * @brief 対応するPrefabアセットを取得する。
         * @return 対応するPrefabアセット
         */
        const Prefab* getPrefab() const noexcept { return m_prefab; }

        /**
         * @brief 生成されたルートGameObjectを取得する。
         * @return 生成されたルートGameObject
         */
        GameObject* getRoot() const noexcept { return m_root; }

        /**
         * @brief Prefabとルートが有効か判定する。
         * @return 有効な場合はtrue、そうでない場合はfalse
         */
        bool isValid() const noexcept { return m_prefab != nullptr && m_prefab->isValid() && m_root != nullptr; }

        /**
         * @brief Prefabの内容をScene上のインスタンスへ反映する。
         * @param scene インスタンス化するScene
         * @return 反映に成功した場合はtrue、失敗した場合はfalse
         */
        bool revert(Scene& scene);

    private:

        const Prefab* m_prefab = nullptr; //!< 対応するPrefabアセット。所有しない
        GameObject* m_root = nullptr;     //!< 生成済みPrefabのルート。所有しない
    };
} // namespace Engine