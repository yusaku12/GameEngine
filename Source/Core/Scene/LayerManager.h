#pragma once

namespace Engine
{
    //! Layerを識別するID
    using LayerID = std::uint8_t;

    //! 登録済みLayerを表すビットマスク。
    using LayerMask = std::uint32_t;

    /**
     * @brief Layer名とLayerIDを管理するクラス。
     * @thread_safety Main thread only.
     */
    class LayerManager
    {
    public:

        /**
         * @brief LayerManagerのシングルトンを取得する。
         */
        static LayerManager& instance() noexcept;

        /**
         * @brief Layer名を登録し、そのIDを返す。
         * @param name 登録するLayer名
         */
        LayerID registerLayer(std::string name);

        /**
         * @brief Layer名からIDを検索する。
         * @param name 検索するLayer名
         */
        LayerID find(std::string_view name) const noexcept;

        /**
         * @brief Layer IDから名前を取得する。
         * @param layer 取得するLayer ID
         */
        std::string_view getName(LayerID layer) const noexcept;

        /**
         * @brief Layerが登録済みか判定する。
         * @param layer 判定するLayer ID
         */
        bool contains(LayerID layer) const noexcept;

    private:

        LayerManager();

        std::array<std::string, 32> m_names; //!< Layer IDごとの名前
        LayerMask m_registeredMask = 0;      //!< 登録済みLayerのビットマスク
    };
} // namespace Engine