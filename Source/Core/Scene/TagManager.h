#pragma once

namespace Engine
{
    /** @brief Tagを識別するID。 */
    using TagID = std::uint32_t;

    /**
     * @brief Tag名とTagIDを管理するクラス。
     * @thread_safety Main thread only.
     */
    class TagManager
    {
    public:

        /** @brief TagManagerのシングルトンを取得する。 */
        static TagManager& instance() noexcept;

        /** @brief Tag名を登録し、そのIDを返す。 */
        TagID registerTag(std::string name);

        /** @brief Tag名からIDを検索する。 */
        TagID find(std::string_view name) const noexcept;

        /** @brief Tag IDから名前を取得する。 */
        std::string_view getName(TagID tag) const noexcept;

        /** @brief Tagが登録済みか判定する。 */
        bool contains(TagID tag) const noexcept;

    private:

        TagManager();

        std::vector<std::string> m_names; //!< Tag IDごとの名前
        std::unordered_map<std::string, TagID> m_ids; //!< Tag名からIDへの索引
    };
} // namespace Engine