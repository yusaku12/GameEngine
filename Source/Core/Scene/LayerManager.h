#pragma once

namespace Engine
{
    /** @brief Layerを識別するID。 */
    using LayerID = std::uint8_t;

    /** @brief 登録済みLayerを表すビットマスク。 */
    using LayerMask = std::uint32_t;

    /**
     * @brief Layer名とLayerIDを管理するクラス。
     * @thread_safety Main thread only.
     */
    class LayerManager
    {
    public:

        /** @brief LayerManagerのシングルトンを取得する。 */
        static LayerManager& instance() noexcept;

        /** @brief Layer名を登録し、そのIDを返す。 */
        LayerID registerLayer(std::string name);

        /** @brief Layer名からIDを検索する。 */
        LayerID find(std::string_view name) const noexcept;

        /** @brief Layer IDから名前を取得する。 */
        std::string_view getName(LayerID layer) const noexcept;

        /** @brief Layerが登録済みか判定する。 */
        bool contains(LayerID layer) const noexcept;

    private:

        LayerManager();

        std::array<std::string, 32> m_names; //!< Layer IDごとの名前
        LayerMask m_registeredMask = 0;      //!< 登録済みLayerのビットマスク
    };
} // namespace Engine