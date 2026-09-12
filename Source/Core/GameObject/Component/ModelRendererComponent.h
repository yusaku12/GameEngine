#pragma once

#include "Assets\Model\ModelTypes.h"
#include "Core\GameObject\Component.h"

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
        void setModel(ModelHandle model) noexcept { m_model = model; }

        /**
         * @brief 表示するモデルHandleを取得する。
         * @return 表示するモデルHandle。
         */
        const ModelHandle& getModel() const noexcept { return m_model; }

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

        ModelHandle m_model;          //!< 表示するモデルHandle。
        std::uint32_t m_objectID = 0; //!< 描画用のオブジェクトID。0は無効。
        bool m_castShadows = true;    //!< Shadow Passへ登録するかどうか。
        std::string m_loadStatus;     //!< モデルのロード状態を表示する文字列。
    };
} // namespace Engine