#pragma once

namespace Engine
{
    //!< Tagを識別するID。
    using TagID = std::uint32_t;
    inline constexpr TagID INVALID_TAG_ID = static_cast<TagID>(-1);

    /**
     * @brief Tag名とTagIDを管理するクラス。
     * @thread_safety Main thread only.
     */
    class TagManager
    {
    public:

        /**
         * @brief Tag登録を一時的に行い、commitされなければ追加分を取り消す。
         * @details Main thread only。スコープ中に別のTag登録を割り込ませないこと。
         */
        class RegistrationTransaction
        {
        public:

            explicit RegistrationTransaction(TagManager& manager) noexcept;
            ~RegistrationTransaction();

            RegistrationTransaction(const RegistrationTransaction&) = delete;
            RegistrationTransaction& operator=(const RegistrationTransaction&) = delete;
            RegistrationTransaction(RegistrationTransaction&&) = delete;
            RegistrationTransaction& operator=(RegistrationTransaction&&) = delete;

            /** @brief Tagを登録し、トランザクション失敗時に取り消せるようにする。 */
            TagID registerTag(std::string name);

            /** @brief 登録したTagを確定し、破棄時のロールバックを無効にする。 */
            void commit() noexcept { m_manager = nullptr; }

        private:

            TagManager* m_manager;
            std::size_t m_initialCount;
        };

        /**
         * @brief TagManagerのシングルトンを取得する。
         * @return TagManagerのインスタンス
         */
        static TagManager& instance() noexcept;

        /**
         * @brief Tag名を登録し、そのIDを返す。
         * @param name 登録するTag名
         * @return 登録されたTagのID。空名または容量不足の場合はINVALID_TAG_ID。
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
        void rollbackTo(std::size_t count) noexcept;

        std::vector<std::string> m_names;             //!< Tag IDごとの名前
        std::unordered_map<std::string, TagID> m_ids; //!< Tag名からIDへの索引
    };
} // namespace Engine