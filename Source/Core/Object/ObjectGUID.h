#pragma once

namespace Engine
{
    /**
     * @brief シリアライズ可能なGameObject識別子。
     * @note メモリアドレスや実行時ハンドルとは独立しています。
     */
    struct ObjectGUID
    {
        std::uint64_t high = 0; //!< 上位64bit
        std::uint64_t low = 0;  //!< 下位64bit

        /**
         * @brief 新しいGUIDを生成する。
         * @return 新しいGUID
         */
        static ObjectGUID generate() noexcept;

        /**
         * @brief GUIDが有効かどうかを判定する。
         * @return 有効な場合はtrue、無効な場合はfalse
         */
        bool isValid() const noexcept { return high != 0 || low != 0; }

        friend bool operator==(const ObjectGUID&, const ObjectGUID&) = default;
    };

    /**
     * @brief ObjectGUIDのハッシュ関数。
     */
    struct ObjectGUIDHash
    {
        /**
         * @brief ObjectGUIDのハッシュ値を計算する。
         * @param guid ハッシュ化するObjectGUID
         * @return ハッシュ値
         */
        std::size_t operator()(const ObjectGUID& guid) const noexcept
        {
            return std::hash<std::uint64_t>{}(guid.high)
                ^ (std::hash<std::uint64_t>{}(guid.low) + 0x9e3779b9u
                    + (std::hash<std::uint64_t>{}(guid.high) << 6)
                    + (std::hash<std::uint64_t>{}(guid.high) >> 2));
        }
    };
} // namespace Engine