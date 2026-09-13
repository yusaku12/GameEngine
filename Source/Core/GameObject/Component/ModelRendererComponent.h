#pragma once

#include "Assets\Material\MaterialTypes.h"
#include "Assets\Model\ModelTypes.h"
#include "Core\GameObject\Component.h"
#include "Graphics\Material\MaterialPropertyBlock.h"

namespace Engine
{
    /**
     * @brief GameObjectへモデル表示情報を付与するComponent。
     * @details DX12 APIを呼ばず、Render thread向けの描画Snapshotだけを提出する。
     * @thread_safety Main thread only.
     */
    class ModelRendererComponent final : public Component
    {
    public:

        ModelRendererComponent() noexcept;

        /**
         * @brief 表示するモデルHandleを設定する。
         * @param model 表示するモデルHandle。
         */
        void setModel(ModelHandle model) noexcept;

        /**
         * @brief 表示するモデルHandleを取得する。
         * @return 表示するモデルHandle。
         */
        const ModelHandle& getModel() const noexcept { return m_model; }

        /**
         * @brief 指定Material SlotのOverrideを設定する。
         * @param slotIndex Model Material SlotのIndex
         * @param material OverrideするMaterial Handle。無効HandleはOverride解除として扱う
         * @return Slotが存在する場合はtrue
         */
        bool setMaterialOverride(std::size_t slotIndex, MaterialHandle material) noexcept;

        /**
         * @brief 指定Material SlotのOverrideを解除する。
         * @param slotIndex Model Material SlotのIndex
         */
        void clearMaterialOverride(std::size_t slotIndex) noexcept;

        /**
         * @brief Material OverrideのSnapshot元配列を取得する。
         * @return Material OverrideのSnapshot元配列。
         */
        std::span<const MaterialHandle> getMaterialOverrides() const noexcept { return m_materialOverrides; }

        /**
         * @brief Renderer単位の一時Parameter Overrideを取得する。
         * @return Renderer単位の一時Parameter Override。
         */
        MaterialPropertyBlock& getMaterialPropertyBlock() noexcept { return m_propertyBlock; }
        const MaterialPropertyBlock& getMaterialPropertyBlock() const noexcept { return m_propertyBlock; }

        /**
         * @brief Shadow Passへ登録するかを設定する。
         * @param castShadows Shadow Passへ登録するかどうか。
         */
        void setCastShadows(bool castShadows) noexcept { m_castShadows = castShadows; }

        /**
         * @brief Shadow Passへ登録するかを取得する。
         * @return Shadow Passへ登録するかどうか。
         */
        bool getCastShadows() const noexcept { return m_castShadows; }

    protected:

        void onLateUpdate(float deltaTime) override;
        void onImGui() override;

    private:

        ModelHandle m_model;                             //!< 表示するモデルHandle。
        std::vector<MaterialHandle> m_materialOverrides; //!< Model Material Slotと同じIndexのOverride。
        MaterialPropertyBlock m_propertyBlock;           //!< Draw単位の数値Parameter Override。
        MaterialHandle m_inspectedMaterial;              //!< Inspectorで表示中の共有Material。
        std::uint32_t m_objectID = 0;                    //!< 描画用のオブジェクトID。0は無効。
        bool m_castShadows = true;                       //!< Shadow Passへ登録するかどうか。
        std::string m_loadStatus;                        //!< モデルのロード状態を表示する文字列。
    };
} // namespace Engine