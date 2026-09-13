#pragma once

#include "Assets\Material\MaterialAsset.h"
#include "Graphics\Material\MaterialParameterLayout.h"

namespace Engine
{
    /**
     * @brief RendererまたはDraw単位の一時的な数値Parameter Override。
     * @details ShaderやRender Stateは変更しない。
     */
    class MaterialPropertyBlock
    {
    public:

        /**
         * @brief MaterialのParameter Overrideを設定する。
         * @param id MaterialParameterID
         * @param value 設定する値
         */
        bool setFloat(MaterialParameterID id, float value) noexcept;

        /**
         * @brief MaterialのParameter Overrideを設定する。
         * @param id MaterialParameterID
         * @param value 設定する値
         */
        bool setVector3(MaterialParameterID id, const Vector3& value) noexcept;

        /**
         * @brief MaterialのParameter Overrideを設定する。
         * @param id MaterialParameterID
         * @param value 設定する値
         */
        bool setVector4(MaterialParameterID id, const Vector4& value) noexcept;

        /**
         * @brief MaterialのParameter Overrideをクリアする。
         * @param id MaterialParameterID
         */
        void clear(MaterialParameterID id) noexcept;

        /**
         * @brief MaterialのParameter Overrideを全てクリアする。
         */
        void clear() noexcept;

        /**
         * @brief MaterialのParameter Overrideが設定されているかを返す。
         * @param id MaterialParameterID
         */
        bool empty() const noexcept { return m_overrideMask == 0; }

        /**
         * @brief MaterialのParameter Overrideが設定されているかを返す。
         * @param id MaterialParameterID
         */
        std::uint32_t getOverrideMask() const noexcept { return m_overrideMask; }

        /**
         * @brief MaterialのParameter Override値を返す。
         */
        const MaterialParameterValues& getValues() const noexcept { return m_values; }

        /**
         * @brief Override値をMaterial Assetへ適用する。Render Stateは変更しない。
         * @param material MaterialAsset
         */
        void applyTo(MaterialAsset& material) const noexcept;

    private:

        MaterialParameterValues m_values; //!< MaterialのParameter Override値
        std::uint32_t m_overrideMask = 0; //!< MaterialのParameter Overrideが設定されているかのビットマスク
    };
} // namespace Engine