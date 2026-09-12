#pragma once

#include <cstdint>

namespace Engine
{
    /**
     * @brief ModelManagerが管理するモデルResourceのHandle。
     */
    struct ModelHandle
    {
        // 無効なインデックス値。
        static constexpr std::uint32_t INVALID_INDEX = UINT32_MAX;

        std::uint32_t index = INVALID_INDEX; //!< インデックス値
        std::uint32_t generation = 0;        //!< 世代番号

        /**
         * @brief Handleが有効かどうかを判定する。
         * @return 有効な場合はtrue、無効な場合はfalseを返す。
         */
        bool isValid() const noexcept { return index != INVALID_INDEX && generation != 0; }

        /**
         * @brief 無効なHandleを取得する。
         * @return 無効なHandleを返す。
         */
        static constexpr ModelHandle Invalid() noexcept { return {}; }

        /**
         * @brief Handleの比較演算子。
         * @param other 比較対象のHandle。
         * @return 等しい場合はtrue、異なる場合はfalseを返す。
         */
        bool operator==(const ModelHandle& other) const noexcept
        {
            return index == other.index && generation == other.generation;
        }

        bool operator!=(const ModelHandle& other) const noexcept { return !(*this == other); }
    };
} // namespace Engine