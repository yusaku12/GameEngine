#pragma once

#include <cstdint>
#include <functional>

namespace Engine
{
    /**
     * @brief シリアライズ可能なGameObject識別子。
     *
     * @note メモリアドレスや実行時ハンドルとは独立しています。
     */
    struct ObjectGUID
    {
        std::uint64_t high = 0;
        std::uint64_t low = 0;

        [[nodiscard]] static ObjectGUID generate() noexcept;
        [[nodiscard]] bool isValid() const noexcept { return high != 0 || low != 0; }

        friend bool operator==(const ObjectGUID&, const ObjectGUID&) = default;
    };

    struct ObjectGUIDHash
    {
        std::size_t operator()(const ObjectGUID& guid) const noexcept
        {
            return std::hash<std::uint64_t>{}(guid.high)
                ^ (std::hash<std::uint64_t>{}(guid.low) + 0x9e3779b9u
                    + (std::hash<std::uint64_t>{}(guid.high) << 6)
                    + (std::hash<std::uint64_t>{}(guid.high) >> 2));
        }
    };
} // namespace Engine