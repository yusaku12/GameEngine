#pragma once

#include "Assets\Material\MaterialTypes.h"
#include "Graphics\Material\MaterialPropertyBlock.h"

namespace Engine
{
    /**
     * @brief 親Materialと明示的なParameter Overrideを保持するRuntime Material Instance。
     */
    class MaterialInstance
    {
    public:

        /**
         * @brief 親Material AssetのHandleを指定して生成する。
         */
        explicit MaterialInstance(MaterialHandle parent) noexcept : m_parent(parent) {}

        /**
         * @brief 親Material AssetのHandleを取得する。
         */
        MaterialHandle getParent() const noexcept { return m_parent; }

        /**
         * @brief Overrideを保持するProperty Blockを取得する。
         */
        MaterialPropertyBlock& getOverrides() noexcept { return m_overrides; }
        const MaterialPropertyBlock& getOverrides() const noexcept { return m_overrides; }

        /**
         * @brief Overrideを適用した独立Material Assetを明示的に生成する。
         * @param name 生成するMaterial Assetの名前。空文字列の場合は自動生成される。
         */
        MaterialHandle createMaterial(std::string name = {}) const;

    private:

        MaterialHandle m_parent;           //!< Parent Material AssetのHandle
        MaterialPropertyBlock m_overrides; //!< Overrideを保持するProperty Block
    };
} // namespace Engine