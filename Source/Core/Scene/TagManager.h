#pragma once

namespace Engine
{
    //!< Tagを識別するID。
    using TagID = std::uint32_t;

    /**
     * @brief Tag名とTagIDを管理するクラス。
     * @thread_safety Main thread only.
     */
    class TagManager
    {
    public:

        /**
         * @brief TagManagerのシングルトンを取得する。
         * @return TagManagerのインスタンス
         */
        static TagManager& instance() noexcept;

        /**
         * @brief Tag名を登録し、そのIDを返す。
         * @param name 登録するTag名
         * @return 登録されたTagのID
         */
        TagID registerTag(std::string name);

        /**
         * @brief Tag名からIDを検索する。
         * @param name 検索するTag名
         * @return 見つかったTagのID、存在しない場合は無効なID
         */
        TagID find(std::string_view name) const noexcept;

        /**
         * @brief Tag IDから名前を取得する。
         * @param tag 検索するTagのID
         * @return 見つかったTagの名前、存在しない場合は空文字列
         */
        std::string_view getName(TagID tag) const noexcept;

        /**
         * @brief Tagが登録済みか判定する。
         * @param tag 判定するTagのID
         * @return 登録済みの場合はtrue、存在しない場合はfalse
         */
        bool contains(TagID tag) const noexcept;

    private:

        TagManager();

        std::vector<std::string> m_names;             //!< Tag IDごとの名前
        std::unordered_map<std::string, TagID> m_ids; //!< Tag名からIDへの索引
    };
} // namespace Engine