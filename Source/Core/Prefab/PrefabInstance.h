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
        PrefabInstance() = default;
        PrefabInstance(const Prefab* prefab, GameObject* root) noexcept
            : m_prefab(prefab), m_root(root) {}

        /** @brief 対応するPrefabアセットを取得する。 */
        [[nodiscard]] const Prefab* getPrefab() const noexcept { return m_prefab; }
        /** @brief 生成されたルートGameObjectを取得する。 */
        [[nodiscard]] GameObject* getRoot() const noexcept { return m_root; }
        /** @brief Prefabとルートが有効か判定する。 */
        [[nodiscard]] bool isValid() const noexcept { return m_prefab != nullptr && m_prefab->isValid() && m_root != nullptr; }

        /** @brief Prefabの内容をScene上のインスタンスへ反映する。 */
        bool revert(Scene& scene);

    private:
        const Prefab* m_prefab = nullptr;
        GameObject* m_root = nullptr;
    };
} // namespace Engine